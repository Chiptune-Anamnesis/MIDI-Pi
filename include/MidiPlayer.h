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
