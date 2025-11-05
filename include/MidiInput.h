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

private:
    MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial>>* midiIn;
    MidiOutput* midiOut;

    // Settings
    bool thruEnabled;           // MIDI Thru: pass all MIDI IN to MIDI OUT
    bool keyboardEnabled;       // MIDI Keyboard mode: only send on specific channel
    uint8_t keyboardChannel;    // Channel for keyboard mode (1-16)
    uint8_t keyboardVelocity;   // Velocity scale for keyboard mode (1-100, 50=default)

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
