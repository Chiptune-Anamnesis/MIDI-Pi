#ifndef MIDI_OUTPUT_H
#define MIDI_OUTPUT_H

#include <Arduino.h>
#include <MIDI.h>

class MidiOutput {
public:
    MidiOutput();
    void begin();

    // MIDI message sending
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    void sendControlChange(uint8_t channel, uint8_t cc, uint8_t value);
    void sendProgramChange(uint8_t channel, uint8_t program);
    void sendPitchBend(uint8_t channel, int16_t bend);
    void sendAfterTouch(uint8_t channel, uint8_t pressure);
    void sendPolyAfterTouch(uint8_t channel, uint8_t note, uint8_t pressure);
    void sendSysEx(const uint8_t* data, uint16_t length);

    // MIDI Clock and Transport messages
    void sendClock();      // 0xF8 - MIDI Clock tick (24 per quarter note)
    void sendStart();      // 0xFA - Start playback
    void sendContinue();   // 0xFB - Continue from pause
    void sendStop();       // 0xFC - Stop playback

    // Utility functions
    void allNotesOff();
    void panic();

    // Visualizer support
    void setNoteOnCallback(void (*callback)(uint8_t channel, uint8_t note, uint8_t velocity));
    void setNoteOffCallback(void (*callback)(uint8_t channel, uint8_t note));
    void setControlChangeCallback(void (*callback)(uint8_t channel, uint8_t cc, uint8_t value));

private:
    MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial>>* midi;
    void (*noteOnCallback)(uint8_t channel, uint8_t note, uint8_t velocity);
    void (*noteOffCallback)(uint8_t channel, uint8_t note);
    void (*controlChangeCallback)(uint8_t channel, uint8_t cc, uint8_t value);
};

#endif // MIDI_OUTPUT_H
