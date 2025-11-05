#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include "FileBrowser.h"

enum DisplayMode {
    MODE_FILE_BROWSER,
    MODE_PLAYBACK,
    MODE_SETTINGS,
    MODE_CHANNEL_MENU
};

// Playback mode options
enum PlaybackMode {
    PLAYBACK_SINGLE = 0,
    PLAYBACK_AUTO_NEXT = 1,
    PLAYBACK_LOOP_ONE = 2,
    PLAYBACK_LOOP_ALL = 3
};

// Menu options in desired rotation order
enum PlaybackMenuOption {
    MENU_TRACK = 0,
    MENU_BPM = 1,
    MENU_TAP = 2,
    MENU_MODE = 3,
    MENU_TIME = 4,
    MENU_PREV = 5,
    MENU_NEXT = 6,
    MENU_COUNT = 7
};

struct PlaybackInfo {
    char songName[64];
    uint32_t currentTime;    // in milliseconds
    uint32_t totalTime;      // in milliseconds
    uint32_t targetBPM;      // Target BPM in hundredths (12050 = 120.50 BPM)
    uint8_t timeSignatureNum;
    uint8_t timeSignatureDen;
    bool isPlaying;
    bool isPaused;
    uint16_t channelMutes;   // Bitmask for 16 channels
    PlaybackMenuOption selectedOption;
    bool optionActive;       // Whether the option is being edited
    bool bpmEditingWhole;    // True = editing whole number, False = editing decimal
    uint16_t currentTrack;   // Current track number (1-based)
    uint16_t totalTracks;    // Total number of tracks
    PlaybackMode playbackMode; // Playback mode
    uint8_t velocityScale;   // Velocity scale (1-100)
    uint16_t sysexCount;     // Number of SysEx messages (for MT-32 indication)
};

class DisplayManager {
public:
    DisplayManager();
    bool begin();
    void update();

    // Display modes
    void setMode(DisplayMode mode);
    DisplayMode getMode() { return currentMode; }

    // File browser display
    void showFileBrowser(FileBrowser* browser);

    // Playback display
    void showPlayback(const PlaybackInfo& info);

    // Settings display
    void showSettings(uint16_t settingIndex, const char* label, const char* value);

    // Channel menu displays
    void showChannelMenu(uint8_t selectedChannel, uint16_t channelMutes);
    void showProgramMenu(uint8_t selectedChannel, uint8_t* channelPrograms);
    void showChannelSettingsMenu(uint8_t selectedChannel, uint16_t channelMutes, uint16_t channelSolos, uint8_t* channelPrograms, uint8_t* channelPan, uint8_t* channelVolume, int8_t* channelTranspose, uint8_t* channelVelocity, uint8_t currentOption, bool optionActive);

    // Track Settings menu display
    void showTrackSettingsMenu(uint32_t targetBPM, bool useDefaultTempo, uint8_t velocityScale, bool sysexEnabled, uint8_t currentOption, bool optionActive, bool bpmEditingWhole);

    // MIDI Settings menu display
    void showMidiSettingsMenu(bool thruEnabled, bool keyboardEnabled, uint8_t keyboardChannel, uint8_t keyboardVelocity, uint8_t currentOption, bool optionActive);

    // Clock Settings menu display
    void showClockSettingsMenu(bool clockEnabled, bool optionActive);

    // Routing menu display
    void showRoutingMenu(uint8_t selectedChannel, uint8_t* channelRouting, uint8_t currentOption, bool optionActive);

    // Visualizer display
    void showVisualizer(uint8_t* channelActivity, uint8_t* channelPeak);

    // Utility displays
    void showMessage(const char* line1, const char* line2 = nullptr);
    void showError(const char* error);
    void showConfirmation(const char* message, bool yesSelected);
    void clear();

private:
    Adafruit_SSD1306 display;
    DisplayMode currentMode;

    // Scrolling text state
    int16_t scrollOffset;
    unsigned long lastScrollTime;
    static const uint16_t SCROLL_DELAY = 200; // ms between scroll updates

    // Visualizer bubble animation state (2 bubbles per channel)
    struct Bubble {
        float y;        // Y position (0-31, floating point for smooth animation)
        float speed;    // Rise speed in pixels per update
    };
    Bubble bubbles[16][2];  // 16 channels, 2 bubbles each
    unsigned long lastBubbleUpdate;
    static const uint16_t BUBBLE_UPDATE_DELAY = 50; // ms between bubble updates

    // Helper functions
    void drawProgressBar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress);
    void formatTime(uint32_t milliseconds, char* buffer);
    void drawScrollingText(const char* text, int16_t y, int16_t maxWidth);
    void drawPlayIcon(int16_t x, int16_t y, bool highlighted);
    void drawPauseIcon(int16_t x, int16_t y, bool highlighted);
    void drawStopIcon(int16_t x, int16_t y, bool highlighted);
};

#endif // DISPLAY_MANAGER_H
