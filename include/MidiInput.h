#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <Arduino.h>
#include <MIDI.h>
#include "MidiOutput.h"

class MidiInput {
public:
    MidiInput(MidiOutput* output);
    void begin();
    void update();

    // Settings
    void setThruEnabled(bool enabled) { thruEnabled = enabled; }
    bool getThruEnabled() { return thruEnabled; }

    void setKeyboardEnabled(bool enabled) { keyboardEnabled = enabled; }
    bool getKeyboardEnabled() { return keyboardEnabled; }

    void setKeyboardChannel(uint8_t channel) {
        if (channel >= 1 && channel <= 16) {
            keyboardChannel = channel;
        }
    }
    uint8_t getKeyboardChannel() { return keyboardChannel; }

    void setKeyboardVelocity(uint8_t velocity) {
        if (velocity >= 1 && velocity <= 100) {
            keyboardVelocity = velocity;
        }
    }
    uint8_t getKeyboardVelocity() { return keyboardVelocity; }

    // Remote control settings (Bank+PC file selection, transport messages)
    void setRemoteEnabled(bool e) { remoteEnabled = e; }
    bool getRemoteEnabled() { return remoteEnabled; }
    void setRemoteAutoPlay(bool e) { remoteAutoPlay = e; }
    bool getRemoteAutoPlay() { return remoteAutoPlay; }
    void setRemoteTransportEnabled(bool e) { remoteTransportEnabled = e; }
    bool getRemoteTransportEnabled() { return remoteTransportEnabled; }
    void setRemoteChannel(uint8_t ch) { if (ch <= 16) remoteChannel = ch; }
    uint8_t getRemoteChannel() { return remoteChannel; }

    // Cross-core request consumers (called from Core 0 in main loop)
    bool consumeFileLoadRequest(uint16_t& outIdx);
    bool consumeTransportStart();
    bool consumeTransportStop();
    bool consumeTransportContinue();

private:
    MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial>>* midiIn;
    MidiOutput* midiOut;

    // Settings
    bool thruEnabled;           // MIDI Thru: pass all MIDI IN to MIDI OUT
    bool keyboardEnabled;       // MIDI Keyboard mode: only send on specific channel
    uint8_t keyboardChannel;    // Channel for keyboard mode (1-16)
    uint8_t keyboardVelocity;   // Velocity scale for keyboard mode (1-100, 50=default)

    // Remote control state
    bool remoteEnabled;             // Master enable for Bank+PC file selection
    bool remoteAutoPlay;            // Auto-play file on Bank+PC selection
    bool remoteTransportEnabled;    // Honor MIDI Start/Stop/Continue
    uint8_t remoteChannel;          // 0=OMNI, 1-16=specific channel
    uint8_t lastBankMSB;            // Updated on CC0, used on next PC

    // Cross-core request flags (Core 1 sets, Core 0 consumes)
    volatile bool fileLoadRequested;
    volatile uint16_t pendingFileIndex;
    volatile bool transportStartRequested;
    volatile bool transportStopRequested;
    volatile bool transportContinueRequested;

    // MIDI message handlers
    void handleNoteOn(byte channel, byte note, byte velocity);
    void handleNoteOff(byte channel, byte note, byte velocity);
    void handleControlChange(byte channel, byte controller, byte value);
    void handleProgramChange(byte channel, byte program);
    void handlePitchBend(byte channel, int bend);
    void handleAfterTouch(byte channel, byte pressure);
    void handlePolyAfterTouch(byte channel, byte note, byte pressure);
};

#endif // MIDI_INPUT_H
