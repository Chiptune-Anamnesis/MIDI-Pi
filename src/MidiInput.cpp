#include "MidiInput.h"
#include "pins.h"

// Use the same MIDI instance as MidiOutput (declared in MidiOutput.cpp)
extern MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial>> MIDI;

MidiInput::MidiInput(MidiOutput* output) {
    midiIn = &MIDI;
    midiOut = output;
    thruEnabled = false;
    keyboardEnabled = false;
    keyboardChannel = 1;  // Default to channel 1
    keyboardVelocity = 50;  // Default to 50 (normal velocity)
}

void MidiInput::begin() {
    // Nothing to do - MIDI library already initialized by MidiOutput
    // We share the same Serial1/UART0 instance
}

void MidiInput::update() {
    // Read and process MIDI messages
    if (midiIn->read()) {
        midi::MidiType type = midiIn->getType();
        byte channel = midiIn->getChannel();
        byte data1 = midiIn->getData1();
        byte data2 = midiIn->getData2();

        // Handle based on mode
        if (thruEnabled) {
            // MIDI Thru mode - pass everything through
            switch (type) {
                case midi::NoteOn:
                    midiOut->sendNoteOn(channel, data1, data2);
                    break;
                case midi::NoteOff:
                    midiOut->sendNoteOff(channel, data1, data2);
                    break;
                case midi::ControlChange:
                    midiOut->sendControlChange(channel, data1, data2);
                    break;
                case midi::ProgramChange:
                    midiOut->sendProgramChange(channel, data1);
                    break;
                case midi::PitchBend:
                    {
                        int bend = (data2 << 7) | data1;
                        midiOut->sendPitchBend(channel, bend - 8192);  // Convert to signed
                    }
                    break;
                case midi::AfterTouchChannel:
                    midiOut->sendAfterTouch(channel, data1);
                    break;
                case midi::AfterTouchPoly:
                    midiOut->sendPolyAfterTouch(channel, data1, data2);
                    break;
                default:
                    break;
            }
        } else if (keyboardEnabled) {
            // MIDI Keyboard mode - only send on specified channel
            switch (type) {
                case midi::NoteOn:
                    {
                        // Apply velocity scaling (keyboardVelocity is 1-100, 50 = normal)
                        uint16_t scaledVelocity = ((uint16_t)data2 * keyboardVelocity) / 50;
                        if (scaledVelocity > 127) scaledVelocity = 127;
                        midiOut->sendNoteOn(keyboardChannel, data1, (byte)scaledVelocity);
                    }
                    break;
                case midi::NoteOff:
                    midiOut->sendNoteOff(keyboardChannel, data1, data2);
                    break;
                case midi::ControlChange:
                    midiOut->sendControlChange(keyboardChannel, data1, data2);
                    break;
                case midi::ProgramChange:
                    midiOut->sendProgramChange(keyboardChannel, data1);
                    break;
                case midi::PitchBend:
                    {
                        int bend = (data2 << 7) | data1;
                        midiOut->sendPitchBend(keyboardChannel, bend - 8192);
                    }
                    break;
                case midi::AfterTouchChannel:
                    midiOut->sendAfterTouch(keyboardChannel, data1);
                    break;
                case midi::AfterTouchPoly:
                    midiOut->sendPolyAfterTouch(keyboardChannel, data1, data2);
                    break;
                default:
                    break;
            }
        }
    }
}
