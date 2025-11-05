#ifndef MIDI_FILE_PARSER_H
#define MIDI_FILE_PARSER_H

#include <Arduino.h>
#include <SdFat.h>

// MIDI event types
#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_POLY_AFTERTOUCH 0xA0
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHANNEL_AFTERTOUCH 0xD0
#define MIDI_PITCH_BEND 0xE0
#define MIDI_SYSEX 0xF0
#define MIDI_META_EVENT 0xFF

// Meta event types
#define META_SEQUENCE_NUMBER 0x00
#define META_TEXT 0x01
#define META_COPYRIGHT 0x02
#define META_TRACK_NAME 0x03
#define META_INSTRUMENT_NAME 0x04
#define META_LYRIC 0x05
#define META_MARKER 0x06
#define META_CUE_POINT 0x07
#define META_CHANNEL_PREFIX 0x20
#define META_END_OF_TRACK 0x2F
#define META_TEMPO 0x51
#define META_SMPTE_OFFSET 0x54
#define META_TIME_SIGNATURE 0x58
#define META_KEY_SIGNATURE 0x59
#define META_SEQUENCER_SPECIFIC 0x7F

#define MAX_TRACKS 16

struct MidiEvent {
    uint32_t deltaTime;      // Delta time in ticks
    uint32_t absoluteTime;   // Absolute time in ticks
    uint8_t type;            // Event type (0x80-0xFF)
    uint8_t channel;         // MIDI channel (0-15)
    uint8_t data1;           // First data byte
    uint8_t data2;           // Second data byte
    uint8_t* sysexData;      // SysEx data (if applicable)
    uint16_t sysexLength;    // SysEx data length
    bool isMetaEvent;        // True if this is a meta event
    uint8_t trackNumber;     // Which track this event came from

    // Constructor
    MidiEvent() : deltaTime(0), absoluteTime(0), type(0), channel(0),
                  data1(0), data2(0), sysexData(nullptr), sysexLength(0),
                  isMetaEvent(false), trackNumber(0) {}

    // Destructor - automatically free SysEx data
    ~MidiEvent() {
        if (sysexData) {
            delete[] sysexData;
            sysexData = nullptr;
        }
    }

    // Copy constructor - deep copy SysEx data
    MidiEvent(const MidiEvent& other)
        : deltaTime(other.deltaTime), absoluteTime(other.absoluteTime),
          type(other.type), channel(other.channel), data1(other.data1),
          data2(other.data2), sysexLength(other.sysexLength),
          isMetaEvent(other.isMetaEvent), trackNumber(other.trackNumber) {
        if (other.sysexData && other.sysexLength > 0) {
            sysexData = new uint8_t[other.sysexLength];
            for (uint16_t i = 0; i < other.sysexLength; i++) {
                sysexData[i] = other.sysexData[i];
            }
        } else {
            sysexData = nullptr;
        }
    }

    // Copy assignment operator
    MidiEvent& operator=(const MidiEvent& other) {
        if (this != &other) {
            // Free existing SysEx data
            if (sysexData) {
                delete[] sysexData;
                sysexData = nullptr;
            }

            // Copy scalar fields
            deltaTime = other.deltaTime;
            absoluteTime = other.absoluteTime;
            type = other.type;
            channel = other.channel;
            data1 = other.data1;
            data2 = other.data2;
            sysexLength = other.sysexLength;
            isMetaEvent = other.isMetaEvent;
            trackNumber = other.trackNumber;

            // Deep copy SysEx data
            if (other.sysexData && other.sysexLength > 0) {
                sysexData = new uint8_t[other.sysexLength];
                for (uint16_t i = 0; i < other.sysexLength; i++) {
                    sysexData[i] = other.sysexData[i];
                }
            }
        }
        return *this;
    }
};

struct MidiFileInfo {
    uint16_t format;         // 0=single track, 1=multi track, 2=multi song
    uint16_t numTracks;      // Number of tracks
    uint16_t ticksPerQuarter;// PPQN (Pulses Per Quarter Note)
    uint32_t tempo;          // Microseconds per quarter note
    uint8_t numerator;       // Time signature numerator
    uint8_t denominator;     // Time signature denominator
    char trackName[64];      // Track name
};

// Per-track state with buffering
#define TRACK_BUFFER_SIZE 512

struct TrackState {
    uint32_t trackStartPos;  // Start position of track data in file
    uint32_t trackEndPos;    // End position of this track
    uint32_t filePosition;   // Current logical position in track
    uint32_t currentTick;    // Current absolute tick for this track
    uint8_t runningStatus;   // Running status for this track
    bool endOfTrack;         // Has this track ended?
    MidiEvent nextEvent;     // Next event buffered for this track
    bool eventReady;         // Is there an event ready?

    // Read buffer to minimize SD seeks
    uint8_t buffer[TRACK_BUFFER_SIZE];
    uint16_t bufferPos;      // Current position in buffer
    uint16_t bufferSize;     // How much valid data in buffer
    uint32_t bufferFilePos;  // File position of start of buffer
};

class MidiFileParser {
public:
    MidiFileParser();
    ~MidiFileParser();

    bool open(const char* filename, FatFile* file);
    void close();
    bool readNextEvent(MidiEvent& event);
    bool reset();  // Returns false if seek fails

    MidiFileInfo getFileInfo() { return fileInfo; }
    uint32_t getTotalTicks();
    uint32_t getFileLengthTicks() { return fileLengthTicks; }
    bool isEndOfFile();

    // Update file length based on playback (track max time seen)
    void updateFileLengthFromPlayback(uint32_t ticks) {
        if (ticks > fileLengthTicks) {
            fileLengthTicks = ticks;
        }
    }

    // Calculate file length by scanning (expensive - use cache system to call only once)
    void calculateFileLengthNow() { calculateFileLength(); }

    // Set file length from cache
    void setFileLengthTicks(uint32_t ticks) { fileLengthTicks = ticks; }

    // SysEx count (for MT-32 detection)
    uint16_t getSysexCount() { return sysexCount; }
    void setSysexCount(uint16_t count) { sysexCount = count; }

    // Scan for initial tempo (call after open, before cache check)
    void scanForInitialTempo();

private:
    FatFile* midiFile;
    MidiFileInfo fileInfo;

    // Multi-track support
    TrackState tracks[MAX_TRACKS];
    uint8_t numTracks;
    bool allTracksEnded;
    uint32_t fileLengthTicks; // Total length of file in ticks
    uint16_t sysexCount;      // Number of SysEx messages found during scan

    // Helper functions
    uint32_t readVariableLength();
    uint16_t read16();
    uint32_t read24();
    uint32_t read32();
    uint8_t read8();
    bool readMidiHeader();
    bool initializeTracks();
    bool readTrackEvent(uint8_t trackNum, MidiEvent& event);

    // Buffered reading for specific track
    uint8_t readTrackByte(uint8_t trackNum);
    uint32_t readTrackVariableLength(uint8_t trackNum);
    bool fillTrackBuffer(uint8_t trackNum);

    // Calculate total file length
    void calculateFileLength();
};

#endif // MIDI_FILE_PARSER_H
