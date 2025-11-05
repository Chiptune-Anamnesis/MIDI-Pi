#include <Arduino.h>
#include <SdFat.h>
#include <pico/mutex.h>
#include "pins.h"
#include "MidiOutput.h"
#include "MidiInput.h"
#include "MidiPlayer.h"
#include "FileBrowser.h"
#include "DisplayManager.h"
#include "InputHandler.h"
#include "RAII.h"

// Global objects
SdFat sd;
MidiOutput midiOut;
MidiInput midiIn(&midiOut);
MidiPlayer player(&midiOut);
FileBrowser browser;
DisplayManager display;
InputHandler input;

// Mutex for thread-safe access to player object (shared between Core 0 and Core 1)
mutex_t playerMutex;

// Debug flag - set to false to disable all verbose Serial output
constexpr bool ENABLE_VERBOSE_DEBUG = false;

// ============================================================================
// Constants
// ============================================================================

// Button hold timing
constexpr unsigned long BUTTON_HOLD_RESET_MS = 2000;  // Hold OK for 2 seconds to reset BPM/Velocity
constexpr unsigned long BUTTON_HOLD_JUMP_MS = 2000;   // Hold MODE for 2 seconds to jump to playback

// Display refresh rates
constexpr unsigned long VISUALIZER_REFRESH_MS = 16;   // 60Hz refresh for visualizer (playing)
constexpr unsigned long VISUALIZER_IDLE_REFRESH_MS = 500;  // 2Hz refresh when stopped (prevent I2C lockup)
constexpr unsigned long UI_REFRESH_MS = 100;          // 10Hz refresh for other UI modes

// Visualizer decay timing
constexpr unsigned long VISUALIZER_DECAY_CHECK_MS = 8;  // Check decay every 8ms (120Hz)
constexpr unsigned long VISUALIZER_PEAK_HOLD_MS = 800;   // Peak hold duration

// Visualizer thresholds
constexpr uint8_t VISUALIZER_PEAK_THRESHOLD = 10;     // Minimum increase to trigger new peak hold
constexpr uint8_t VISUALIZER_DECAY_FLOOR = 3;         // Activity floor before zeroing

// File operation limits
constexpr uint32_t MAX_FF_EVENTS_SAFETY = 50000;      // Max events to process during fast-forward seek

// MIDI timing
constexpr unsigned long SYSEX_DELAY_MS = 35;          // Delay after SysEx (MT-32 compatibility)
constexpr unsigned long MIDI_SETTLE_DELAY_MS = 10;    // General MIDI settling delay

// SD card timing
constexpr unsigned long SD_CLOSE_DELAY_MS = 20;       // Delay after closing files before opening new ones

// Transpose cooldown (prevent rapid changes that cause hung notes)
constexpr unsigned long TRANSPOSE_COOLDOWN_MS = 200;  // Minimum time between transpose changes

// Default values
constexpr uint16_t DEFAULT_TEMPO_PERCENT = 1000;      // 1000 = 100.0% = normal speed (internal use)
constexpr uint16_t MIN_TEMPO_PERCENT = 500;           // 500 = 50.0% minimum (internal use)
constexpr uint16_t MAX_TEMPO_PERCENT = 2000;          // 2000 = 200.0% maximum (internal use)

// Target BPM constants (user-facing, in hundredths: 12050 = 120.50 BPM)
constexpr uint32_t MIN_TARGET_BPM = 4000;             // 40.00 BPM
constexpr uint32_t MAX_TARGET_BPM = 30000;            // 300.00 BPM
constexpr uint32_t DEFAULT_TARGET_BPM = 12000;        // 120.00 BPM
constexpr unsigned long BPM_HOLD_THRESHOLD_MS = 500;  // Hold time to switch to coarse adjustment

constexpr uint8_t DEFAULT_VELOCITY_SCALE = 50;        // 50 = normal MIDI velocity
constexpr uint8_t USE_FILE_DEFAULT_VELOCITY = 0;      // 0 = use file default (50)
constexpr uint8_t MIN_VELOCITY_SCALE = 1;             // Minimum velocity scale
constexpr uint8_t MAX_VELOCITY_SCALE = 100;           // Maximum velocity scale
constexpr uint8_t CHANNEL_PROGRAM_USE_MIDI_FILE = 128;    // 128 = use MIDI file program
constexpr uint8_t CHANNEL_VOLUME_USE_MIDI_FILE = 255;     // 255 = use MIDI file volume
constexpr uint8_t CHANNEL_PAN_USE_MIDI_FILE = 255;        // 255 = use MIDI file pan
constexpr uint8_t CHANNEL_VOLUME_MAX = 127;           // Maximum MIDI volume
constexpr uint8_t CHANNEL_VOLUME_DEFAULT_START = 100; // Starting volume when adjusting from "use MIDI file"
constexpr uint8_t CHANNEL_PAN_CENTER = 64;            // Center pan position

// ============================================================================
// Application State Structure
// ============================================================================
// Encapsulates all application state to improve code organization and reduce
// global variable pollution

enum AppMode {
    APP_MODE_BROWSE,
    APP_MODE_PLAY,
    APP_MODE_SETTINGS,
    APP_MODE_CHANNEL_MENU,
    APP_MODE_PROGRAM_MENU,
    APP_MODE_TRACK_SETTINGS,
    APP_MODE_ROUTING,
    APP_MODE_MIDI_SETTINGS,
    APP_MODE_CLOCK_SETTINGS,
    APP_MODE_VISUALIZER
};

enum ChannelMenuOption {
    CH_OPTION_CHANNEL,
    CH_OPTION_MUTE,
    CH_OPTION_TRANSPOSE,
    CH_OPTION_PROGRAM,
    CH_OPTION_VELOCITY,  // Per-channel velocity scale
    CH_OPTION_VOLUME,
    CH_OPTION_PAN,       // Moved to top row in display
    CH_OPTION_SAVE,
    CH_OPTION_DELETE,
    CH_OPTION_COUNT
};

enum TrackMenuOption {
    TRACK_OPTION_SAVE,
    TRACK_OPTION_DELETE,
    TRACK_OPTION_BPM,
    TRACK_OPTION_VELOCITY,
    TRACK_OPTION_SYSEX,
    TRACK_OPTION_COUNT
};

enum MidiSettingsOption {
    MIDI_OPTION_THRU,
    MIDI_OPTION_KEYBOARD,
    MIDI_OPTION_KEYBOARD_CH,
    MIDI_OPTION_KEYBOARD_VEL,
    MIDI_OPTION_COUNT
};

enum ClockSettingsOption {
    CLOCK_OPTION_ENABLED,
    CLOCK_OPTION_COUNT
};

enum RoutingMenuOption {
    ROUTING_OPTION_SAVE,
    ROUTING_OPTION_DELETE,
    ROUTING_OPTION_CHANNEL,
    ROUTING_OPTION_ROUTE_TO,
    ROUTING_OPTION_COUNT
};

enum ConfirmAction {
    CONFIRM_NONE,
    CONFIRM_SAVE,
    CONFIRM_DELETE
};

// Simple visualizer state (GENaJam-Pi style, adapted for 16 channels)
struct VisualizerState {
    uint8_t velocity;         // Current base velocity (0-127)
    uint8_t expression;       // Current expression CC11 (0-127, default 127)
    uint8_t peak;             // Peak velocity (0-127)
    uint32_t peakTime;        // Timestamp of peak for hold/decay
    bool hasActiveNotes;      // Track if channel has active notes
};

struct ApplicationState {
    // Current mode and file
    AppMode currentMode;
    FatFile currentFile;
    FileEntry* lastPlayedFile;

    // Playback menu state
    PlaybackMenuOption currentPlaybackOption;
    bool playbackOptionActive;
    PlaybackMode playbackMode;
    unsigned long okButtonHoldStart;
    unsigned long modeButtonHoldStart;
    bool ignoreModeRelease;

    // Playback settings
    uint16_t tempoPercent;      // Internal tempo adjustment (1000 = 100.0%)
    uint32_t targetBPM;         // User-facing target BPM in hundredths (12050 = 120.50 BPM)
    uint32_t fileBPM_hundredths; // File's base BPM in hundredths (stored once at 100% tempo)
    uint32_t savedConfigBPM;    // BPM saved in config file (0 if no config), for reset functionality
    bool useDefaultTempo;       // True = use file default BPM
    bool useTargetBPM;          // True = user set target BPM, False = use tempo percent
    bool bpmEditingWhole;       // True = editing whole number, False = editing decimal
    uint8_t velocityScale;
    uint8_t velocityScaleDefault;
    bool sysexEnabled;          // True = send SysEx messages, False = skip them

    // Tap tempo state
    uint32_t tapTimes[4];       // Last 4 tap timestamps (milliseconds)
    uint8_t tapCount;           // Number of taps recorded (0-4)
    uint32_t lastTapTime;       // Time of last tap
    uint16_t calculatedBPM;     // Current calculated BPM from taps

    // Channel settings (per-channel configuration)
    uint8_t selectedChannel;
    uint8_t channelPrograms[16];
    uint8_t channelVolume[16];
    uint8_t channelPan[16];
    int8_t channelTranspose[16];  // Transpose in semitones (-24, -12, 0, +12, +24)
    uint8_t channelVelocity[16];  // Per-channel velocity scale (0=use MIDI file, 1-200, 100=normal)
    unsigned long lastTransposeChangeTime;  // Timestamp of last transpose change (for cooldown)
    uint16_t channelSolos;  // Bitmask for 16 channels (like channelMutes)

    // Routing settings (per-channel routing)
    uint8_t channelRouting[16];   // 255 = use original channel, 0-15 = route to that channel
    uint8_t selectedRoutingChannel;
    uint8_t originalRouting;      // Track original routing value before editing (for CC 123)

    // MIDI IN Settings
    bool midiThruEnabled;
    bool midiKeyboardEnabled;
    uint8_t midiKeyboardChannel;
    uint8_t midiKeyboardVelocity;  // 1-100, 50 = default

    // MIDI Clock Settings
    bool midiClockEnabled;

    // Visualizer state (simple velocity tracking)
    VisualizerState vizChannels[16];     // Visualizer state per channel
    uint8_t channelActivity[16];         // For display (0-127)
    uint8_t channelPeak[16];             // For display (0-127)

    // Menu state
    ChannelMenuOption currentChannelOption;
    bool channelOptionActive;
    TrackMenuOption currentTrackOption;
    bool trackOptionActive;
    RoutingMenuOption currentRoutingOption;
    bool routingOptionActive;
    MidiSettingsOption currentMidiOption;
    bool midiOptionActive;
    ClockSettingsOption currentClockOption;
    bool clockOptionActive;

    // Confirmation dialog state
    bool showingConfirmation;
    ConfirmAction pendingConfirmAction;
    bool confirmSelection;  // false = No, true = Yes

    // Flag to prevent hold-to-reset check on same frame as option activation
    bool justActivatedOption;

    // Constructor: Initialize with default values
    ApplicationState()
        : currentMode(APP_MODE_BROWSE)
        , lastPlayedFile(nullptr)
        , currentPlaybackOption(MENU_TRACK)
        , playbackOptionActive(false)
        , playbackMode(PLAYBACK_SINGLE)
        , okButtonHoldStart(0)
        , modeButtonHoldStart(0)
        , ignoreModeRelease(false)
        , tempoPercent(DEFAULT_TEMPO_PERCENT)
        , targetBPM(DEFAULT_TARGET_BPM)
        , fileBPM_hundredths(0)
        , savedConfigBPM(0)
        , useDefaultTempo(true)
        , useTargetBPM(false)
        , bpmEditingWhole(true)
        , velocityScale(DEFAULT_VELOCITY_SCALE)
        , velocityScaleDefault(DEFAULT_VELOCITY_SCALE)
        , sysexEnabled(true)  // SysEx enabled by default
        , tapCount(0)
        , lastTapTime(0)
        , calculatedBPM(0)
        , selectedChannel(0)
        , channelSolos(0)
        , selectedRoutingChannel(0)
        , originalRouting(255)
        , midiThruEnabled(false)
        , midiKeyboardEnabled(false)
        , midiKeyboardChannel(1)
        , midiKeyboardVelocity(50)
        , midiClockEnabled(false)
        , currentChannelOption(CH_OPTION_CHANNEL)
        , channelOptionActive(false)
        , currentTrackOption(TRACK_OPTION_SAVE)
        , trackOptionActive(false)
        , currentRoutingOption(ROUTING_OPTION_CHANNEL)
        , routingOptionActive(false)
        , currentMidiOption(MIDI_OPTION_THRU)
        , midiOptionActive(false)
        , currentClockOption(CLOCK_OPTION_ENABLED)
        , clockOptionActive(false)
        , showingConfirmation(false)
        , pendingConfirmAction(CONFIRM_NONE)
        , confirmSelection(false)
        , justActivatedOption(false)
    {
        // Initialize tap tempo timestamps
        for (int i = 0; i < 4; i++) {
            tapTimes[i] = 0;
        }

        // Initialize channel arrays
        for (int i = 0; i < 16; i++) {
            channelPrograms[i] = CHANNEL_PROGRAM_USE_MIDI_FILE;
            channelVolume[i] = CHANNEL_VOLUME_MAX;
            channelPan[i] = CHANNEL_PAN_CENTER;
            channelTranspose[i] = 0;  // No transpose by default
            channelVelocity[i] = 0;   // 0 = use MIDI file default
            channelRouting[i] = 255;  // 255 = use original channel (no routing)
            channelActivity[i] = 0;
            channelPeak[i] = 0;

            // Initialize visualizer state
            vizChannels[i].velocity = 0;
            vizChannels[i].expression = 127;  // Default to full expression
            vizChannels[i].peak = 0;
            vizChannels[i].peakTime = 0;
            vizChannels[i].hasActiveNotes = false;
        }
    }
};

// Global application state object (encapsulates all state variables)
ApplicationState appState;

// Hardware spin lock for visualizer data protection (Core 0 vs Core 1)
// Protects concurrent access to vizChannels[], channelActivity[], channelPeak[]
spin_lock_t* visualizerSpinLock = nullptr;

// Convenience references to appState members (for easier migration)
// These avoid having to change every variable reference throughout the code
AppMode& currentMode = appState.currentMode;
FatFile& currentFile = appState.currentFile;
FileEntry*& lastPlayedFile = appState.lastPlayedFile;
PlaybackMenuOption& currentPlaybackOption = appState.currentPlaybackOption;
bool& playbackOptionActive = appState.playbackOptionActive;
PlaybackMode& playbackMode = appState.playbackMode;
unsigned long& okButtonHoldStart = appState.okButtonHoldStart;
unsigned long& modeButtonHoldStart = appState.modeButtonHoldStart;
bool& ignoreModeRelease = appState.ignoreModeRelease;
uint16_t& tempoPercent = appState.tempoPercent;
uint32_t& targetBPM = appState.targetBPM;
uint32_t& fileBPM_hundredths = appState.fileBPM_hundredths;
uint32_t& savedConfigBPM = appState.savedConfigBPM;
bool& useDefaultTempo = appState.useDefaultTempo;
bool& useTargetBPM = appState.useTargetBPM;
bool& bpmEditingWhole = appState.bpmEditingWhole;
uint8_t& velocityScale = appState.velocityScale;
uint8_t& velocityScaleDefault = appState.velocityScaleDefault;
bool& sysexEnabled = appState.sysexEnabled;
uint32_t* tapTimes = appState.tapTimes;
uint8_t& tapCount = appState.tapCount;
uint32_t& lastTapTime = appState.lastTapTime;
uint16_t& calculatedBPM = appState.calculatedBPM;
uint8_t& selectedChannel = appState.selectedChannel;
uint8_t* channelPrograms = appState.channelPrograms;
uint8_t* channelVolume = appState.channelVolume;
uint8_t* channelPan = appState.channelPan;
int8_t* channelTranspose = appState.channelTranspose;
uint8_t* channelVelocity = appState.channelVelocity;
unsigned long& lastTransposeChangeTime = appState.lastTransposeChangeTime;
uint16_t& channelSolos = appState.channelSolos;
uint8_t* channelRouting = appState.channelRouting;
uint8_t& selectedRoutingChannel = appState.selectedRoutingChannel;
uint8_t& originalRouting = appState.originalRouting;
bool& midiThruEnabled = appState.midiThruEnabled;
bool& midiKeyboardEnabled = appState.midiKeyboardEnabled;
uint8_t& midiKeyboardChannel = appState.midiKeyboardChannel;
uint8_t& midiKeyboardVelocity = appState.midiKeyboardVelocity;
bool& midiClockEnabled = appState.midiClockEnabled;
VisualizerState* vizChannels = appState.vizChannels;
uint8_t* channelActivity = appState.channelActivity;
uint8_t* channelPeak = appState.channelPeak;
ChannelMenuOption& currentChannelOption = appState.currentChannelOption;
bool& channelOptionActive = appState.channelOptionActive;
TrackMenuOption& currentTrackOption = appState.currentTrackOption;
bool& trackOptionActive = appState.trackOptionActive;
RoutingMenuOption& currentRoutingOption = appState.currentRoutingOption;
bool& routingOptionActive = appState.routingOptionActive;
MidiSettingsOption& currentMidiOption = appState.currentMidiOption;
bool& midiOptionActive = appState.midiOptionActive;
ClockSettingsOption& currentClockOption = appState.currentClockOption;
bool& clockOptionActive = appState.clockOptionActive;
bool& showingConfirmation = appState.showingConfirmation;
ConfirmAction& pendingConfirmAction = appState.pendingConfirmAction;
bool& confirmSelection = appState.confirmSelection;
bool& justActivatedOption = appState.justActivatedOption;

// Function declarations
void handleBrowseMode(Button btn);
void handlePlayMode(Button btn);
void handleSettingsMode(Button btn);
void handleChannelMenuMode(Button btn);
void handleTrackSettingsMode(Button btn);
void handleRoutingMode(Button btn);
void handleMidiSettingsMode(Button btn);
void handleClockSettingsMode(Button btn);
void handleVisualizerMode(Button btn);
void updateDisplay();
void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void onNoteOff(uint8_t channel, uint8_t note);
void onControlChange(uint8_t channel, uint8_t cc, uint8_t value);
void updateChannelLevels();
void resetVisualizer();
bool loadAndPlayFile();
bool loadFileOnly();
void sendProgramChanges();
void sendChannelVolumes();
void sendChannelPan();
bool saveTrackSettings(const char* midiFilename);
void resetChannelSettingsToDefaults();
bool loadTrackSettings(const char* midiFilename);
int deleteTrackSettings(const char* midiFilename);
void buildConfigPath(const char* midiFilename, char* configPath, size_t configPathSize);
bool saveGlobalSettings();
bool loadGlobalSettings();
void applySoloLogic();  // Apply solo logic to mutes
void handleTapTempo();  // Handle tap tempo input
void setTargetBPM(uint32_t bpmHundredths);  // Set target BPM and calculate tempo percent

// File length cache system (max 200 entries, LRU eviction)
uint32_t getCachedFileLength(const char* filename, uint32_t modtime, uint16_t* outSysexCount = nullptr);
void cacheFileLength(const char* filename, uint32_t modtime, uint32_t lengthTicks, uint16_t sysexCount);
void calculateAndCacheFileLength(const char* fullPath, MidiFileParser& fileParser);

void setup1();  // Core 1 setup
void loop1();   // Core 1 loop

void setup() {
    Serial.begin(115200);

    // Initialize hardware spin lock for visualizer (protects concurrent access from Core 0 and Core 1)
    // Claim an unused hardware spin lock (RP2040 has 32 available)
    uint spin_lock_num = spin_lock_claim_unused(true);
    visualizerSpinLock = spin_lock_init(spin_lock_num);
    Serial.println("Visualizer spin lock initialized");

    // Initialize transpose cooldown timer
    lastTransposeChangeTime = 0;

    // Initialize display first
    if (!display.begin()) {
        Serial.println("ERROR: Display initialization failed!");
        while (1) {
            delay(1000);
        }
    }

    display.showMessage("MIDI-PI", "Initializing...");
    delay(1000);

    // Initialize SD card
    display.showMessage("Initializing", "SD Card...");

    SPI.setSCK(SD_SCK_PIN);
    SPI.setTX(SD_MOSI_PIN);
    SPI.setRX(SD_MISO_PIN);
    SPI.begin();

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // Try 12 MHz for balance of speed and stability
    // Falls back to slower speeds if initialization fails
    if (!sd.begin(SD_CS_PIN, SD_SCK_HZ(12000000))) {
        display.showError("SD Card Failed!");
        Serial.println("ERROR: SD card initialization failed!");
        while (1) {
            delay(1000);
        }
    }

    // Create MIDI folder if it doesn't exist
    if (!sd.exists("/MIDI")) {
        sd.mkdir("/MIDI");
    }

    // Initialize file browser
    display.showMessage("Scanning", "MIDI files...");
    if (!browser.begin(&sd)) {
        display.showError("No MIDI files!");
        Serial.println("WARNING: No MIDI files found");
    }

    // Initialize MIDI output
    display.showMessage("Initializing", "MIDI Out...");
    midiOut.begin();

    // Initialize MIDI input
    display.showMessage("Initializing", "MIDI In...");
    midiIn.begin();

    // Register visualizer callbacks
    midiOut.setNoteOnCallback(onNoteOn);
    midiOut.setNoteOffCallback(onNoteOff);
    midiOut.setControlChangeCallback(onControlChange);

    // Initialize input
    input.begin();

    // Initialize mutex for player object access (BEFORE loadFileOnly)
    mutex_init(&playerMutex);

    // Load global settings (MIDI IN, MIDI Clock)
    display.showMessage("Loading", "Settings...");
    loadGlobalSettings();

    // Show ready message
    display.showMessage("Ready!", "");
    delay(500);

    // Load the first file (so time/duration is visible)
    FileEntry* firstFile = browser.getCurrentFile();
    if (firstFile && !firstFile->isDirectory) {
        if (loadFileOnly()) {
            lastPlayedFile = firstFile;
        }
    }

    // Start in playback mode
    currentMode = APP_MODE_PLAY;
    display.setMode(MODE_PLAYBACK);
    updateDisplay();
}

void loop() {
    // MIDI processing moved to Core 1 for better timing
    // Core 0 handles UI, display, and file operations

    // Read input with repeat/acceleration for navigation
    // Disable acceleration for BPM option (use regular button presses only)
    Button btn;
    if (currentMode == APP_MODE_PLAY && playbackOptionActive &&
        currentPlaybackOption == MENU_BPM) {
        btn = input.readButton(); // No acceleration for BPM
    } else {
        btn = input.readButtonWithRepeat(); // Normal acceleration
    }

    // Check for MODE button hold (2 seconds) to jump to playback screen
    if (currentMode != APP_MODE_PLAY) {
        if (input.isButtonHeld(BTN_MODE)) {
            if (modeButtonHoldStart == 0) {
                // Just started holding
                modeButtonHoldStart = millis();
            } else {
                // Check if held long enough
                unsigned long holdDuration = millis() - modeButtonHoldStart;
                if (holdDuration >= BUTTON_HOLD_JUMP_MS) {
                    // Jump to playback screen
                    currentMode = APP_MODE_PLAY;
                    display.setMode(MODE_PLAYBACK);
                    playbackOptionActive = false; // Make sure no option is active
                    updateDisplay();
                    modeButtonHoldStart = 0; // Reset
                    ignoreModeRelease = true; // Ignore the upcoming button release
                    return; // Skip normal button handling
                }
            }
        } else {
            // Button released
            modeButtonHoldStart = 0;
        }
    } else {
        // In playback mode, always reset the hold timer to prevent stale values
        modeButtonHoldStart = 0;
        // Also clear the ignore flag if MODE button is not currently held
        if (!input.isButtonHeld(BTN_MODE)) {
            ignoreModeRelease = false;
        }
    }

    // Check for OK button hold (2 seconds) for various reset functions
    // Skip check on the frame where option was just activated to allow clean button state transition
    if (currentMode == APP_MODE_PLAY && playbackOptionActive && !justActivatedOption) {
        // Playback mode: reset BPM/Velocity to default
        if (input.isButtonHeld(BTN_OK)) {
            // OK is being held - start or continue tracking
            if (okButtonHoldStart == 0) {
                // Just started holding (but not from initial press - that's handled elsewhere)
                okButtonHoldStart = millis();
            } else {
                // Already holding - check duration
                unsigned long holdDuration = millis() - okButtonHoldStart;
                if (holdDuration >= BUTTON_HOLD_RESET_MS) {
                    // Held long enough - reset to default
                    if (currentPlaybackOption == MENU_BPM) {
                        // Reset to saved config BPM (if exists), otherwise file's original BPM
                        if (savedConfigBPM > 0) {
                            // Config had a saved BPM - reset to that
                            targetBPM = savedConfigBPM;
                            setTargetBPM(targetBPM);
                        } else {
                            // No saved config BPM - reset to file's original BPM (100% tempo)
                            targetBPM = fileBPM_hundredths;
                            tempoPercent = DEFAULT_TEMPO_PERCENT;
                            useDefaultTempo = false;
                            useTargetBPM = false;
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setTempoPercent(tempoPercent);
                            }
                        }
                    }
                    // Deactivate the option so user can navigate to other settings
                    playbackOptionActive = false;
                    okButtonHoldStart = 0;
                    updateDisplay();
                    return; // Skip normal button handling
                }
            }
        } else {
            // OK not held - reset tracking
            okButtonHoldStart = 0;
        }
    } else if ((currentMode == APP_MODE_CHANNEL_MENU || currentMode == APP_MODE_PROGRAM_MENU)
               && currentChannelOption == CH_OPTION_MUTE && channelOptionActive && !justActivatedOption) {
        if (input.isButtonHeld(BTN_OK)) {
            if (okButtonHoldStart == 0) {
                okButtonHoldStart = millis();
            } else {
                unsigned long holdDuration = millis() - okButtonHoldStart;
                if (holdDuration >= BUTTON_HOLD_RESET_MS) {
                    {
                        ScopedMutex lock(&playerMutex);
                        for (uint8_t ch = 0; ch < 16; ch++) {
                            player.unmuteChannel(ch);
                        }
                    }
                    channelSolos = 0;
                    channelOptionActive = false;
                    okButtonHoldStart = 0;
                    updateDisplay();
                    return;
                }
            }
        } else {
            okButtonHoldStart = 0;
        }
    } else {
        okButtonHoldStart = 0;
    }

    if (btn == BTN_STOP) {
        {
            ScopedMutex lock(&playerMutex);
            player.stop();
        }
        resetVisualizer();
        return;
    }

    if (btn == BTN_PLAY) {
        PlayerState currentState;
        {
            ScopedMutex lock(&playerMutex);
            currentState = player.getState();
        }

        if (currentState == STATE_PLAYING) {
            {
                ScopedMutex lock(&playerMutex);
                player.pause();
            }
            resetVisualizer();
        } else if (currentState == STATE_PAUSED) {
            {
                ScopedMutex lock(&playerMutex);
                player.play();
            }
        } else {
            FileEntry* currentSelection = browser.getCurrentFile();

            if (currentMode == APP_MODE_BROWSE && currentSelection && !currentSelection->isDirectory) {
                if (loadAndPlayFile()) {
                    lastPlayedFile = currentSelection;
                    currentMode = APP_MODE_PLAY;
                    display.setMode(MODE_PLAYBACK);
                    updateDisplay();
                }
            } else if (lastPlayedFile != nullptr) {
                resetVisualizer();
                loadTrackSettings(lastPlayedFile->filename);
                {
                    ScopedMutex lock(&playerMutex);
                    player.setChannelPrograms(channelPrograms);
                    player.play();
                }
            }
        }
        return;
    }

    // Skip button handling on the frame where we just activated an option
    // This gives the InputHandler a clean slate to detect new button presses
    if (justActivatedOption) {
        return;
    }

    // Handle button presses based on current mode
    unsigned long modeHandlerStart = millis();

    switch (currentMode) {
        case APP_MODE_BROWSE:
            handleBrowseMode(btn);
            break;

        case APP_MODE_PLAY:
            handlePlayMode(btn);
            break;

        case APP_MODE_SETTINGS:
            handleSettingsMode(btn);
            break;

        case APP_MODE_CHANNEL_MENU:
        case APP_MODE_PROGRAM_MENU:
            handleChannelMenuMode(btn);
            break;

        case APP_MODE_TRACK_SETTINGS:
            handleTrackSettingsMode(btn);
            break;

        case APP_MODE_ROUTING:
            handleRoutingMode(btn);
            break;

        case APP_MODE_MIDI_SETTINGS:
            handleMidiSettingsMode(btn);
            break;

        case APP_MODE_CLOCK_SETTINGS:
            handleClockSettingsMode(btn);
            break;

        case APP_MODE_VISUALIZER:
            handleVisualizerMode(btn);
            break;
    }

    updateChannelLevels();
    justActivatedOption = false;

    static unsigned long lastDisplayUpdate = 0;
    uint16_t refreshInterval;
    if (currentMode == APP_MODE_VISUALIZER) {
        PlayerState visualizerState;
        {
            ScopedMutex lock(&playerMutex);
            visualizerState = player.getState();
        }
        refreshInterval = (visualizerState == STATE_PLAYING) ? VISUALIZER_REFRESH_MS : VISUALIZER_IDLE_REFRESH_MS;
    } else {
        refreshInterval = UI_REFRESH_MS;
    }

    if (millis() - lastDisplayUpdate > refreshInterval) {
        updateDisplay();
        lastDisplayUpdate = millis();
    }

    static PlayerState lastPlayerState = STATE_STOPPED;
    PlayerState currentPlayerState;
    bool hasReachedEnd;
    {
        ScopedMutex lock(&playerMutex);
        currentPlayerState = player.getState();
        hasReachedEnd = player.hasReachedEnd();
    }

    if (lastPlayerState == STATE_PLAYING && currentPlayerState == STATE_STOPPED && hasReachedEnd) {
        FileEntry* fileEntry = nullptr;

        switch (playbackMode) {
            case PLAYBACK_SINGLE:
                resetVisualizer();
                break;

            case PLAYBACK_AUTO_NEXT:
                browser.selectNext();
                fileEntry = browser.getCurrentFile();
                if (fileEntry && !fileEntry->isDirectory) {
                    if (loadAndPlayFile()) {
                        lastPlayedFile = fileEntry;
                    }
                }
                break;

            case PLAYBACK_LOOP_ONE:
                fileEntry = browser.getCurrentFile();
                if (fileEntry && !fileEntry->isDirectory) {
                    if (loadAndPlayFile()) {
                        lastPlayedFile = fileEntry;
                    }
                }
                break;

            case PLAYBACK_LOOP_ALL:
                browser.selectNext();
                fileEntry = browser.getCurrentFile();
                if (fileEntry && !fileEntry->isDirectory) {
                    if (loadAndPlayFile()) {
                        lastPlayedFile = fileEntry;
                    }
                } else {
                    // Reached end, loop back to first
                    while (browser.getCurrentIndex() > 0) {
                        browser.selectPrevious();
                    }
                    fileEntry = browser.getCurrentFile();
                    if (fileEntry && !fileEntry->isDirectory) {
                        if (loadAndPlayFile()) {
                            lastPlayedFile = fileEntry;
                        }
                    }
                }
                break;
        }
    }

    lastPlayerState = currentPlayerState;
}

void handleBrowseMode(Button btn) {
    switch (btn) {
        case BTN_LEFT:
            // Previous file/folder
            browser.selectPrevious();
            updateDisplay();
            break;

        case BTN_RIGHT:
            // Next file/folder
            browser.selectNext();
            updateDisplay();
            break;

        case BTN_OK:
            // Enter directory or load file (don't play yet)
            {
                FileEntry* current = browser.getCurrentFile();
                if (current) {
                    if (current->isDirectory) {
                        browser.enterDirectory();
                        updateDisplay();
                    } else {
                        // Load file only (don't play)
                        if (loadFileOnly()) {
                            lastPlayedFile = current;
                            currentMode = APP_MODE_PLAY;
                            display.setMode(MODE_PLAYBACK);
                            updateDisplay();
                        }
                    }
                }
            }
            break;

        // BTN_PLAY is handled globally - not here

        // BTN_STOP is handled globally - not here

        case BTN_MODE:
            // Return to player screen
            currentMode = APP_MODE_PLAY;
            display.setMode(MODE_PLAYBACK);
            updateDisplay();
            break;

        default:
            break;
    }
}

void handlePlayMode(Button btn) {
    // Hold-to-reset handling is done in main loop()

    switch (btn) {
        case BTN_LEFT:
            if (playbackOptionActive) {
                // Active option - decrease value
                switch (currentPlaybackOption) {
                    case MENU_TRACK:
                        // TRACK doesn't have active mode
                        break;

                    case MENU_BPM:
                        {
                            // Adjust target BPM based on which part is being edited
                            uint32_t decrement = bpmEditingWhole ? 100 : 1; // 100 = 1.00 BPM, 1 = 0.01 BPM
                            if (targetBPM > MIN_TARGET_BPM + decrement) {
                                targetBPM -= decrement;
                                setTargetBPM(targetBPM);
                            } else {
                                targetBPM = MIN_TARGET_BPM;
                                setTargetBPM(targetBPM);
                            }
                        }
                        break;

                    case MENU_TAP:
                        // LEFT button taps tempo
                        handleTapTempo();
                        break;

                    case MENU_TIME:
                        // Rewind 1 second
                        {
                            ScopedMutex lock(&playerMutex);
                            player.rewind(1000);
                        }
                        break;

                    case MENU_MODE:
                        // Cycle playback mode backwards
                        playbackMode = (PlaybackMode)((playbackMode - 1 + 4) % 4);
                        break;

                    case MENU_PREV:
                    case MENU_NEXT:
                        // PREV/NEXT don't have active mode
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu left
                currentPlaybackOption = (PlaybackMenuOption)((currentPlaybackOption - 1 + MENU_COUNT) % MENU_COUNT);
            }
            break;

        case BTN_RIGHT:
            if (playbackOptionActive) {
                // Active option - increase value
                switch (currentPlaybackOption) {
                    case MENU_TRACK:
                        // TRACK doesn't have active mode
                        break;

                    case MENU_BPM:
                        {
                            // Adjust target BPM based on which part is being edited
                            uint32_t increment = bpmEditingWhole ? 100 : 1; // 100 = 1.00 BPM, 1 = 0.01 BPM
                            if (targetBPM < MAX_TARGET_BPM - increment) {
                                targetBPM += increment;
                                setTargetBPM(targetBPM);
                            } else {
                                targetBPM = MAX_TARGET_BPM;
                                setTargetBPM(targetBPM);
                            }
                        }
                        break;

                    case MENU_TAP:
                        // RIGHT button taps tempo
                        handleTapTempo();
                        break;

                    case MENU_TIME:
                        // Fast forward 1 second
                        {
                            ScopedMutex lock(&playerMutex);
                            player.fastForward(1000);
                        }
                        break;

                    case MENU_MODE:
                        // Cycle playback mode forwards
                        playbackMode = (PlaybackMode)((playbackMode + 1) % 4);
                        break;

                    case MENU_PREV:
                    case MENU_NEXT:
                        // PREV/NEXT don't have active mode
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu right
                currentPlaybackOption = (PlaybackMenuOption)((currentPlaybackOption + 1) % MENU_COUNT);
            }
            break;

        case BTN_OK:
            // Handle OK button
            if (currentPlaybackOption == MENU_TRACK) {
                // Enter file browser for track selection
                playbackOptionActive = false;
                currentMode = APP_MODE_BROWSE;
                display.setMode(MODE_FILE_BROWSER);
                updateDisplay();
            } else if (currentPlaybackOption == MENU_PREV) {
                // Previous song
                bool wasPlaying;
                {
                    ScopedMutex lock(&playerMutex);
                    wasPlaying = (player.getState() == STATE_PLAYING);
                }

                browser.selectPrevious();
                FileEntry* fileEntry = browser.getCurrentFile();
                if (fileEntry && !fileEntry->isDirectory) {
                    resetVisualizer();
                    // Load and optionally play previous song (unloadFile handles stop with proper delay)
                    if (wasPlaying) {
                        // Was playing - load and auto-play
                        if (loadAndPlayFile()) {
                            lastPlayedFile = fileEntry;
                        }
                    } else {
                        // Was stopped - load only, don't play
                        if (loadFileOnly()) {
                            lastPlayedFile = fileEntry;
                        }
                    }
                }
            } else if (currentPlaybackOption == MENU_NEXT) {
                // Next song
                bool wasPlaying;
                {
                    ScopedMutex lock(&playerMutex);
                    wasPlaying = (player.getState() == STATE_PLAYING);
                }

                browser.selectNext();
                FileEntry* fileEntry = browser.getCurrentFile();
                if (fileEntry && !fileEntry->isDirectory) {
                    resetVisualizer();
                    // Load and optionally play next song (unloadFile handles stop with proper delay)
                    if (wasPlaying) {
                        // Was playing - load and auto-play
                        if (loadAndPlayFile()) {
                            lastPlayedFile = fileEntry;
                        }
                    } else {
                        // Was stopped - load only, don't play
                        if (loadFileOnly()) {
                            lastPlayedFile = fileEntry;
                        }
                    }
                }
            } else {
                // For BPM - special handling for whole/decimal toggle cycle
                // Cycle: inactive (whole) → active whole → inactive (decimal) → active decimal → inactive (whole)
                if (currentPlaybackOption == MENU_BPM) {
                    if (playbackOptionActive) {
                        // Deactivate and switch to the other part for next activation
                        playbackOptionActive = false;
                        bpmEditingWhole = !bpmEditingWhole; // Toggle for next time
                    } else {
                        // Activate editing of the current part (whole or decimal based on bpmEditingWhole)
                        playbackOptionActive = true;
                        // Keep bpmEditingWhole as is - it indicates which part to edit
                    }

                    // Don't start hold tracking immediately - let user release OK first
                    okButtonHoldStart = 0;
                    if (playbackOptionActive) {
                        justActivatedOption = true;
                    }
                } else {
                    // For TAP, TIME, MODE - toggle active state
                    bool wasActive = playbackOptionActive;
                    playbackOptionActive = !playbackOptionActive;

                    // Reset tap state when deactivating TAP mode
                    if (wasActive && !playbackOptionActive && currentPlaybackOption == MENU_TAP) {
                        tapCount = 0;
                        lastTapTime = 0;
                        calculatedBPM = 0;
                    }

                    // Don't start hold tracking immediately - let user release OK first
                    okButtonHoldStart = 0;
                    if (playbackOptionActive) {
                        justActivatedOption = true;
                    }
                }
            }
            break;

        // BTN_PLAY is handled globally - not here

        // BTN_STOP is handled globally - not here

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false; // Clear flag
                break; // Ignore this MODE press
            }
            // Cycle to channel menu
            currentMode = APP_MODE_CHANNEL_MENU;
            display.setMode(MODE_CHANNEL_MENU);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic - All Notes Off on all channels
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

void handleSettingsMode(Button btn) {
    // Settings mode not fully implemented yet
    // Could add channel mute/unmute, global tempo adjust, etc.

    switch (btn) {
        case BTN_OK:
            // Exit settings
            currentMode = APP_MODE_BROWSE;
            display.setMode(MODE_FILE_BROWSER);
            updateDisplay();
            break;

        // BTN_STOP is handled globally - not here

        default:
            break;
    }
}

void handleChannelMenuMode(Button btn) {
    switch (btn) {
        case BTN_RIGHT:
            // If showing confirmation, toggle Yes/No
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (channelOptionActive) {
                // Active - increase value
                switch (currentChannelOption) {
                    case CH_OPTION_CHANNEL:
                        selectedChannel = (selectedChannel + 1) % 16;
                        break;

                    case CH_OPTION_MUTE:
                        // Cycle through: unmuted (X) → muted (O) → solo (S) → unmuted
                        {
                            bool isMuted, isSolo;
                            {
                                ScopedMutex lock(&playerMutex);
                                isMuted = (player.getChannelMutes() & (1 << selectedChannel)) != 0;
                            }
                            isSolo = (channelSolos & (1 << selectedChannel)) != 0;

                            {
                                ScopedMutex lock(&playerMutex);
                                if (!isMuted && !isSolo) {
                                    // Currently unmuted → set to muted
                                    player.muteChannel(selectedChannel);
                                } else if (isMuted && !isSolo) {
                                    // Currently muted → set to solo (unmute and set solo)
                                    player.unmuteChannel(selectedChannel);
                                    channelSolos |= (1 << selectedChannel);
                                } else if (!isMuted && isSolo) {
                                    // Currently solo → set to unmuted (clear solo)
                                    channelSolos &= ~(1 << selectedChannel);
                                }
                            }
                            applySoloLogic();  // Apply solo logic to all channels
                        }
                        break;

                    case CH_OPTION_TRANSPOSE:
                        // Check cooldown to prevent rapid transpose changes that cause hung notes
                        if (millis() - lastTransposeChangeTime >= TRANSPOSE_COOLDOWN_MS) {
                            // Send All Notes Off (CC 123) on this channel before changing transpose
                            // This is more efficient and reliable than sending 128 individual note-offs
                            midiOut.sendControlChange(selectedChannel + 1, 123, 0);
                            delay(20); // Give device time to process the All Notes Off message

                            // Cycle through transpose values: -24, -12, 0, +12, +24
                            if (channelTranspose[selectedChannel] == -24) {
                                channelTranspose[selectedChannel] = -12;
                            } else if (channelTranspose[selectedChannel] == -12) {
                                channelTranspose[selectedChannel] = 0;
                            } else if (channelTranspose[selectedChannel] == 0) {
                                channelTranspose[selectedChannel] = 12;
                            } else if (channelTranspose[selectedChannel] == 12) {
                                channelTranspose[selectedChannel] = 24;
                            } else {
                                channelTranspose[selectedChannel] = -24; // Wrap around
                            }
                            // Update player immediately
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelTranspose(channelTranspose);
                            }
                            // Update timestamp for cooldown
                            lastTransposeChangeTime = millis();
                        }
                        break;

                    case CH_OPTION_PROGRAM:
                        channelPrograms[selectedChannel] = (channelPrograms[selectedChannel] + 1) % 129;
                        // Send program change immediately (but not if set to 128 = use MIDI file)
                        if (channelPrograms[selectedChannel] < CHANNEL_PROGRAM_USE_MIDI_FILE) {
                            midiOut.sendProgramChange(selectedChannel + 1, channelPrograms[selectedChannel]);
                        }
                        break;

                    case CH_OPTION_VELOCITY:
                        // Increment velocity scale (0=use MIDI file, 1-200, step by 5)
                        if (channelVelocity[selectedChannel] == 0) {
                            channelVelocity[selectedChannel] = 50; // Start from 50% when first adjusted
                        } else if (channelVelocity[selectedChannel] < 200) {
                            channelVelocity[selectedChannel] += 5;
                            if (channelVelocity[selectedChannel] > 200) {
                                channelVelocity[selectedChannel] = 200;
                            }
                        } else {
                            // Wrap back to 0 (use MIDI file default)
                            channelVelocity[selectedChannel] = 0;
                        }
                        // Update player velocity scales
                        {
                            ScopedMutex lock(&playerMutex);
                            player.setChannelVelocityScales(channelVelocity);
                        }
                        break;

                    case CH_OPTION_PAN:
                        // Increment pan (0-127, 64=center, 255=use MIDI file)
                        if (channelPan[selectedChannel] == 255) {
                            channelPan[selectedChannel] = 0; // Start from left when first adjusted
                        } else if (channelPan[selectedChannel] < 127) {
                            channelPan[selectedChannel]++;
                        } else {
                            // Wrap back to 255 (use MIDI file default)
                            channelPan[selectedChannel] = 255;
                        }
                        // Send CC 10 (Pan) immediately and update player override (but not if 255)
                        if (channelPan[selectedChannel] < 255) {
                            midiOut.sendControlChange(selectedChannel + 1, 10, channelPan[selectedChannel]);
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelPan(channelPan);
                            }
                        }
                        break;

                    case CH_OPTION_VOLUME:
                        // Increment volume (0-127, 255=use MIDI file)
                        if (channelVolume[selectedChannel] == CHANNEL_VOLUME_USE_MIDI_FILE) {
                            channelVolume[selectedChannel] = 0; // Start from 0 when first adjusted
                        } else if (channelVolume[selectedChannel] < CHANNEL_VOLUME_MAX) {
                            channelVolume[selectedChannel]++;
                        } else {
                            // Wrap back to 255 (use MIDI file default)
                            channelVolume[selectedChannel] = CHANNEL_VOLUME_USE_MIDI_FILE;
                        }
                        // Send CC 7 (Volume) immediately and update player override (but not if 255)
                        if (channelVolume[selectedChannel] < CHANNEL_VOLUME_USE_MIDI_FILE) {
                            midiOut.sendControlChange(selectedChannel + 1, 7, channelVolume[selectedChannel]);
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelVolumes(channelVolume);
                            }
                        }
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu right
                currentChannelOption = (ChannelMenuOption)((currentChannelOption + 1) % CH_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_LEFT:
            // If showing confirmation, toggle Yes/No
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (channelOptionActive) {
                // Active - decrease value
                switch (currentChannelOption) {
                    case CH_OPTION_CHANNEL:
                        selectedChannel = (selectedChannel - 1 + 16) % 16;
                        break;

                    case CH_OPTION_MUTE:
                        // Cycle through (backwards): unmuted (X) ← solo (S) ← muted (O) ← unmuted
                        {
                            bool isMuted, isSolo;
                            {
                                ScopedMutex lock(&playerMutex);
                                isMuted = (player.getChannelMutes() & (1 << selectedChannel)) != 0;
                            }
                            isSolo = (channelSolos & (1 << selectedChannel)) != 0;

                            {
                                ScopedMutex lock(&playerMutex);
                                if (!isMuted && !isSolo) {
                                    // Currently unmuted → set to solo
                                    channelSolos |= (1 << selectedChannel);
                                } else if (!isMuted && isSolo) {
                                    // Currently solo → set to muted (clear solo and mute)
                                    channelSolos &= ~(1 << selectedChannel);
                                    player.muteChannel(selectedChannel);
                                } else if (isMuted && !isSolo) {
                                    // Currently muted → set to unmuted
                                    player.unmuteChannel(selectedChannel);
                                }
                            }
                            applySoloLogic();  // Apply solo logic to all channels
                        }
                        break;

                    case CH_OPTION_TRANSPOSE:
                        // Check cooldown to prevent rapid transpose changes that cause hung notes
                        if (millis() - lastTransposeChangeTime >= TRANSPOSE_COOLDOWN_MS) {
                            // Send All Notes Off (CC 123) on this channel before changing transpose
                            // This is more efficient and reliable than sending 128 individual note-offs
                            midiOut.sendControlChange(selectedChannel + 1, 123, 0);
                            delay(20); // Give device time to process the All Notes Off message

                            // Cycle through transpose values backwards: +24, +12, 0, -12, -24
                            if (channelTranspose[selectedChannel] == 24) {
                                channelTranspose[selectedChannel] = 12;
                            } else if (channelTranspose[selectedChannel] == 12) {
                                channelTranspose[selectedChannel] = 0;
                            } else if (channelTranspose[selectedChannel] == 0) {
                                channelTranspose[selectedChannel] = -12;
                            } else if (channelTranspose[selectedChannel] == -12) {
                                channelTranspose[selectedChannel] = -24;
                            } else {
                                channelTranspose[selectedChannel] = 24; // Wrap around
                            }
                            // Update player immediately
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelTranspose(channelTranspose);
                            }
                            // Update timestamp for cooldown
                            lastTransposeChangeTime = millis();
                        }
                        break;

                    case CH_OPTION_PROGRAM:
                        channelPrograms[selectedChannel] = (channelPrograms[selectedChannel] - 1 + 129) % 129;
                        // Send program change immediately (but not if set to 128 = use MIDI file)
                        if (channelPrograms[selectedChannel] < CHANNEL_PROGRAM_USE_MIDI_FILE) {
                            midiOut.sendProgramChange(selectedChannel + 1, channelPrograms[selectedChannel]);
                        }
                        break;

                    case CH_OPTION_VELOCITY:
                        // Decrement velocity scale (0=use MIDI file, 1-200, step by 5)
                        if (channelVelocity[selectedChannel] == 0) {
                            channelVelocity[selectedChannel] = 200; // Start from 200% when going backwards
                        } else if (channelVelocity[selectedChannel] > 50) {
                            channelVelocity[selectedChannel] -= 5;
                            if (channelVelocity[selectedChannel] < 1) {
                                channelVelocity[selectedChannel] = 1;
                            }
                        } else {
                            // Wrap back to 0 (use MIDI file default)
                            channelVelocity[selectedChannel] = 0;
                        }
                        // Update player velocity scales
                        {
                            ScopedMutex lock(&playerMutex);
                            player.setChannelVelocityScales(channelVelocity);
                        }
                        break;

                    case CH_OPTION_PAN:
                        // Decrement pan (0-127, 64=center, 255=use MIDI file)
                        if (channelPan[selectedChannel] == 255) {
                            channelPan[selectedChannel] = 127; // Start from right when first adjusted (going backwards)
                        } else if (channelPan[selectedChannel] > 0) {
                            channelPan[selectedChannel]--;
                        } else {
                            // Wrap back to 255 (use MIDI file default)
                            channelPan[selectedChannel] = 255;
                        }
                        // Send CC 10 (Pan) immediately and update player override (but not if 255)
                        if (channelPan[selectedChannel] < 255) {
                            midiOut.sendControlChange(selectedChannel + 1, 10, channelPan[selectedChannel]);
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelPan(channelPan);
                            }
                        }
                        break;

                    case CH_OPTION_VOLUME:
                        // Decrement volume (0-127, 255=use MIDI file)
                        if (channelVolume[selectedChannel] == CHANNEL_VOLUME_USE_MIDI_FILE) {
                            channelVolume[selectedChannel] = CHANNEL_VOLUME_MAX; // Start from max when first adjusted (going backwards)
                        } else if (channelVolume[selectedChannel] > 0) {
                            channelVolume[selectedChannel]--;
                        } else {
                            // Wrap back to 255 (use MIDI file default)
                            channelVolume[selectedChannel] = CHANNEL_VOLUME_USE_MIDI_FILE;
                        }
                        // Send CC 7 (Volume) immediately and update player override (but not if 255)
                        if (channelVolume[selectedChannel] < CHANNEL_VOLUME_USE_MIDI_FILE) {
                            midiOut.sendControlChange(selectedChannel + 1, 7, channelVolume[selectedChannel]);
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelVolumes(channelVolume);
                            }
                        }
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu left
                currentChannelOption = (ChannelMenuOption)((currentChannelOption - 1 + CH_OPTION_COUNT) % CH_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_OK:
            // If showing confirmation, handle Yes/No selection
            if (showingConfirmation) {
                if (confirmSelection) {
                    // User selected Yes - execute the action
                    FileEntry* entry = browser.getCurrentFile();
                    if (entry && !entry->isDirectory) {
                        if (pendingConfirmAction == CONFIRM_SAVE) {
                            if (saveTrackSettings(entry->filename)) {
                                display.showMessage("Settings", "Saved!");
                                delay(1000);
                            } else {
                                display.showError("Save Failed!");
                                delay(1000);
                            }
                        } else if (pendingConfirmAction == CONFIRM_DELETE) {
                            int result = deleteTrackSettings(entry->filename);
                            if (result == 1) {
                                // Deleted successfully
                                resetChannelSettingsToDefaults();
                                display.showMessage("Settings", "Deleted!");
                                delay(1000);
                            } else if (result == 0) {
                                // No file to delete
                                display.showMessage("No Settings", "to Delete");
                                delay(1000);
                            } else {
                                // Delete failed
                                display.showError("Delete Failed!");
                                delay(1000);
                            }
                        }
                    }
                }
                // Cancel confirmation (whether Yes or No)
                showingConfirmation = false;
                pendingConfirmAction = CONFIRM_NONE;
                confirmSelection = false;
                updateDisplay();
                break;
            }

            // Handle based on option
            switch (currentChannelOption) {
                case CH_OPTION_SAVE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_SAVE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                case CH_OPTION_DELETE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_DELETE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                default:
                    // For other options, toggle active state
                    channelOptionActive = !channelOptionActive;
                    // Reset hold tracking to prevent immediate hold-to-reset trigger
                    okButtonHoldStart = 0;
                    // Set flag to skip hold check on this frame
                    if (channelOptionActive) {
                        justActivatedOption = true;
                    }
                    break;
            }
            updateDisplay();
            break;

        // BTN_STOP is handled globally - not here

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false; // Clear flag
                break; // Ignore this MODE press
            }
            // Cycle to Track settings
            currentChannelOption = CH_OPTION_CHANNEL;
            channelOptionActive = false;
            currentMode = APP_MODE_TRACK_SETTINGS;
            currentTrackOption = TRACK_OPTION_BPM;
            trackOptionActive = false;
            display.setMode(MODE_SETTINGS);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic from channel menu too
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

void handleTrackSettingsMode(Button btn) {
    switch (btn) {
        case BTN_RIGHT:
            // If showing confirmation, toggle Yes/No
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (trackOptionActive) {
                switch (currentTrackOption) {
                    case TRACK_OPTION_BPM:
                        if (useDefaultTempo) {
                            useDefaultTempo = false;
                            targetBPM = fileBPM_hundredths;
                            setTargetBPM(targetBPM);
                        } else {
                            uint32_t increment = bpmEditingWhole ? 100 : 1;
                            if (targetBPM < MAX_TARGET_BPM - increment) {
                                targetBPM += increment;
                                setTargetBPM(targetBPM);
                            } else {
                                targetBPM = MAX_TARGET_BPM;
                                setTargetBPM(targetBPM);
                            }
                        }
                        break;

                    case TRACK_OPTION_VELOCITY:
                        if (velocityScale == USE_FILE_DEFAULT_VELOCITY) {
                            velocityScale = DEFAULT_VELOCITY_SCALE;
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setVelocityScale(velocityScale);
                            }
                        } else if (velocityScale < MAX_VELOCITY_SCALE) {
                            velocityScale++;
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setVelocityScale(velocityScale);
                            }
                        }
                        break;

                    case TRACK_OPTION_SYSEX:
                        sysexEnabled = !sysexEnabled;
                        {
                            ScopedMutex lock(&playerMutex);
                            player.setSysexEnabled(sysexEnabled);
                        }
                        break;

                    default:
                        break;
                }
            } else {
                currentTrackOption = (TrackMenuOption)((currentTrackOption + 1) % TRACK_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_LEFT:
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (trackOptionActive) {
                switch (currentTrackOption) {
                    case TRACK_OPTION_BPM:
                        if (useDefaultTempo) {
                        } else {
                            uint32_t decrement = bpmEditingWhole ? 100 : 1;
                            if (targetBPM > MIN_TARGET_BPM + decrement) {
                                targetBPM -= decrement;
                                setTargetBPM(targetBPM);
                            } else {
                                targetBPM = MIN_TARGET_BPM;
                                setTargetBPM(targetBPM);
                            }
                        }
                        break;

                    case TRACK_OPTION_VELOCITY:
                        if (velocityScale == USE_FILE_DEFAULT_VELOCITY) {
                        } else if (velocityScale > MIN_VELOCITY_SCALE) {
                            velocityScale--;
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setVelocityScale(velocityScale);
                            }
                        } else {
                            velocityScale = USE_FILE_DEFAULT_VELOCITY;
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setVelocityScale(DEFAULT_VELOCITY_SCALE);
                            }
                        }
                        break;

                    case TRACK_OPTION_SYSEX:
                        sysexEnabled = !sysexEnabled;
                        {
                            ScopedMutex lock(&playerMutex);
                            player.setSysexEnabled(sysexEnabled);
                        }
                        break;

                    default:
                        break;
                }
            } else {
                currentTrackOption = (TrackMenuOption)((currentTrackOption - 1 + TRACK_OPTION_COUNT) % TRACK_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_OK:
            // If showing confirmation, handle Yes/No selection
            if (showingConfirmation) {
                if (confirmSelection) {
                    // User selected Yes - execute the action
                    FileEntry* entry = browser.getCurrentFile();
                    if (entry && !entry->isDirectory) {
                        if (pendingConfirmAction == CONFIRM_SAVE) {
                            if (saveTrackSettings(entry->filename)) {
                                display.showMessage("Settings", "Saved!");
                                delay(1000);
                            } else {
                                display.showError("Save Failed!");
                                delay(1000);
                            }
                        } else if (pendingConfirmAction == CONFIRM_DELETE) {
                            int result = deleteTrackSettings(entry->filename);
                            if (result == 1) {
                                // Deleted successfully
                                resetChannelSettingsToDefaults();
                                display.showMessage("Settings", "Deleted!");
                                delay(1000);
                            } else if (result == 0) {
                                // No file to delete
                                display.showMessage("No Settings", "to Delete");
                                delay(1000);
                            } else {
                                // Delete failed
                                display.showError("Delete Failed!");
                                delay(1000);
                            }
                        }
                    }
                }
                // Cancel confirmation (whether Yes or No)
                showingConfirmation = false;
                pendingConfirmAction = CONFIRM_NONE;
                confirmSelection = false;
                updateDisplay();
                break;
            }

            // Handle based on option
            switch (currentTrackOption) {
                case TRACK_OPTION_SAVE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_SAVE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                case TRACK_OPTION_DELETE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_DELETE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                case TRACK_OPTION_BPM:
                    // Special handling for BPM whole/decimal toggle cycle
                    if (trackOptionActive) {
                        // Deactivate and switch to the other part for next activation
                        trackOptionActive = false;
                        bpmEditingWhole = !bpmEditingWhole; // Toggle for next time
                    } else {
                        // Activate editing of the current part (whole or decimal)
                        trackOptionActive = true;
                    }
                    okButtonHoldStart = 0;
                    if (trackOptionActive) {
                        justActivatedOption = true;
                    }
                    break;

                default:
                    // For other options, toggle active state
                    trackOptionActive = !trackOptionActive;
                    okButtonHoldStart = 0;
                    if (trackOptionActive) {
                        justActivatedOption = true;
                    }
                    break;
            }
            updateDisplay();
            break;

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false;
                break;
            }
            // Cycle to Routing menu
            currentTrackOption = TRACK_OPTION_BPM;
            trackOptionActive = false;
            currentMode = APP_MODE_ROUTING;
            currentRoutingOption = ROUTING_OPTION_CHANNEL;
            routingOptionActive = false;
            display.setMode(MODE_SETTINGS);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0);
                midiOut.sendControlChange(ch, 120, 0);
            }
            break;

        default:
            break;
    }
}

void handleRoutingMode(Button btn) {
    switch (btn) {
        case BTN_RIGHT:
            // If showing confirmation, toggle Yes/No
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (routingOptionActive) {
                // Active - increase value
                switch (currentRoutingOption) {
                    case ROUTING_OPTION_CHANNEL:
                        // Cycle to next channel (0-15)
                        selectedRoutingChannel = (selectedRoutingChannel + 1) % 16;
                        break;

                    case ROUTING_OPTION_ROUTE_TO:
                        // Cycle through routing options: --, 1, 2, ..., 16
                        if (channelRouting[selectedRoutingChannel] == 255) {
                            // Currently "--" (no routing), change to 1
                            channelRouting[selectedRoutingChannel] = 0;
                        } else if (channelRouting[selectedRoutingChannel] < 15) {
                            // Increment channel
                            channelRouting[selectedRoutingChannel]++;
                        } else {
                            // At 16, wrap to "--"
                            channelRouting[selectedRoutingChannel] = 255;
                        }
                        // Note: Routing is applied when OK is pressed to deactivate
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu right
                currentRoutingOption = (RoutingMenuOption)((currentRoutingOption + 1) % ROUTING_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_LEFT:
            // If showing confirmation, toggle Yes/No
            if (showingConfirmation) {
                confirmSelection = !confirmSelection;
                updateDisplay();
                break;
            }

            if (routingOptionActive) {
                // Active - decrease value
                switch (currentRoutingOption) {
                    case ROUTING_OPTION_CHANNEL:
                        // Cycle to previous channel (0-15)
                        selectedRoutingChannel = (selectedRoutingChannel - 1 + 16) % 16;
                        break;

                    case ROUTING_OPTION_ROUTE_TO:
                        // Cycle through routing options: 16, 15, ..., 1, --
                        if (channelRouting[selectedRoutingChannel] == 255) {
                            // Currently "--", change to 16
                            channelRouting[selectedRoutingChannel] = 15;
                        } else if (channelRouting[selectedRoutingChannel] > 0) {
                            // Decrement channel
                            channelRouting[selectedRoutingChannel]--;
                        } else {
                            // At 1, wrap to "--"
                            channelRouting[selectedRoutingChannel] = 255;
                        }
                        // Note: Routing is applied when OK is pressed to deactivate
                        break;

                    default:
                        break;
                }
            } else {
                // Navigate menu left
                currentRoutingOption = (RoutingMenuOption)((currentRoutingOption - 1 + ROUTING_OPTION_COUNT) % ROUTING_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_OK:
            // If showing confirmation, handle Yes/No selection
            if (showingConfirmation) {
                if (confirmSelection) {
                    // User selected Yes - execute the action
                    FileEntry* entry = browser.getCurrentFile();
                    if (entry && !entry->isDirectory) {
                        if (pendingConfirmAction == CONFIRM_SAVE) {
                            if (saveTrackSettings(entry->filename)) {
                                display.showMessage("Settings", "Saved!");
                                delay(1000);
                            } else {
                                display.showError("Save Failed!");
                                delay(1000);
                            }
                        } else if (pendingConfirmAction == CONFIRM_DELETE) {
                            int result = deleteTrackSettings(entry->filename);
                            if (result == 1) {
                                // Deleted successfully
                                resetChannelSettingsToDefaults();
                                display.showMessage("Settings", "Deleted!");
                                delay(1000);
                            } else if (result == 0) {
                                // No file to delete
                                display.showMessage("No Settings", "to Delete");
                                delay(1000);
                            } else {
                                // Delete failed
                                display.showError("Delete Failed!");
                                delay(1000);
                            }
                        }
                    }
                }
                // Cancel confirmation (whether Yes or No)
                showingConfirmation = false;
                pendingConfirmAction = CONFIRM_NONE;
                confirmSelection = false;
                updateDisplay();
                break;
            }

            // Handle based on option
            switch (currentRoutingOption) {
                case ROUTING_OPTION_SAVE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_SAVE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                case ROUTING_OPTION_DELETE:
                    // Show confirmation dialog
                    showingConfirmation = true;
                    pendingConfirmAction = CONFIRM_DELETE;
                    confirmSelection = true;  // Default to Yes
                    updateDisplay();
                    break;

                default:
                    // For other options, toggle active state
                    if (currentRoutingOption == ROUTING_OPTION_ROUTE_TO) {
                        if (routingOptionActive) {
                            // Deactivating - apply the routing change
                            // Send All Notes Off to the OLD routing destination to prevent stuck notes
                            uint8_t outputChannel;
                            if (originalRouting == 255) {
                                // Was using original channel
                                outputChannel = selectedRoutingChannel + 1;
                            } else {
                                // Was routed to different channel
                                outputChannel = originalRouting + 1;
                            }
                            midiOut.sendControlChange(outputChannel, 123, 0);
                            delay(20);

                            // Now apply the new routing
                            {
                                ScopedMutex lock(&playerMutex);
                                player.setChannelRouting(channelRouting);
                            }

                            routingOptionActive = false;
                        } else {
                            // Activating - store the original routing value
                            originalRouting = channelRouting[selectedRoutingChannel];
                            routingOptionActive = true;
                        }
                    } else {
                        // For other options, just toggle
                        routingOptionActive = !routingOptionActive;
                    }
                    break;
            }
            updateDisplay();
            break;

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false;
                break;
            }
            // Cycle to MIDI settings
            currentRoutingOption = ROUTING_OPTION_CHANNEL;
            routingOptionActive = false;
            currentMode = APP_MODE_MIDI_SETTINGS;
            currentMidiOption = MIDI_OPTION_THRU;
            midiOptionActive = false;
            display.setMode(MODE_SETTINGS);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic from routing mode too
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

void handleMidiSettingsMode(Button btn) {
    switch (btn) {
        case BTN_RIGHT:
            if (midiOptionActive) {
                // Active - toggle/increase value
                switch (currentMidiOption) {
                    case MIDI_OPTION_THRU:
                        midiThruEnabled = !midiThruEnabled;
                        midiIn.setThruEnabled(midiThruEnabled);
                        // If enabling Thru, disable Keyboard
                        if (midiThruEnabled && midiKeyboardEnabled) {
                            midiKeyboardEnabled = false;
                            midiIn.setKeyboardEnabled(false);
                        }
                        break;

                    case MIDI_OPTION_KEYBOARD:
                        midiKeyboardEnabled = !midiKeyboardEnabled;
                        midiIn.setKeyboardEnabled(midiKeyboardEnabled);
                        // If enabling Keyboard, disable Thru
                        if (midiKeyboardEnabled && midiThruEnabled) {
                            midiThruEnabled = false;
                            midiIn.setThruEnabled(false);
                        }
                        break;

                    case MIDI_OPTION_KEYBOARD_CH:
                        midiKeyboardChannel = (midiKeyboardChannel % 16) + 1;
                        midiIn.setKeyboardChannel(midiKeyboardChannel);
                        break;

                    case MIDI_OPTION_KEYBOARD_VEL:
                        // Increase velocity (1-100)
                        if (midiKeyboardVelocity < 100) {
                            midiKeyboardVelocity++;
                            midiIn.setKeyboardVelocity(midiKeyboardVelocity);
                        }
                        break;

                    default:
                        break;
                }
                // Save settings after any change
                saveGlobalSettings();
            } else {
                // Navigate menu right
                currentMidiOption = (MidiSettingsOption)((currentMidiOption + 1) % MIDI_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_LEFT:
            if (midiOptionActive) {
                // Active - toggle/decrease value
                switch (currentMidiOption) {
                    case MIDI_OPTION_THRU:
                        midiThruEnabled = !midiThruEnabled;
                        midiIn.setThruEnabled(midiThruEnabled);
                        // If enabling Thru, disable Keyboard
                        if (midiThruEnabled && midiKeyboardEnabled) {
                            midiKeyboardEnabled = false;
                            midiIn.setKeyboardEnabled(false);
                        }
                        break;

                    case MIDI_OPTION_KEYBOARD:
                        midiKeyboardEnabled = !midiKeyboardEnabled;
                        midiIn.setKeyboardEnabled(midiKeyboardEnabled);
                        // If enabling Keyboard, disable Thru
                        if (midiKeyboardEnabled && midiThruEnabled) {
                            midiThruEnabled = false;
                            midiIn.setThruEnabled(false);
                        }
                        break;

                    case MIDI_OPTION_KEYBOARD_CH:
                        midiKeyboardChannel = ((midiKeyboardChannel - 2 + 16) % 16) + 1;
                        midiIn.setKeyboardChannel(midiKeyboardChannel);
                        break;

                    case MIDI_OPTION_KEYBOARD_VEL:
                        // Decrease velocity (1-100)
                        if (midiKeyboardVelocity > 1) {
                            midiKeyboardVelocity--;
                            midiIn.setKeyboardVelocity(midiKeyboardVelocity);
                        }
                        break;

                    default:
                        break;
                }
                // Save settings after any change
                saveGlobalSettings();
            } else {
                // Navigate menu left
                currentMidiOption = (MidiSettingsOption)((currentMidiOption - 1 + MIDI_OPTION_COUNT) % MIDI_OPTION_COUNT);
            }
            updateDisplay();
            break;

        case BTN_OK:
            // Toggle active state
            midiOptionActive = !midiOptionActive;
            updateDisplay();
            break;

        // BTN_STOP is handled globally - not here

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false; // Clear flag
                break; // Ignore this MODE press
            }
            // Cycle to Clock settings
            currentMidiOption = MIDI_OPTION_THRU;
            midiOptionActive = false;
            currentMode = APP_MODE_CLOCK_SETTINGS;
            display.setMode(MODE_SETTINGS);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic from MIDI settings too
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

void handleClockSettingsMode(Button btn) {
    switch (btn) {
        case BTN_RIGHT:
            if (clockOptionActive) {
                // Active - toggle clock enabled
                midiClockEnabled = !midiClockEnabled;
                player.setClockEnabled(midiClockEnabled);
                // Save settings after change
                saveGlobalSettings();
            }
            // No menu navigation needed - only one option
            updateDisplay();
            break;

        case BTN_LEFT:
            if (clockOptionActive) {
                // Active - toggle clock enabled
                midiClockEnabled = !midiClockEnabled;
                player.setClockEnabled(midiClockEnabled);
                // Save settings after change
                saveGlobalSettings();
            }
            // No menu navigation needed - only one option
            updateDisplay();
            break;

        case BTN_OK:
            // Toggle active state
            clockOptionActive = !clockOptionActive;
            updateDisplay();
            break;

        // BTN_STOP is handled globally - not here

        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false; // Clear flag
                break; // Ignore this MODE press
            }
            // Cycle to Visualizer
            clockOptionActive = false;
            currentMode = APP_MODE_VISUALIZER;
            display.setMode(MODE_SETTINGS);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic from Clock settings too
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

void handleVisualizerMode(Button btn) {
    switch (btn) {
        case BTN_MODE:
            // Check if we should ignore this release (after hold-jump)
            if (ignoreModeRelease) {
                ignoreModeRelease = false; // Clear flag
                break; // Ignore this MODE press
            }
            // Cycle back to Playback
            currentMode = APP_MODE_PLAY;
            display.setMode(MODE_PLAYBACK);
            updateDisplay();
            break;

        case BTN_PANIC:
            // Send MIDI panic from Visualizer too
            for (uint8_t ch = 1; ch <= 16; ch++) {
                midiOut.sendControlChange(ch, 123, 0); // All Notes Off
                midiOut.sendControlChange(ch, 120, 0); // All Sound Off
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// Visualizer Functions (simple GENaJam-Pi style for 16 channels)
// ============================================================================

void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel >= 16) return;

    // Scale velocity down to prevent maxing out too easily (70% of original)
    uint8_t scaledVelocity = (velocity * 7) / 10;

    // CRITICAL SECTION: Protect visualizer data from concurrent Core 0 reads
    // Hardware spin lock ensures atomic updates (< 1 microsecond)
    uint32_t save = spin_lock_blocking(visualizerSpinLock);

    // Store base velocity and mark channel as having active notes
    vizChannels[channel].velocity = scaledVelocity;
    vizChannels[channel].hasActiveNotes = true;

    // Apply expression to create final display value (expression modulates velocity)
    // Expression: 127 = full velocity, 64 = half velocity, 0 = silent
    uint16_t expressedVelocity = (scaledVelocity * vizChannels[channel].expression) / 127;
    channelActivity[channel] = (uint8_t)expressedVelocity;

    // Update peak if this expressed velocity is higher
    if (expressedVelocity > vizChannels[channel].peak) {
        vizChannels[channel].peak = expressedVelocity;
        vizChannels[channel].peakTime = millis();
        channelPeak[channel] = expressedVelocity;  // Update peak display directly too
    }

    spin_unlock(visualizerSpinLock, save);
    // END CRITICAL SECTION
}

void onNoteOff(uint8_t channel, uint8_t note) {
    if (channel >= 16) return;

    // CRITICAL SECTION: Protect visualizer data from concurrent Core 0 reads
    uint32_t save = spin_lock_blocking(visualizerSpinLock);

    // Clear velocity and active notes flag, but keep peak for hold/decay
    vizChannels[channel].velocity = 0;
    vizChannels[channel].hasActiveNotes = false;
    channelActivity[channel] = 0;

    spin_unlock(visualizerSpinLock, save);
    // END CRITICAL SECTION
}

void onControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    if (channel >= 16) return;

    // Handle Expression (CC11) - modulates the visualizer bar height
    if (cc == 11) {
        // CRITICAL SECTION: Protect visualizer data from concurrent Core 0 reads
        uint32_t save = spin_lock_blocking(visualizerSpinLock);

        vizChannels[channel].expression = value;

        // If there are active notes, recalculate the display value with new expression
        // This creates fluid bar movement when expression changes during sustained notes
        if (vizChannels[channel].hasActiveNotes && vizChannels[channel].velocity > 0) {
            // Recalculate expressed velocity with new expression value
            uint16_t expressedVelocity = (vizChannels[channel].velocity * value) / 127;
            channelActivity[channel] = (uint8_t)expressedVelocity;

            // Update peak if this new expressed velocity is higher
            if (expressedVelocity > vizChannels[channel].peak) {
                vizChannels[channel].peak = expressedVelocity;
                vizChannels[channel].peakTime = millis();
                channelPeak[channel] = expressedVelocity;
            }
        }

        spin_unlock(visualizerSpinLock, save);
        // END CRITICAL SECTION
    }
    // Could also handle Volume (CC7) similarly if desired
}

// Update visualizer bars with decay
void updateChannelLevels() {
    uint32_t now = millis();
    constexpr uint32_t PEAK_HOLD_MS = 800;
    constexpr uint8_t DECAY_RATE = 3;

    // CRITICAL SECTION: Protect visualizer data from concurrent Core 1 writes
    uint32_t save = spin_lock_blocking(visualizerSpinLock);

    for (uint8_t ch = 0; ch < 16; ch++) {
        // Update current activity with expression applied
        uint16_t expressedVelocity = (vizChannels[ch].velocity * vizChannels[ch].expression) / 127;
        channelActivity[ch] = (uint8_t)expressedVelocity;

        // Update peak with hold and decay
        if (vizChannels[ch].peak > 0) {
            // Check if peak hold time has expired
            if (now - vizChannels[ch].peakTime > PEAK_HOLD_MS) {
                // Decay peak
                if (vizChannels[ch].peak > DECAY_RATE) {
                    vizChannels[ch].peak -= DECAY_RATE;
                } else {
                    vizChannels[ch].peak = 0;
                }
            }
        }

        channelPeak[ch] = vizChannels[ch].peak;
    }

    spin_unlock(visualizerSpinLock, save);
    // END CRITICAL SECTION
}

void resetVisualizer() {
    // CRITICAL SECTION: Protect visualizer data from concurrent Core 1 writes
    uint32_t save = spin_lock_blocking(visualizerSpinLock);

    // Clear all visualizer state
    for (uint8_t ch = 0; ch < 16; ch++) {
        vizChannels[ch].velocity = 0;
        vizChannels[ch].expression = 127;  // Reset to full expression
        vizChannels[ch].peak = 0;
        vizChannels[ch].peakTime = 0;
        vizChannels[ch].hasActiveNotes = false;
        channelActivity[ch] = 0;
        channelPeak[ch] = 0;
    }

    spin_unlock(visualizerSpinLock, save);
    // END CRITICAL SECTION
}

void updateDisplay() {
    unsigned long updateDisplayStart = millis();

    // If showing confirmation dialog, override all other displays
    if (showingConfirmation) {
        const char* message = "";
        if (pendingConfirmAction == CONFIRM_SAVE) {
            message = "Save settings?";
        } else if (pendingConfirmAction == CONFIRM_DELETE) {
            message = "Delete settings?";
        }
        display.showConfirmation(message, confirmSelection);
        return;
    }

    switch (currentMode) {
        case APP_MODE_BROWSE:
            display.showFileBrowser(&browser);
            break;

        case APP_MODE_PLAY:
            {
                PlaybackInfo info;
                FileEntry* current = browser.getCurrentFile();
                if (current) {
                    strncpy(info.songName, current->filename, sizeof(info.songName) - 1);
                    info.songName[sizeof(info.songName) - 1] = '\0';
                } else {
                    strcpy(info.songName, "Unknown");
                }

                {
                    ScopedMutex lock(&playerMutex);
                    info.currentTime = player.getCurrentTimeMs();
                    info.totalTime = player.getTotalTimeMs();
                    info.targetBPM = targetBPM;  // Display user's target BPM

                    MidiFileInfo fileInfo = player.getFileInfo();
                    info.timeSignatureNum = fileInfo.numerator;
                    info.timeSignatureDen = fileInfo.denominator;

                    info.isPlaying = (player.getState() == STATE_PLAYING);
                    info.isPaused = (player.getState() == STATE_PAUSED);
                    info.channelMutes = player.getChannelMutes();
                }

                // Add menu state
                info.selectedOption = currentPlaybackOption;
                info.optionActive = playbackOptionActive;
                info.bpmEditingWhole = bpmEditingWhole;

                // Add track info
                info.currentTrack = browser.getCurrentIndex() + 1; // 1-based
                info.totalTracks = browser.getFileCount();

                // Add velocity scale
                info.velocityScale = velocityScale;

                // Add playback mode
                info.playbackMode = playbackMode;

                // Add SysEx count (for MT-32 indication)
                {
                    ScopedMutex lock(&playerMutex);
                    info.sysexCount = player.getParser().getSysexCount();
                }

                display.showPlayback(info);
            }
            break;

        case APP_MODE_SETTINGS:
            display.showSettings(0, "Tempo", "100%");
            break;

        case APP_MODE_CHANNEL_MENU:
        case APP_MODE_PROGRAM_MENU:
            {
                uint16_t channelMutes;
                {
                    ScopedMutex lock(&playerMutex);
                    channelMutes = player.getChannelMutes();
                }
                display.showChannelSettingsMenu(selectedChannel, channelMutes, channelSolos, channelPrograms, channelPan, channelVolume, channelTranspose, channelVelocity, currentChannelOption, channelOptionActive);
            }
            break;

        case APP_MODE_TRACK_SETTINGS:
            {
                display.showTrackSettingsMenu(targetBPM, useDefaultTempo, velocityScale, sysexEnabled, currentTrackOption, trackOptionActive, bpmEditingWhole);
            }
            break;

        case APP_MODE_ROUTING:
            display.showRoutingMenu(selectedRoutingChannel, channelRouting, currentRoutingOption, routingOptionActive);
            break;

        case APP_MODE_MIDI_SETTINGS:
            display.showMidiSettingsMenu(midiThruEnabled, midiKeyboardEnabled, midiKeyboardChannel, midiKeyboardVelocity, currentMidiOption, midiOptionActive);
            break;

        case APP_MODE_CLOCK_SETTINGS:
            display.showClockSettingsMenu(midiClockEnabled, clockOptionActive);
            break;

        case APP_MODE_VISUALIZER:
            {
                // Copy visualizer data under lock for thread-safe rendering
                // This ensures consistent snapshot even if Core 1 updates during render
                uint8_t localActivity[16];
                uint8_t localPeak[16];

                uint32_t save = spin_lock_blocking(visualizerSpinLock);
                memcpy(localActivity, channelActivity, 16);
                memcpy(localPeak, channelPeak, 16);
                spin_unlock(visualizerSpinLock, save);

                // Render using local copies (outside lock - slow I2C display updates)
                display.showVisualizer(localActivity, localPeak);
            }
            break;
    }

    // Display update timing disabled - heap monitoring now tracks performance
}

void sendProgramChanges() {
    // Send program change messages for all channels that have been set (not 128)
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (channelPrograms[ch] < CHANNEL_PROGRAM_USE_MIDI_FILE) {
            // Only send if user has set a program (0-127)
            midiOut.sendProgramChange(ch + 1, channelPrograms[ch]); // Channels are 1-based in MIDI
            delay(MIDI_SETTLE_DELAY_MS);
        }
    }
}

void sendChannelVolumes() {
    // Send CC 7 (Volume) for all channels that have been set (not 255)
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (channelVolume[ch] < CHANNEL_VOLUME_USE_MIDI_FILE) {
            // Only send if user has set a volume (0-127)
            midiOut.sendControlChange(ch + 1, 7, channelVolume[ch]); // CC 7 = Volume
            delay(MIDI_SETTLE_DELAY_MS);
        }
    }
}

void sendChannelPan() {
    // Send CC 10 (Pan) for all channels that have been set (not 255)
    for (uint8_t ch = 0; ch < 16; ch++) {
        if (channelPan[ch] < CHANNEL_VOLUME_USE_MIDI_FILE) {
            // Only send if user has set a pan (0-127)
            midiOut.sendControlChange(ch + 1, 10, channelPan[ch]); // CC 10 = Pan
            delay(MIDI_SETTLE_DELAY_MS);
        }
    }
}

void buildConfigPath(const char* midiFilename, char* configPath, size_t configPathSize) {
    // Extract just the filename (without path) from the MIDI filename
    const char* baseFilename = strrchr(midiFilename, '/');
    if (baseFilename) {
        baseFilename++; // Skip the '/'
    } else {
        baseFilename = midiFilename; // No path separator, use whole string
    }

    // Build path: /MIDI/config/filename.cfg
    snprintf(configPath, configPathSize, "/MIDI/config/%s", baseFilename);

    // Find .mid extension and replace with .cfg
    char* ext = strrchr(configPath, '.');
    if (ext && (strcasecmp(ext, ".mid") == 0 || strcasecmp(ext, ".midi") == 0)) {
        strcpy(ext, ".cfg");
    } else {
        strcat(configPath, ".cfg");
    }
}

bool saveTrackSettings(const char* midiFilename) {
    // Build config file path
    char settingsFilename[128];
    buildConfigPath(midiFilename, settingsFilename, sizeof(settingsFilename));

    // Ensure /MIDI/config directory exists
    if (!sd.exists("/MIDI/config")) {
        if (!sd.mkdir("/MIDI/config")) {
            return false;
        }
    }

    // Open file for writing with RAII
    FatFile settingsFileObj;
    ScopedFile settingsFile(&settingsFileObj);
    if (!settingsFile.open(settingsFilename, O_WRONLY | O_CREAT | O_TRUNC)) {
        return false;
    }

    // Write settings in simple text format using write()
    char line[256];

    settingsFileObj.write("[MIDI_SETTINGS_V1]\n");

    // Write channel mutes (as bitmask)
    sprintf(line, "MUTES=%u\n", player.getChannelMutes());
    settingsFileObj.write(line);

    // Write channel programs
    strcpy(line, "PROGRAMS=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%u", channelPrograms[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write channel volumes
    strcpy(line, "VOLUMES=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%u", channelVolume[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write channel pan
    strcpy(line, "PAN=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%u", channelPan[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write channel transpose
    strcpy(line, "TRANSPOSE=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%d", channelTranspose[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write channel routing
    strcpy(line, "ROUTING=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%u", channelRouting[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write channel velocity scales
    strcpy(line, "CH_VELOCITY=");
    for (int i = 0; i < 16; i++) {
        char num[8];
        sprintf(num, "%u", channelVelocity[i]);
        strcat(line, num);
        if (i < 15) strcat(line, ",");
    }
    strcat(line, "\n");
    settingsFileObj.write(line);

    // Write global velocity scale
    sprintf(line, "VELOCITY_SCALE=%u\n", velocityScale);
    settingsFileObj.write(line);

    // Write target BPM (user-facing BPM in hundredths: 12050 = 120.50 BPM)
    sprintf(line, "TARGET_BPM=%u\n", targetBPM);
    settingsFileObj.write(line);

    // Write use target BPM flag
    sprintf(line, "USE_TARGET_BPM=%u\n", useTargetBPM ? 1 : 0);
    settingsFileObj.write(line);

    // Write channel solos (as bitmask)
    sprintf(line, "SOLOS=%u\n", channelSolos);
    settingsFileObj.write(line);

    // Write SysEx enabled/disabled
    sprintf(line, "SYSEX_ENABLED=%u\n", sysexEnabled ? 1 : 0);
    settingsFileObj.write(line);

    // File automatically closed by ScopedFile destructor
    return true;
}

void resetChannelSettingsToDefaults() {
    // Reset all channel settings to defaults (use MIDI file)
    {
        ScopedMutex lock(&playerMutex);
        for (uint8_t ch = 0; ch < 16; ch++) {
            channelPrograms[ch] = CHANNEL_PROGRAM_USE_MIDI_FILE;
            channelVolume[ch] = CHANNEL_VOLUME_USE_MIDI_FILE;
            channelPan[ch] = CHANNEL_PAN_USE_MIDI_FILE;
            channelTranspose[ch] = 0;   // No transpose by default
            channelVelocity[ch] = 0;    // Use global velocity scale (no per-channel override)
            channelRouting[ch] = 255;   // Use original channel (no routing)
            player.unmuteChannel(ch);   // Unmute all channels

            // Send All Sound Off to reset the channel
            midiOut.sendControlChange(ch + 1, 120, 0);  // All Sound Off
        }

        // Tell player to use MIDI file defaults (no overrides)
        player.setChannelPrograms(channelPrograms);
        player.setChannelVolumes(channelVolume);
        player.setChannelPan(channelPan);
        player.setChannelTranspose(channelTranspose);
        player.setChannelVelocityScales(channelVelocity);
        player.setChannelRouting(channelRouting);

        // Reset global velocity scale to default
        velocityScale = DEFAULT_VELOCITY_SCALE;
        velocityScaleDefault = DEFAULT_VELOCITY_SCALE;
        player.setVelocityScale(velocityScale);

        // Reset SysEx to enabled (default)
        sysexEnabled = true;
        player.setSysexEnabled(sysexEnabled);
    }

    // Clear all solos
    channelSolos = 0;

    // Reset target BPM flag (will use file's default tempo)
    useTargetBPM = false;
    useDefaultTempo = false;
    savedConfigBPM = 0;  // Clear saved config BPM
}

bool loadTrackSettings(const char* midiFilename) {
    // Reset to defaults first (will be overridden if .cfg file exists)
    resetChannelSettingsToDefaults();

    // Build config file path
    char settingsFilename[128];
    buildConfigPath(midiFilename, settingsFilename, sizeof(settingsFilename));

    // Check if settings file exists with RAII
    FatFile settingsFileObj;
    ScopedFile settingsFile(&settingsFileObj);
    if (!settingsFile.open(settingsFilename, O_RDONLY)) {
        return false;
    }

    // Read and parse settings
    char line[256];
    int lineNum = 0;

    while (settingsFileObj.available()) {
        int len = settingsFileObj.fgets(line, sizeof(line));
        if (len <= 0) break;

        // Remove newline
        if (line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';

        if (strncmp(line, "MUTES=", 6) == 0) {
            uint16_t mutes = atoi(line + 6);
            for (int ch = 0; ch < 16; ch++) {
                if (mutes & (1 << ch)) {
                    player.muteChannel(ch);
                } else {
                    player.unmuteChannel(ch);
                }
            }
        } else if (strncmp(line, "PROGRAMS=", 9) == 0) {
            char* token = strtok(line + 9, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelPrograms[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "VOLUMES=", 8) == 0) {
            char* token = strtok(line + 8, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelVolume[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "PAN=", 4) == 0) {
            char* token = strtok(line + 4, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelPan[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "TRANSPOSE=", 10) == 0) {
            char* token = strtok(line + 10, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelTranspose[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "ROUTING=", 8) == 0) {
            char* token = strtok(line + 8, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelRouting[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "CH_VELOCITY=", 12) == 0) {
            char* token = strtok(line + 12, ",");
            for (int i = 0; i < 16 && token; i++) {
                channelVelocity[i] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strncmp(line, "VELOCITY_SCALE=", 15) == 0) {
            velocityScale = atoi(line + 15);
            if (velocityScale < MIN_VELOCITY_SCALE) velocityScale = MIN_VELOCITY_SCALE;
            if (velocityScale > MAX_VELOCITY_SCALE) velocityScale = MAX_VELOCITY_SCALE;
            velocityScaleDefault = velocityScale; // Remember loaded value as default
            {
                ScopedMutex lock(&playerMutex);
                player.setVelocityScale(velocityScale);
            }
        } else if (strncmp(line, "TARGET_BPM=", 11) == 0) {
            targetBPM = atoi(line + 11);
            if (targetBPM < MIN_TARGET_BPM) targetBPM = MIN_TARGET_BPM;
            if (targetBPM > MAX_TARGET_BPM) targetBPM = MAX_TARGET_BPM;
            // Save this as the config's BPM for reset functionality
            savedConfigBPM = targetBPM;
            // NOTE: Don't call setTargetBPM() here - file isn't loaded yet!
            // It will be applied in loadFileOnly() after the file is loaded
        } else if (strncmp(line, "USE_TARGET_BPM=", 15) == 0) {
            useTargetBPM = (atoi(line + 15) != 0);
        } else if (strncmp(line, "TEMPO_PERCENT=", 14) == 0) {
            // Backward compatibility: Old config files used TEMPO_PERCENT
            // For now, we'll ignore old TEMPO_PERCENT values and use file's default BPM
            // User can re-save config to migrate to new TARGET_BPM format
        } else if (strncmp(line, "SOLOS=", 6) == 0) {
            channelSolos = atoi(line + 6);
            applySoloLogic();  // Apply solo logic after loading
        } else if (strncmp(line, "SYSEX_ENABLED=", 14) == 0) {
            sysexEnabled = (atoi(line + 14) != 0);
        }
    }

    // File automatically closed by ScopedFile destructor

    // Tell player about the loaded settings so it can filter MIDI file messages
    {
        ScopedMutex lock(&playerMutex);
        player.setChannelPrograms(channelPrograms);
        player.setChannelVolumes(channelVolume);
        player.setChannelPan(channelPan);
        player.setChannelTranspose(channelTranspose);
        player.setChannelVelocityScales(channelVelocity);
        player.setChannelRouting(channelRouting);
        player.setSysexEnabled(sysexEnabled);
    }

    // Send loaded settings to MIDI output immediately
    sendProgramChanges();
    sendChannelVolumes();
    sendChannelPan();

    return true;
}

int deleteTrackSettings(const char* midiFilename) {
    // Build config file path
    char settingsFilename[128];
    buildConfigPath(midiFilename, settingsFilename, sizeof(settingsFilename));

    // Check if file exists first
    if (!sd.exists(settingsFilename)) {
        return 0; // Return 0 = no file to delete
    }

    if (!sd.remove(settingsFilename)) {
        return -1; // Return -1 = delete failed
    }

    return 1; // Return 1 = deleted successfully
}

bool saveGlobalSettings() {
    // Save global settings to /settings.cfg
    const char* settingsFilename = "/settings.cfg";

    // Open file for writing with RAII
    FatFile settingsFileObj;
    ScopedFile settingsFile(&settingsFileObj);
    if (!settingsFile.open(settingsFilename, O_WRONLY | O_CREAT | O_TRUNC)) {
        return false;
    }

    // Write settings in simple text format
    char line[128];

    settingsFileObj.write("[GLOBAL_SETTINGS_V1]\n");

    // Write MIDI IN settings
    sprintf(line, "MIDI_THRU=%d\n", midiThruEnabled ? 1 : 0);
    settingsFileObj.write(line);

    sprintf(line, "MIDI_KEYBOARD=%d\n", midiKeyboardEnabled ? 1 : 0);
    settingsFileObj.write(line);

    sprintf(line, "MIDI_KEYBOARD_CH=%u\n", midiKeyboardChannel);
    settingsFileObj.write(line);

    sprintf(line, "MIDI_KEYBOARD_VEL=%u\n", midiKeyboardVelocity);
    settingsFileObj.write(line);

    // Write MIDI Clock settings
    sprintf(line, "MIDI_CLOCK=%d\n", midiClockEnabled ? 1 : 0);
    settingsFileObj.write(line);

    // File automatically closed by ScopedFile destructor
    return true;
}

bool loadGlobalSettings() {
    // Load global settings from /settings.cfg
    const char* settingsFilename = "/settings.cfg";

    // Check if settings file exists with RAII
    FatFile settingsFileObj;
    ScopedFile settingsFile(&settingsFileObj);
    if (!settingsFile.open(settingsFilename, O_RDONLY)) {
        return false; // No settings file, use defaults
    }

    // Read and parse settings
    char line[128];

    while (settingsFileObj.available()) {
        int len = settingsFileObj.fgets(line, sizeof(line));
        if (len <= 0) break;

        // Remove newline
        if (line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';

        if (strncmp(line, "MIDI_THRU=", 10) == 0) {
            midiThruEnabled = (atoi(line + 10) != 0);
            midiIn.setThruEnabled(midiThruEnabled);
        } else if (strncmp(line, "MIDI_KEYBOARD=", 14) == 0) {
            midiKeyboardEnabled = (atoi(line + 14) != 0);
            midiIn.setKeyboardEnabled(midiKeyboardEnabled);
        } else if (strncmp(line, "MIDI_KEYBOARD_CH=", 17) == 0) {
            midiKeyboardChannel = atoi(line + 17);
            if (midiKeyboardChannel < 1) midiKeyboardChannel = 1;
            if (midiKeyboardChannel > 16) midiKeyboardChannel = 16;
            midiIn.setKeyboardChannel(midiKeyboardChannel);
        } else if (strncmp(line, "MIDI_KEYBOARD_VEL=", 18) == 0) {
            midiKeyboardVelocity = atoi(line + 18);
            if (midiKeyboardVelocity < 1) midiKeyboardVelocity = 1;
            if (midiKeyboardVelocity > 100) midiKeyboardVelocity = 100;
            midiIn.setKeyboardVelocity(midiKeyboardVelocity);
        } else if (strncmp(line, "MIDI_CLOCK=", 11) == 0) {
            midiClockEnabled = (atoi(line + 11) != 0);
            {
                ScopedMutex lock(&playerMutex);
                player.setClockEnabled(midiClockEnabled);
            }
        }
    }

    // File automatically closed by ScopedFile destructor
    return true;
}

bool loadFileOnly() {
    unsigned long startTime = millis();

    // CRITICAL: Prevent re-entrant calls during rapid file switching
    static bool isLoading = false;
    if (isLoading) {
        return false;
    }
    isLoading = true;

    // CRITICAL: Stop playback FIRST, outside mutex, so Core 1 can exit cleanly
    // Core 1 needs to complete any in-progress SD card reads before we close files
    {
        ScopedMutex lock(&playerMutex);
        player.stop(false);  // Stop without reset
    }

    // Wait for Core 1 to fully exit update() - critical for parser access safety
    // Must wait for any in-progress SD card reads to complete
    // 100ms is conservative but necessary for slow SD cards and large MIDI events
    delay(100);  // Increased from 50ms for better safety margin

    // Now safe to acquire mutex and close everything
    {
        ScopedMutex lock(&playerMutex);

        // Reset MIDI device to clean state (inside mutex for thread safety)
        // This stops all notes and resets controllers
        player.resetMidiDevice();

        // Unload player (Core 1 is done with it - no delay needed here)
        player.unloadFile();

        // Then close our file handle
        if (currentFile.isOpen()) {
            currentFile.close();
        }
    }

    // Extra delay to ensure SD card file handles are fully released
    delay(SD_CLOSE_DELAY_MS);

    // Get the current file entry
    FileEntry* entry = browser.getCurrentFile();
    if (!entry || entry->isDirectory) {
        isLoading = false;
        return false;
    }

    // Reset tempo to default for new file
    tempoPercent = DEFAULT_TEMPO_PERCENT;
    useDefaultTempo = false;  // Start with explicit tempo control

    // Try to load saved settings for this file (may override tempoPercent)
    loadTrackSettings(entry->filename);

    // Open the selected file (no mutex needed - player not accessing yet)
    if (!browser.openFile(&currentFile)) {
        display.showError("Failed to open!");
        delay(2000);
        isLoading = false;
        return false;
    }

    // Load the file into the player (protected with mutex)
    {
        ScopedMutex lock(&playerMutex);
        if (!player.loadFile(&currentFile)) {
            display.showError("Invalid MIDI!");
            currentFile.close();
            delay(2000);
            isLoading = false;
            return false;
        }
    }

    // CRITICAL: Scan for initial tempo BEFORE cache check
    // This ensures parser has correct tempo even if file length is cached
    // NOTE: This is safe without mutex because player is stopped and Core 1 has exited update()
    player.getParser().scanForInitialTempo();

    // Calculate and cache file length (first time only, uses cache afterward)
    // This is done OUTSIDE mutex to avoid blocking Core 1, but player must be fully stopped first
    // WARNING: For large files not in cache, this can take several seconds and will freeze UI!
    // Check if file is in cache to decide whether to show loading message
    const char* filename = strrchr(entry->fullPath, '/');
    filename = filename ? filename + 1 : entry->fullPath;
    FatFile tempFile;
    if (tempFile.open(entry->fullPath, O_RDONLY)) {
        uint16_t date, time;
        tempFile.getModifyDateTime(&date, &time);
        uint32_t modtime = ((uint32_t)date << 16) | time;
        tempFile.close();

        // Check if in cache - if not, show loading message
        if (getCachedFileLength(filename, modtime) == 0) {
            display.showMessage("Scanning", "MIDI file...");
            delay(100);  // Brief delay so message is visible
        }
    }

    calculateAndCacheFileLength(entry->fullPath, player.getParser());

    // NOW apply tempo and channel settings AFTER file scanning - with mutex protection
    // We temporarily set tempo to 100% so we can read the file's actual BPM
    {
        ScopedMutex lock(&playerMutex);
        player.setTempoPercent(DEFAULT_TEMPO_PERCENT);  // Set to 100% (1000 in tenth-percent)
        player.setChannelPrograms(channelPrograms);
    }

    // Store file's base BPM for all tempo calculations (always do this)
    // Read the current BPM from the player (at 100% tempo)
    uint16_t fileBPM;
    {
        ScopedMutex lock(&playerMutex);
        fileBPM = player.getCurrentBPM();
    }

    if (fileBPM > 0) {
        // Store file's base BPM in hundredths for tempo percent calculations
        fileBPM_hundredths = (uint32_t)fileBPM * 100;
    } else {
        // Fallback to 120.00 BPM
        fileBPM_hundredths = DEFAULT_TARGET_BPM;
    }

    // Initialize targetBPM from file's base tempo (if not already set by config)
    if (!useTargetBPM) {
        // Use file's base BPM as target
        targetBPM = fileBPM_hundredths;
        // Clamp to valid range
        if (targetBPM < MIN_TARGET_BPM) targetBPM = MIN_TARGET_BPM;
        if (targetBPM > MAX_TARGET_BPM) targetBPM = MAX_TARGET_BPM;
        // Keep tempo at file's default (100%)
        tempoPercent = DEFAULT_TEMPO_PERCENT;
    } else {
        // Config loaded a target BPM - apply it now that file is loaded
        setTargetBPM(targetBPM);
    }

    // File is loaded, player is stopped at position 0
    isLoading = false;

    return true;
}

bool loadAndPlayFile() {
    // Load the file
    if (!loadFileOnly()) {
        return false;
    }

    // Reset visualizer for new song
    resetVisualizer();

    // NOTE: MIDI device reset already done in loadFileOnly()

    // Send program changes to set instruments on all channels
    sendProgramChanges();

    // Send channel volumes
    sendChannelVolumes();

    // Send channel pan
    sendChannelPan();

    // Start playback
    player.play();

    return true;
}

void applySoloLogic() {
    // Apply solo logic:
    // If ANY channel has solo enabled, mute all NON-solo channels
    // If NO channels have solo enabled, use normal mute state

    bool anySoloActive = (channelSolos != 0);

    if (anySoloActive) {
        // Solo mode active: mute all channels that are NOT soloed
        for (uint8_t ch = 0; ch < 16; ch++) {
            bool isSolo = (channelSolos & (1 << ch)) != 0;
            if (isSolo) {
                // Unmute solo channels (unless explicitly muted)
                // Note: We don't touch explicitly muted channels
            } else {
                // Mute non-solo channels
                player.muteChannel(ch);
            }
        }
    } else {
        // No solos active: restore normal mute state
        // The mute state is already managed by player, so nothing to do here
    }
}

void handleTapTempo() {
    // Record tap timestamp
    uint32_t now = millis();

    // Timeout check: if more than 2000ms since last tap, reset buffer
    if (tapCount > 0 && (now - lastTapTime) > 2000) {
        tapCount = 0;
    }

    // Store timestamp in circular buffer
    tapTimes[tapCount % 4] = now;
    tapCount++;
    lastTapTime = now;

    // Need at least 2 taps to calculate BPM
    if (tapCount < 2) {
        return;
    }

    // Calculate BPM from intervals
    uint8_t tapsToUse = (tapCount < 4) ? tapCount : 4;
    uint32_t totalInterval = 0;
    uint8_t intervalCount = 0;

    // Calculate intervals between consecutive taps
    for (uint8_t i = 1; i < tapsToUse; i++) {
        uint8_t prevIdx = (tapCount - tapsToUse + i - 1) % 4;
        uint8_t currIdx = (tapCount - tapsToUse + i) % 4;
        uint32_t interval = tapTimes[currIdx] - tapTimes[prevIdx];

        // Validate interval (40 BPM = 1500ms, 300 BPM = 200ms)
        if (interval >= 200 && interval <= 1500) {
            totalInterval += interval;
            intervalCount++;
        }
    }

    if (intervalCount == 0) {
        return; // No valid intervals
    }

    // Average interval in milliseconds
    uint32_t avgInterval = totalInterval / intervalCount;

    // Convert to BPM: BPM = 60000 / interval_ms (in hundredths for precision)
    uint32_t bpm_hundredths = (60000UL * 100) / avgInterval;

    // Clamp to reasonable range (40.00 - 300.00 BPM)
    if (bpm_hundredths < MIN_TARGET_BPM) bpm_hundredths = MIN_TARGET_BPM;
    if (bpm_hundredths > MAX_TARGET_BPM) bpm_hundredths = MAX_TARGET_BPM;

    calculatedBPM = bpm_hundredths / 100;  // Store whole BPM for reference

    // Set target BPM (this handles all the tempo percent calculation internally)
    setTargetBPM(bpm_hundredths);
}

// Set target BPM (user-facing, in hundredths) and calculate tempo percent in background
void setTargetBPM(uint32_t bpmHundredths) {
    // Clamp to valid range
    if (bpmHundredths < MIN_TARGET_BPM) bpmHundredths = MIN_TARGET_BPM;
    if (bpmHundredths > MAX_TARGET_BPM) bpmHundredths = MAX_TARGET_BPM;

    targetBPM = bpmHundredths;
    useTargetBPM = true;
    useDefaultTempo = false;

    // Use stored file's base BPM (calculated once at 100% tempo during file load)
    if (fileBPM_hundredths == 0) {
        // No file loaded, just store target BPM
        return;
    }

    // Calculate tempo percent with tenth-percent precision: (target / file) * 1000
    // Use 64-bit to avoid overflow
    uint16_t percent = (uint16_t)(((uint64_t)bpmHundredths * 1000) / fileBPM_hundredths);

    // Clamp to player limits (50.0% - 200.0%)
    if (percent < MIN_TEMPO_PERCENT) percent = MIN_TEMPO_PERCENT;
    if (percent > MAX_TEMPO_PERCENT) percent = MAX_TEMPO_PERCENT;

    tempoPercent = percent;

    // Apply to player
    {
        ScopedMutex lock(&playerMutex);
        player.setTempoPercent(percent);
    }
}

// ============================================================================
// FILE LENGTH CACHE SYSTEM
// Caches calculated file lengths to avoid expensive rescanning
// Max 500 entries with FIFO eviction when full
// ============================================================================

#define MAX_CACHE_ENTRIES 500
#define CACHE_FILE_PATH "/.cache/cache"
#define CACHE_VERSION 3  // Increment this when cache format changes or calculation logic improves

struct FileLengthCacheEntry {
    char filename[64];
    uint32_t modtime;        // File modification time (unix timestamp)
    uint32_t lengthTicks;
    uint16_t sysexCount;     // Number of SysEx messages (for MT-32 detection)
};

static FileLengthCacheEntry lengthCache[MAX_CACHE_ENTRIES];
static uint16_t cacheSize = 0;
static bool cacheLoaded = false;

void loadLengthCache() {
    cacheSize = 0;
    cacheLoaded = true;

    FatFile cacheFile;
    if (!cacheFile.open(CACHE_FILE_PATH, O_RDONLY)) {
        return;  // Cache doesn't exist yet
    }

    // Read and validate version header
    char line[256];
    int len = cacheFile.fgets(line, sizeof(line));
    if (len > 0) {
        // Check if first line is version header
        if (strncmp(line, "VERSION,", 8) == 0) {
            uint32_t version = strtoul(line + 8, NULL, 10);
            if (version != CACHE_VERSION) {
                // Version mismatch - invalidate cache
                cacheFile.close();
                sd.remove(CACHE_FILE_PATH);  // Delete old cache
                return;
            }
        } else {
            // Old format without version - invalidate cache
            cacheFile.close();
            sd.remove(CACHE_FILE_PATH);  // Delete old cache
            return;
        }
    }

    // Read cache entries
    while (cacheFile.available() && cacheSize < MAX_CACHE_ENTRIES) {
        len = cacheFile.fgets(line, sizeof(line));
        if (len <= 0) break;

        // Parse: filename,modtime,length_ticks,sysex_count
        char* filename = strtok(line, ",");
        char* modtimeStr = strtok(NULL, ",");
        char* lengthStr = strtok(NULL, ",");
        char* sysexStr = strtok(NULL, ",\r\n");

        if (filename && modtimeStr && lengthStr && sysexStr) {
            strncpy(lengthCache[cacheSize].filename, filename, 63);
            lengthCache[cacheSize].filename[63] = '\0';
            lengthCache[cacheSize].modtime = strtoul(modtimeStr, NULL, 10);
            lengthCache[cacheSize].lengthTicks = strtoul(lengthStr, NULL, 10);
            lengthCache[cacheSize].sysexCount = (uint16_t)strtoul(sysexStr, NULL, 10);
            cacheSize++;
        }
    }

    cacheFile.close();
}

void saveLengthCache() {
    // Ensure cache directory exists
    if (!sd.exists("/.cache")) {
        sd.mkdir("/.cache");
    }

    FatFile cacheFile;
    if (!cacheFile.open(CACHE_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC)) {
        return;
    }

    // Write version header as first line
    char versionLine[32];
    sprintf(versionLine, "VERSION,%d\n", CACHE_VERSION);
    cacheFile.write(versionLine);

    // Write cache entries
    for (uint16_t i = 0; i < cacheSize; i++) {
        char line[256];
        sprintf(line, "%s,%lu,%lu,%u\n",
                lengthCache[i].filename,
                lengthCache[i].modtime,
                lengthCache[i].lengthTicks,
                lengthCache[i].sysexCount);
        cacheFile.write(line);
    }

    cacheFile.close();
}

uint32_t getCachedFileLength(const char* filename, uint32_t modtime, uint16_t* outSysexCount) {
    if (!cacheLoaded) {
        loadLengthCache();
    }

    // Search for matching entry
    for (uint16_t i = 0; i < cacheSize; i++) {
        if (strcmp(lengthCache[i].filename, filename) == 0) {
            // Found entry - check if file was modified
            if (lengthCache[i].modtime == modtime) {
                if (outSysexCount) {
                    *outSysexCount = lengthCache[i].sysexCount;
                }
                return lengthCache[i].lengthTicks;
            } else {
                return 0;  // File modified, cache stale
            }
        }
    }

    return 0;  // Not in cache
}

void cacheFileLength(const char* filename, uint32_t modtime, uint32_t lengthTicks, uint16_t sysexCount) {
    if (!cacheLoaded) {
        loadLengthCache();
    }

    // Check if entry exists - update it
    for (uint16_t i = 0; i < cacheSize; i++) {
        if (strcmp(lengthCache[i].filename, filename) == 0) {
            lengthCache[i].modtime = modtime;
            lengthCache[i].lengthTicks = lengthTicks;
            lengthCache[i].sysexCount = sysexCount;
            saveLengthCache();
            return;
        }
    }

    // Add new entry
    if (cacheSize < MAX_CACHE_ENTRIES) {
        strncpy(lengthCache[cacheSize].filename, filename, 63);
        lengthCache[cacheSize].filename[63] = '\0';
        lengthCache[cacheSize].modtime = modtime;
        lengthCache[cacheSize].lengthTicks = lengthTicks;
        lengthCache[cacheSize].sysexCount = sysexCount;
        cacheSize++;
        saveLengthCache();
    } else {
        // Cache full - evict oldest entry (entry 0)
        for (uint16_t i = 0; i < MAX_CACHE_ENTRIES - 1; i++) {
            lengthCache[i] = lengthCache[i + 1];
        }
        // Add new entry at end
        strncpy(lengthCache[MAX_CACHE_ENTRIES - 1].filename, filename, 63);
        lengthCache[MAX_CACHE_ENTRIES - 1].filename[63] = '\0';
        lengthCache[MAX_CACHE_ENTRIES - 1].modtime = modtime;
        lengthCache[MAX_CACHE_ENTRIES - 1].lengthTicks = lengthTicks;
        lengthCache[MAX_CACHE_ENTRIES - 1].sysexCount = sysexCount;
        saveLengthCache();
    }
}

void calculateAndCacheFileLength(const char* fullPath, MidiFileParser& fileParser) {
    // Extract just the filename from the full path for cache storage
    const char* filename = strrchr(fullPath, '/');
    if (filename) {
        filename++;  // Skip the '/'
    } else {
        filename = fullPath;  // No '/' found, use full string
    }

    // Get file modification time using full path
    FatFile file;
    if (!file.open(fullPath, O_RDONLY)) {
        return;
    }

    uint16_t date, time;
    file.getModifyDateTime(&date, &time);
    uint32_t modtime = ((uint32_t)date << 16) | time;  // Combine date/time into single value
    file.close();

    // Check cache first using just the filename
    uint16_t cachedSysexCount = 0;
    uint32_t cachedLength = getCachedFileLength(filename, modtime, &cachedSysexCount);
    if (cachedLength > 0) {
        fileParser.setFileLengthTicks(cachedLength);
        fileParser.setSysexCount(cachedSysexCount);
        return;
    }

    // Not in cache - calculate it (this can take several seconds for large files!)
    fileParser.calculateFileLengthNow();

    uint32_t lengthTicks = fileParser.getFileLengthTicks();
    uint16_t sysexCount = fileParser.getSysexCount();

    // Cache the result
    if (lengthTicks > 0) {
        cacheFileLength(filename, modtime, lengthTicks, sysexCount);
    }
}

// ============================================================================
// CORE 1 - Dedicated to MIDI processing for accurate timing
// ============================================================================

void setup1() {
    // Core 1 setup - nothing needed, Core 0 already initialized everything
    delay(100); // Wait for Core 0 to finish setup
}

void loop1() {
    // Core 1: Dedicated to MIDI timing-critical operations
    // This runs in parallel with Core 0 (UI, display, file I/O)

    // Update MIDI player - must be called frequently for accurate timing
    // Protected with mutex to prevent race conditions with Core 0
    {
        ScopedMutex lock(&playerMutex);
        player.update();
    } // Mutex automatically released here

    // Update MIDI input - process incoming MIDI messages
    midiIn.update();

    // No delay needed - MIDI timing is critical and these operations are very fast
    // The player.update() internally handles timing with micros()
}
