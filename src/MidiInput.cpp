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

    // Remote control defaults
    remoteEnabled = false;
    remoteAutoPlay = true;
    remoteTransportEnabled = true;
    remoteChannel = 0;  // OMNI
    lastBankMSB = 0;

    fileLoadRequested = false;
    pendingFileIndex = 0;
    transportStartRequested = false;
    transportStopRequested = false;
    transportContinueRequested = false;
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

        // Remote control: Bank+PC file selection
        // Channel filter: 0=OMNI, otherwise must match (channel-less messages bypass this)
        if (remoteEnabled) {
            bool channelMatch = (remoteChannel == 0) || (channel == remoteChannel);
            if (channelMatch) {
                if (type == midi::ControlChange && data1 == 0) {
                    // CC0 = Bank Select MSB
                    lastBankMSB = data2 & 0x07;  // Clamp to 8 banks
                } else if (type == midi::ProgramChange) {
                    pendingFileIndex = (uint16_t)lastBankMSB * 128 + data1;
                    fileLoadRequested = true;
                }
            }
        }

        // Remote control: transport (system real-time, no channel)
        if (remoteTransportEnabled) {
            if (type == midi::Start) {
                transportStartRequested = true;
            } else if (type == midi::Stop) {
                transportStopRequested = true;
            } else if (type == midi::Continue) {
                transportContinueRequested = true;
            }
        }

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

bool MidiInput::consumeFileLoadRequest(uint16_t& outIdx) {
    if (!fileLoadRequested) return false;
    outIdx = pendingFileIndex;
    fileLoadRequested = false;
    return true;
}

bool MidiInput::consumeTransportStart() {
    if (!transportStartRequested) return false;
    transportStartRequested = false;
    return true;
}

bool MidiInput::consumeTransportStop() {
    if (!transportStopRequested) return false;
    transportStopRequested = false;
    return true;
}

bool MidiInput::consumeTransportContinue() {
    if (!transportContinueRequested) return false;
    transportContinueRequested = false;
    return true;
}
