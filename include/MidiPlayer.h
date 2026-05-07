#ifndef MIDI_PLAYER_H
#define MIDI_PLAYER_H

#include <Arduino.h>
#include "MidiFileParser.h"
#include "MidiOutput.h"
#include <SdFat.h>

enum PlayerState {
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_PAUSED
};

class MidiPlayer {
public:
    MidiPlayer(MidiOutput* output);
    ~MidiPlayer();

    // File operations
    bool loadFile(FatFile* file);
    void unloadFile();

    // Playback control
    void play();
    void pause();
    void stop(bool resetToBeginning = true);
    void update(); // Call this frequently in main loop

    // Navigation
    void fastForward(uint32_t milliseconds);
    void rewind(uint32_t milliseconds);
    void seek(uint32_t milliseconds);

    // Tempo control
    void setTempoPercent(uint16_t percent); // 50-200%
    uint16_t getTempoPercent() { return tempoPercent; }
    uint16_t getCurrentBPM();

    // Velocity control
    void setVelocityScale(uint8_t scale); // 1-100, where 50 = default MIDI velocity, 100 = max (127)
    uint8_t getVelocityScale() { return velocityScale; }

    // Channel mute/unmute
    void muteChannel(uint8_t channel);
    void unmuteChannel(uint8_t channel);
    void toggleMuteChannel(uint8_t channel);
    bool isChannelMuted(uint8_t channel);
    uint16_t getChannelMutes() { return channelMutes; }

    // Program change override (auto-detects based on non-zero programs)
    void setChannelPrograms(uint8_t* programs); // Set user's program settings for auto-override

    // Volume and Pan overrides
    void setChannelVolumes(uint8_t* volumes); // Set user's volume settings (0-127, 255 = use MIDI file)
    void setChannelPan(uint8_t* pan); // Set user's pan settings (0-127, 255 = use MIDI file)

    // Transpose override
    void setChannelTranspose(int8_t* transpose); // Set user's transpose settings in semitones (-24 to +24)

    // Per-channel velocity scale override
    void setChannelVelocityScales(uint8_t* velocities); // Set user's velocity scale settings (0 = use MIDI file, 1-200, 100 = normal)

    // Routing override
    void setChannelRouting(uint8_t* routing); // Set user's routing settings (255 = use original, 0-15 = route to that channel)

    // Status getters
    PlayerState getState() { return state; }
    uint32_t getCurrentTimeMs();
    uint32_t getTotalTimeMs();
    MidiFileInfo getFileInfo() { return parser.getFileInfo(); }
    bool hasReachedEnd() { return reachedEnd; } // True if file ended naturally
    MidiFileParser& getParser() { return parser; } // For cache system access

    // MIDI Clock and Transport
    void setClockEnabled(bool enabled) { clockEnabled = enabled; }
    bool getClockEnabled() { return clockEnabled; }

    // Slave mode (clock IN). Tick advancement is driven by incoming 0xF8
    // pulses instead of microsecondsPerTick + micros(). Each pulse advances
    // by ticksPerQuarter/24 file ticks (24 PPQN MIDI clock standard).
    // No PLL, no interpolation — like a drum machine.
    void setUseExternalClock(bool enabled) { useExternalClock = enabled; }
    bool getUseExternalClock() { return useExternalClock; }
    // Called from MidiInput on every incoming 0xF8 (Core 1, called from
    // loop1's MidiInput::update() — same core as player.update() so no mutex
    // is required). Records the pulse-to-pulse interval into a 24-slot ring
    // buffer (one quarter-note's worth at any tempo) and republishes the
    // averaged tempo as `measuredExternalBPM` for the display path to read.
    void onExternalClockTick();
    void resetExternalSyncTicks();
    uint32_t getExternalSyncTicks() { return externalSyncTicks; }
    // Position getters used to compute slave-mode display time externally
    // (without mutating the player's internal microsecondsPerTick).
    uint32_t getCurrentTicks() { return ticksElapsed; }
    // Smoothed master BPM measured from incoming 0xF8 intervals. 0.0 = not
    // yet measured (no pulses received since reset).
    float getMeasuredExternalBPM() { return measuredExternalBPM; }

    // Seamless Loop Mode (for LP1 - zero-gap looping)
    void setLoopMode(bool enabled) { loopMode = enabled; }
    bool getLoopMode() { return loopMode; }
    bool checkLoopRestarted() { if (loopRestarted) { loopRestarted = false; return true; } return false; }
    void setCleanLoop(bool enabled) { cleanLoop = enabled; }

    // SysEx Control
    void setSysexEnabled(bool enabled) { sysexEnabled = enabled; }
    bool getSysexEnabled() { return sysexEnabled; }

    // MIDI Device Control
    void resetMidiDevice(); // Comprehensive MIDI reset for song changes

private:
    MidiOutput* midiOut;
    MidiFileParser parser;
    FatFile* midiFile;  // Pointer to avoid duplicating file handles
    PlayerState state;

    // Timing
    uint32_t ticksElapsed;
    uint32_t lastUpdateMicros;
    uint32_t microsecondsPerTick;
    uint16_t tempoPercent; // 100 = normal speed

    // Channel control
    uint16_t channelMutes; // Bitmask for 16 channels
    uint8_t velocityScale; // 1-100, where 50 = default, 100 = max velocity
    uint8_t channelVelocities[16]; // Per-channel velocity 0-200% (100 = normal)
    uint8_t userChannelPrograms[16]; // User's program settings: 0-127 = override MIDI file, 128 = use MIDI file
    uint8_t userChannelVolumes[16]; // User's volume settings: 0-127 = override MIDI file, 255 = use MIDI file
    uint8_t userChannelPan[16]; // User's pan settings: 0-127 = override MIDI file, 255 = use MIDI file
    int8_t userChannelTranspose[16]; // User's transpose settings in semitones: -24 to +24
    uint8_t userChannelRouting[16]; // User's routing settings: 255 = use original channel, 0-15 = route to that channel

    // Event queue for timing
    MidiEvent nextEvent;
    bool eventReady;
    bool reachedEnd; // Track if file ended naturally (vs user stop)

    // MIDI Clock and Transport
    bool clockEnabled;
    uint32_t lastClockMicros;
    PlayerState lastTransportState; // Track state changes for transport messages

    // Seamless Loop Mode
    bool loopMode;              // True = seamless loop at end of file
    volatile bool loopRestarted; // Flag for Core 0 to detect loop restart
    bool cleanLoop;             // True = use targeted NOTE_OFFs instead of CC123 burst at loop restart
    bool loopSkipSetup;         // True = skip redundant setup events at tick 0 after loop restart

    // Active-note tracking. One bit per (channel, note) covering 16 ch × 128 notes
    // = 256 bytes. Set when a NOTE_ON (vel>0) is sent on the wire, cleared on
    // NOTE_OFF (or vel-0 NOTE_ON). Used by stopActiveNotes() so we can clear
    // hanging notes at the LP1 loop boundary with minimal UART traffic
    // (3 bytes per active note instead of the 48-byte all-channels CC123 burst).
    uint8_t activeNotes[16][16];
    inline void noteActiveSet(uint8_t channel, uint8_t note) {
        if (channel < 16 && note < 128)
            activeNotes[channel][note >> 3] |= (uint8_t)(1 << (note & 7));
    }
    inline void noteActiveClear(uint8_t channel, uint8_t note) {
        if (channel < 16 && note < 128)
            activeNotes[channel][note >> 3] &= (uint8_t)~(1 << (note & 7));
    }
    void stopActiveNotes();        // emit NOTE_OFFs for tracked-active notes only
    void clearActiveNoteTracking(); // zero the bitmap (call after CC123 / panic)

    // Slave mode (clock IN)
    volatile bool useExternalClock;
    volatile uint32_t externalSyncTicks;  // count of 0xF8 pulses since last reset

    // Smoothed master-tempo measurement: ring buffer of the last 24 pulse-to-
    // pulse intervals (1 quarter note's worth at any tempo) → averaged into
    // measuredExternalBPM. All members written only on Core 1 in
    // onExternalClockTick(); read from Core 0 in the display path.
    static constexpr uint8_t SYNC_INTERVAL_BUFFER_SIZE = 24;
    volatile uint32_t lastSyncTickMicros;
    volatile uint32_t syncIntervals[SYNC_INTERVAL_BUFFER_SIZE];
    volatile uint32_t syncIntervalSum;
    volatile uint8_t  syncIntervalIdx;
    volatile uint8_t  syncIntervalCount;
    volatile float    measuredExternalBPM;

    // SysEx Control
    bool sysexEnabled; // True = send SysEx messages, False = filter them out

    // Helper functions
    void calculateMicrosecondsPerTick();
    void sendMidiEvent(const MidiEvent& event);
    void stopAllNotes();
    uint32_t ticksToMilliseconds(uint32_t ticks);
    uint32_t millisecondsToTicks(uint32_t ms);
};

#endif // MIDI_PLAYER_H
