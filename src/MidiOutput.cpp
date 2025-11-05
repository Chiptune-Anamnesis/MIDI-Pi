#include "MidiOutput.h"
#include "pins.h"

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

MidiOutput::MidiOutput() {
    midi = &MIDI;
    noteOnCallback = nullptr;
    noteOffCallback = nullptr;
    controlChangeCallback = nullptr;
}

void MidiOutput::begin() {
    Serial1.setTX(MIDI_TX_PIN);
    Serial1.setRX(MIDI_RX_PIN);
    Serial1.begin(MIDI_BAUD_RATE);
    delay(100);

    midi->begin(MIDI_CHANNEL_OMNI);
    midi->turnThruOff();
}

void MidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16 || note > 127 || velocity > 127) return;
    midi->sendNoteOn(note, velocity, channel);

    // Notify note on callback for visualizer
    if (noteOnCallback && velocity > 0) {
        noteOnCallback(channel - 1, note, velocity); // Convert to 0-based channel
    }
}

void MidiOutput::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16 || note > 127 || velocity > 127) return;
    midi->sendNoteOff(note, velocity, channel);

    // Notify note off callback for visualizer
    if (noteOffCallback) {
        noteOffCallback(channel - 1, note); // Convert to 0-based channel
    }
}

void MidiOutput::sendControlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    if (channel < 1 || channel > 16 || cc > 127 || value > 127) return;
    midi->sendControlChange(cc, value, channel);

    // Notify control change callback for visualizer (CC7=Volume, CC11=Expression)
    if (controlChangeCallback) {
        controlChangeCallback(channel - 1, cc, value); // Convert to 0-based channel
    }
}

void MidiOutput::sendProgramChange(uint8_t channel, uint8_t program) {
    if (channel < 1 || channel > 16 || program > 127) {
        return;
    }
    midi->sendProgramChange(program, channel);
}

void MidiOutput::sendPitchBend(uint8_t channel, int16_t bend) {
    if (channel < 1 || channel > 16) return;
    midi->sendPitchBend(bend, channel);
}

void MidiOutput::sendAfterTouch(uint8_t channel, uint8_t pressure) {
    if (channel < 1 || channel > 16 || pressure > 127) return;
    midi->sendAfterTouch(pressure, channel);
}

void MidiOutput::sendPolyAfterTouch(uint8_t channel, uint8_t note, uint8_t pressure) {
    if (channel < 1 || channel > 16 || note > 127 || pressure > 127) return;
    midi->sendAfterTouch(pressure, channel, note);
}

void MidiOutput::sendSysEx(const uint8_t* data, uint16_t length) {
    midi->sendSysEx(length, data, true);

    // NO delay - modern USB MIDI and software synths don't need it
    // The 5ms delay was still causing 80ms+ blocking when combined with USB buffering
    // If you have real MT-32 hardware and experience glitches, add back a 1-2ms delay
}

void MidiOutput::sendClock() {
    midi->sendRealTime(midi::Clock);
}

void MidiOutput::sendStart() {
    midi->sendRealTime(midi::Start);
}

void MidiOutput::sendContinue() {
    midi->sendRealTime(midi::Continue);
}

void MidiOutput::sendStop() {
    midi->sendRealTime(midi::Stop);
}

void MidiOutput::allNotesOff() {
    for (uint8_t ch = 1; ch <= 16; ch++) {
        sendControlChange(ch, 123, 0); // All Notes Off
    }
}

void MidiOutput::panic() {
    for (uint8_t ch = 1; ch <= 16; ch++) {
        sendControlChange(ch, 120, 0); // All Sound Off
        sendControlChange(ch, 123, 0); // All Notes Off

        // Send note offs for all possible notes
        for (uint8_t note = 0; note < 128; note++) {
            sendNoteOff(ch, note, 0);
        }
    }
}

void MidiOutput::setNoteOnCallback(void (*callback)(uint8_t channel, uint8_t note, uint8_t velocity)) {
    noteOnCallback = callback;
}

void MidiOutput::setNoteOffCallback(void (*callback)(uint8_t channel, uint8_t note)) {
    noteOffCallback = callback;
}

void MidiOutput::setControlChangeCallback(void (*callback)(uint8_t channel, uint8_t cc, uint8_t value)) {
    controlChangeCallback = callback;
}
