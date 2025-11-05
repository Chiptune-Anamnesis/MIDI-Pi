#include "MidiFileParser.h"

MidiFileParser::MidiFileParser() {
    midiFile = nullptr;
    numTracks = 0;
    allTracksEnded = false;
    fileLengthTicks = 0;
    sysexCount = 0;
    memset(&fileInfo, 0, sizeof(MidiFileInfo));
    memset(tracks, 0, sizeof(tracks));
    fileInfo.tempo = 500000; // Default 120 BPM
    fileInfo.numerator = 4;
    fileInfo.denominator = 4;
}

MidiFileParser::~MidiFileParser() {
    close();
}

void MidiFileParser::close() {
    // Clean up all tracks to prevent stale SysEx pointers and memory leaks
    // CRITICAL: Must reset ALL tracks (not just numTracks) in case previous file had more tracks
    for (uint8_t i = 0; i < MAX_TRACKS; i++) {
        // Explicitly free SysEx data by assigning a clean MidiEvent
        // This triggers the MidiEvent destructor which deletes sysexData
        tracks[i].nextEvent = MidiEvent();

        // Reset other track state
        tracks[i].trackStartPos = 0;
        tracks[i].trackEndPos = 0;
        tracks[i].filePosition = 0;
        tracks[i].currentTick = 0;
        tracks[i].runningStatus = 0;
        tracks[i].endOfTrack = false;
        tracks[i].eventReady = false;
        tracks[i].bufferPos = 0;
        tracks[i].bufferSize = 0;
        tracks[i].bufferFilePos = 0;
    }

    midiFile = nullptr;
    numTracks = 0;
    allTracksEnded = false;
    fileLengthTicks = 0;
}

bool MidiFileParser::open(const char* filename, FatFile* file) {
    midiFile = file;
    allTracksEnded = false;

    // CRITICAL: Reset tempo to default BEFORE reading the file
    // This prevents tempo from carrying over from previous file
    fileInfo.tempo = 500000;  // Default 120 BPM
    fileInfo.numerator = 4;
    fileInfo.denominator = 4;
    memset(fileInfo.trackName, 0, sizeof(fileInfo.trackName));

    if (!readMidiHeader()) {
        return false;
    }

    if (!initializeTracks()) {
        return false;
    }

    // NOTE: File length calculation is now handled by caller via cache system
    // to avoid heap fragmentation from repeated scans. Caller will call
    // calculateFileLength() only once per file and cache the result.
    fileLengthTicks = 0;  // Will be set by cache or calculateFileLength()

    return true;
}

uint8_t MidiFileParser::read8() {
    uint8_t val = 0;
    if (midiFile && midiFile->available()) {
        midiFile->read(&val, 1);
    }
    return val;
}

uint16_t MidiFileParser::read16() {
    uint16_t val = read8() << 8;
    val |= read8();
    return val;
}

uint32_t MidiFileParser::read24() {
    uint32_t val = read8() << 16;
    val |= read8() << 8;
    val |= read8();
    return val;
}

uint32_t MidiFileParser::read32() {
    uint32_t val = read8() << 24;
    val |= read8() << 16;
    val |= read8() << 8;
    val |= read8();
    return val;
}

uint32_t MidiFileParser::readVariableLength() {
    uint32_t value = 0;
    uint8_t byte;

    do {
        byte = read8();
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

bool MidiFileParser::readMidiHeader() {
    if (!midiFile) return false;

    // Read "MThd"
    char header[4];
    midiFile->read(header, 4);
    if (strncmp(header, "MThd", 4) != 0) {
        return false;
    }

    // Read header length (should be 6)
    uint32_t headerLength = read32();
    if (headerLength != 6) {
        return false;
    }

    // Read format
    fileInfo.format = read16();

    // Read number of tracks
    fileInfo.numTracks = read16();
    numTracks = fileInfo.numTracks;
    if (numTracks > MAX_TRACKS) {
        numTracks = MAX_TRACKS; // Limit to max supported
    }

    // Read ticks per quarter note
    fileInfo.ticksPerQuarter = read16();

    return true;
}

bool MidiFileParser::initializeTracks() {
    if (!midiFile) return false;

    // Read all track headers and initialize track states
    for (uint8_t i = 0; i < numTracks; i++) {
        // Read "MTrk"
        char header[4];
        midiFile->read(header, 4);
        if (strncmp(header, "MTrk", 4) != 0) {
            return false;
        }

        // Read track length
        uint32_t trackLength = read32();

        // Initialize track state
        tracks[i].trackStartPos = midiFile->curPosition();
        tracks[i].filePosition = 0; // Relative to track start
        tracks[i].trackEndPos = trackLength;
        tracks[i].currentTick = 0;
        tracks[i].runningStatus = 0;
        tracks[i].endOfTrack = false;
        tracks[i].eventReady = false;
        // nextEvent initialized by MidiEvent constructor

        // Initialize buffer
        tracks[i].bufferPos = 0;
        tracks[i].bufferSize = 0;
        tracks[i].bufferFilePos = 0;

        // Skip to next track for now
        if (!midiFile->seekSet(tracks[i].trackStartPos + trackLength)) {
            // Seek failed - SD card error
            return false;
        }
    }

    // Now pre-read first event from each track
    for (uint8_t i = 0; i < numTracks; i++) {
        // Fill initial buffer for this track
        fillTrackBuffer(i);

        if (readTrackEvent(i, tracks[i].nextEvent)) {
            tracks[i].eventReady = true;
        }
    }

    return true;
}

bool MidiFileParser::readTrackEvent(uint8_t trackNum, MidiEvent& event) {
    if (trackNum >= numTracks) return false;
    if (tracks[trackNum].endOfTrack) return false;

    TrackState* track = &tracks[trackNum];

    // Check if we've reached end of track
    if (track->filePosition >= track->trackEndPos) {
        track->endOfTrack = true;
        return false;
    }

    // Read delta time using buffered read
    event.deltaTime = readTrackVariableLength(trackNum);
    track->currentTick += event.deltaTime;
    event.absoluteTime = track->currentTick;
    event.trackNumber = trackNum;

    // Read event type using buffered read
    uint8_t statusByte = readTrackByte(trackNum);

    // Handle running status
    if (statusByte < 0x80) {
        statusByte = track->runningStatus;
        // Unread the byte
        track->bufferPos--;
        track->filePosition--;
    } else {
        track->runningStatus = statusByte;
    }

    event.type = statusByte & 0xF0;
    event.channel = statusByte & 0x0F;
    event.isMetaEvent = false;

    // CRITICAL: Free existing SysEx data to prevent memory leak
    // readTrackEvent() is called repeatedly on the same MidiEvent reference
    if (event.sysexData) {
        delete[] event.sysexData;
        event.sysexData = nullptr;
    }
    event.sysexLength = 0;

    // Handle different event types
    if (statusByte == MIDI_SYSEX || statusByte == 0xF7) {
        // SysEx event
        uint32_t length = readTrackVariableLength(trackNum);
        event.sysexLength = length;
        event.sysexData = new uint8_t[length];
        // Read sysex data
        for (uint32_t i = 0; i < length; i++) {
            event.sysexData[i] = readTrackByte(trackNum);
        }
        track->runningStatus = 0;
    } else if (statusByte == MIDI_META_EVENT) {
        // Meta event
        event.isMetaEvent = true;
        uint8_t metaType = readTrackByte(trackNum);
        event.data1 = metaType;
        uint32_t length = readTrackVariableLength(trackNum);

        // Handle specific meta events
        switch (metaType) {
            case META_TEMPO:
                if (length == 3) {
                    uint32_t tempo = readTrackByte(trackNum) << 16;
                    tempo |= readTrackByte(trackNum) << 8;
                    tempo |= readTrackByte(trackNum);

                    // Validate tempo is in reasonable range
                    // Min: 100,000 microseconds = 600 BPM
                    // Max: 10,000,000 microseconds = 6 BPM
                    if (tempo >= 100000 && tempo <= 10000000) {
                        fileInfo.tempo = tempo;
                    }
                    // Keep previous tempo if invalid
                } else {
                    // Skip invalid length tempo events
                    for (uint32_t i = 0; i < length; i++) {
                        readTrackByte(trackNum);
                    }
                }
                break;

            case META_TIME_SIGNATURE:
                if (length == 4) {
                    fileInfo.numerator = readTrackByte(trackNum);
                    fileInfo.denominator = 1 << readTrackByte(trackNum);
                    readTrackByte(trackNum); // MIDI clocks per metronome click
                    readTrackByte(trackNum); // 32nd notes per quarter note
                }
                break;

            case META_TRACK_NAME:
                if (length < sizeof(fileInfo.trackName)) {
                    for (uint32_t i = 0; i < length; i++) {
                        fileInfo.trackName[i] = readTrackByte(trackNum);
                    }
                    fileInfo.trackName[length] = '\0';
                } else {
                    // Skip
                    for (uint32_t i = 0; i < length; i++) {
                        readTrackByte(trackNum);
                    }
                }
                break;

            case META_END_OF_TRACK:
                track->endOfTrack = true;
                return false;

            default:
                // Skip unknown meta events
                for (uint32_t i = 0; i < length; i++) {
                    readTrackByte(trackNum);
                }
                break;
        }
        track->runningStatus = 0;
    } else {
        // Regular MIDI event
        switch (event.type) {
            case MIDI_NOTE_OFF:
            case MIDI_NOTE_ON:
            case MIDI_POLY_AFTERTOUCH:
            case MIDI_CONTROL_CHANGE:
            case MIDI_PITCH_BEND:
                event.data1 = readTrackByte(trackNum);
                event.data2 = readTrackByte(trackNum);
                break;

            case MIDI_PROGRAM_CHANGE:
            case MIDI_CHANNEL_AFTERTOUCH:
                event.data1 = readTrackByte(trackNum);
                event.data2 = 0;
                break;

            default:
                return false;
        }
    }

    return true;
}

bool MidiFileParser::readNextEvent(MidiEvent& event) {
    if (allTracksEnded) return false;

    // Find the track with the earliest next event
    int8_t earliestTrack = -1;
    uint32_t earliestTime = 0xFFFFFFFF;

    for (uint8_t i = 0; i < numTracks; i++) {
        if (tracks[i].eventReady && !tracks[i].endOfTrack) {
            if (tracks[i].nextEvent.absoluteTime < earliestTime) {
                earliestTime = tracks[i].nextEvent.absoluteTime;
                earliestTrack = i;
            }
        }
    }

    if (earliestTrack == -1) {
        allTracksEnded = true;
        return false;
    }

    // Return the earliest event
    event = tracks[earliestTrack].nextEvent;

    // Read next event from that track (uses buffered reading, no seek needed!)
    if (readTrackEvent(earliestTrack, tracks[earliestTrack].nextEvent)) {
        tracks[earliestTrack].eventReady = true;
    } else {
        tracks[earliestTrack].eventReady = false;
    }

    return true;
}

bool MidiFileParser::reset() {
    if (!midiFile) return false;

    // Seek back to start and re-initialize
    if (!midiFile->seekSet(0)) {
        // Seek failed - SD card error
        return false;
    }

    readMidiHeader();
    initializeTracks();
    allTracksEnded = false;
    return true;
}

uint32_t MidiFileParser::getTotalTicks() {
    // Return the maximum tick from all tracks
    uint32_t maxTick = 0;
    for (uint8_t i = 0; i < numTracks; i++) {
        if (tracks[i].currentTick > maxTick) {
            maxTick = tracks[i].currentTick;
        }
    }
    return maxTick;
}

bool MidiFileParser::isEndOfFile() {
    return allTracksEnded;
}

// Buffered reading functions to minimize SD card seeks
bool MidiFileParser::fillTrackBuffer(uint8_t trackNum) {
    if (trackNum >= numTracks) return false;

    TrackState* track = &tracks[trackNum];

    // Calculate how much we can read
    uint32_t absoluteFilePos = track->trackStartPos + track->filePosition;
    uint32_t trackBytesLeft = track->trackEndPos - track->filePosition;

    if (trackBytesLeft == 0) {
        track->bufferSize = 0;
        return false;
    }

    // Seek to position and read a chunk
    if (!midiFile->seekSet(absoluteFilePos)) {
        // Seek failed - SD card error
        track->bufferSize = 0;
        return false;
    }

    uint16_t bytesToRead = (trackBytesLeft > TRACK_BUFFER_SIZE) ? TRACK_BUFFER_SIZE : trackBytesLeft;
    track->bufferSize = midiFile->read(track->buffer, bytesToRead);
    track->bufferPos = 0;
    track->bufferFilePos = track->filePosition;

    return (track->bufferSize > 0);
}

uint8_t MidiFileParser::readTrackByte(uint8_t trackNum) {
    if (trackNum >= numTracks) return 0;

    TrackState* track = &tracks[trackNum];

    // Check if we need to refill buffer
    if (track->bufferPos >= track->bufferSize) {
        if (!fillTrackBuffer(trackNum)) {
            return 0; // End of track
        }
    }

    uint8_t byte = track->buffer[track->bufferPos++];
    track->filePosition++;

    return byte;
}

uint32_t MidiFileParser::readTrackVariableLength(uint8_t trackNum) {
    uint32_t value = 0;
    uint8_t byte;

    do {
        byte = readTrackByte(trackNum);
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);

    return value;
}

void MidiFileParser::scanForInitialTempo() {
    // Scan the first events of track 0 to find the initial tempo
    // OPTIMIZED: Parse manually to avoid heap allocations from MidiEvent
    // Most MIDI files have tempo in track 0

    if (numTracks == 0) {
        return;
    }

    bool foundTempo = false;

    // Only scan track 0 - simpler and faster
    // Reset track 0 to start
    tracks[0].filePosition = 0;
    tracks[0].currentTick = 0;
    tracks[0].bufferPos = 0;
    tracks[0].bufferSize = 0;
    tracks[0].endOfTrack = false;
    tracks[0].runningStatus = 0;

    // Fill buffer
    if (fillTrackBuffer(0)) {
        // Manually parse events without creating MidiEvent objects (avoids heap allocations)
        for (uint8_t i = 0; i < 100; i++) {
            // Check if we've reached the end of track data
            if (tracks[0].filePosition >= (tracks[0].trackEndPos - tracks[0].trackStartPos)) {
                break;
            }

            // Read delta time
            uint32_t deltaTime = readTrackVariableLength(0);

            // Read status byte
            uint8_t status = readTrackByte(0);

            // Handle running status
            if (status < 0x80) {
                // Check if this is actually a failed read vs. running status
                if (status == 0 && tracks[0].bufferSize == 0) {
                    break;  // No more data
                }
                // This is a data byte, use running status
                status = tracks[0].runningStatus;

                // Put the data byte back by rewinding file position
                if (tracks[0].filePosition > 0) {
                    tracks[0].filePosition--;
                    // Also rewind buffer position if possible
                    if (tracks[0].bufferPos > 0) {
                        tracks[0].bufferPos--;
                    } else {
                        // At start of buffer, need to invalidate buffer to force refill
                        tracks[0].bufferSize = 0;
                    }
                }
            } else {
                // Update running status (but not for meta/sysex)
                if (status < 0xF0) {
                    tracks[0].runningStatus = status;
                }
            }

            // Check if this is a meta event (0xFF)
            if (status == 0xFF) {
                uint8_t metaType = readTrackByte(0);
                uint32_t length = readTrackVariableLength(0);

                // Check if it's a tempo event
                if (metaType == META_TEMPO && length == 3) {
                    uint32_t tempo = readTrackByte(0) << 16;
                    tempo |= readTrackByte(0) << 8;
                    tempo |= readTrackByte(0);

                    // Validate tempo is in reasonable range
                    if (tempo >= 100000 && tempo <= 10000000) {
                        fileInfo.tempo = tempo;
                        foundTempo = true;
                        break;  // Found tempo, exit early
                    }
                } else {
                    // Skip other meta events
                    for (uint32_t j = 0; j < length; j++) {
                        readTrackByte(0);
                    }
                }
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx event - skip it without allocating memory
                uint32_t length = readTrackVariableLength(0);
                for (uint32_t j = 0; j < length; j++) {
                    readTrackByte(0);
                }
            } else {
                // Regular MIDI event - skip data bytes
                uint8_t eventType = status & 0xF0;

                // Determine data byte count
                uint8_t dataBytes = 0;
                if (eventType == MIDI_PROGRAM_CHANGE || eventType == MIDI_CHANNEL_AFTERTOUCH) {
                    dataBytes = 1;
                } else if (eventType >= MIDI_NOTE_OFF && eventType <= MIDI_PITCH_BEND) {
                    dataBytes = 2;
                }

                // Skip data bytes
                for (uint8_t j = 0; j < dataBytes; j++) {
                    readTrackByte(0);
                }
            }

            // Check if we've reached end of track
            if (tracks[0].endOfTrack) {
                break;
            }
        }
    }

    if (!foundTempo) {
        fileInfo.tempo = 500000;  // Default 120 BPM
    }

    // Re-initialize all tracks to restore proper state
    // This is much safer than trying to save/restore track buffers
    if (!midiFile->seekSet(0)) {
        return;  // Seek failed
    }
    readMidiHeader();
    initializeTracks();
}

void MidiFileParser::calculateFileLength() {
    // Find the maximum end time across all tracks
    // OPTIMIZED: Parse manually to avoid heap allocations from MidiEvent objects
    fileLengthTicks = 0;
    sysexCount = 0;  // Reset SysEx count

    // Look at each track and find which has the latest event
    for (uint8_t i = 0; i < numTracks; i++) {
        // Save minimal state (avoid copying 512-byte buffer)
        uint32_t savedFilePos = tracks[i].filePosition;
        uint32_t savedTick = tracks[i].currentTick;
        uint8_t savedRunningStatus = tracks[i].runningStatus;
        bool savedEndOfTrack = tracks[i].endOfTrack;

        // Reset to start of this track
        tracks[i].filePosition = 0;
        tracks[i].currentTick = 0;
        tracks[i].bufferPos = 0;
        tracks[i].bufferSize = 0;
        tracks[i].endOfTrack = false;
        tracks[i].runningStatus = 0;
        fillTrackBuffer(i);

        // Track absolute time without creating MidiEvent objects
        uint32_t absoluteTime = 0;

        // Read through all events manually
        while (!tracks[i].endOfTrack) {
            // Check if we've reached the end of track data
            if (tracks[i].filePosition >= (tracks[i].trackEndPos - tracks[i].trackStartPos)) {
                break;
            }

            // Read delta time and accumulate to absolute time
            uint32_t deltaTime = readTrackVariableLength(i);

            // Sanity check: delta times shouldn't be unreasonably large
            // Max reasonable delta: 960 ticks/quarter * 300 quarters = 288000 ticks (~5 min at 120BPM)
            if (deltaTime > 500000) {
                break;  // Probably corrupted data
            }

            absoluteTime += deltaTime;

            // Read status byte
            uint8_t status = readTrackByte(i);

            // Handle running status
            bool isRunningStatus = false;
            if (status < 0x80) {
                // Check if this is actually a failed read (0 with empty buffer) vs. running status
                if (status == 0 && tracks[i].bufferSize == 0) {
                    break;  // No more data
                }
                // This is a data byte, use running status
                isRunningStatus = true;
                uint8_t dataByte = status;  // Save the data byte
                status = tracks[i].runningStatus;

                // Put the data byte back by rewinding file position
                if (tracks[i].filePosition > 0) {
                    tracks[i].filePosition--;
                    // Also rewind buffer position if possible
                    if (tracks[i].bufferPos > 0) {
                        tracks[i].bufferPos--;
                    } else {
                        // At start of buffer, need to invalidate buffer to force refill
                        tracks[i].bufferSize = 0;
                    }
                }
            } else {
                // Update running status (but not for meta/sysex)
                if (status < 0xF0) {
                    tracks[i].runningStatus = status;
                }
            }

            // Parse event to skip past data bytes without allocating memory
            if (status == 0xFF) {
                // Meta event
                uint8_t metaType = readTrackByte(i);
                uint32_t length = readTrackVariableLength(i);

                // Check for end of track
                if (metaType == META_END_OF_TRACK) {
                    tracks[i].endOfTrack = true;
                    break;
                }

                // Skip meta data
                for (uint32_t j = 0; j < length; j++) {
                    readTrackByte(i);
                }
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx event - count it and skip without allocating
                sysexCount++;  // Track for MT-32 detection
                uint32_t length = readTrackVariableLength(i);
                for (uint32_t j = 0; j < length; j++) {
                    readTrackByte(i);
                }
            } else {
                // Regular MIDI event - skip data bytes
                uint8_t eventType = status & 0xF0;

                // Determine data byte count
                uint8_t dataBytes = 0;
                if (eventType == MIDI_PROGRAM_CHANGE || eventType == MIDI_CHANNEL_AFTERTOUCH) {
                    dataBytes = 1;
                } else if (eventType >= MIDI_NOTE_OFF && eventType <= MIDI_PITCH_BEND) {
                    dataBytes = 2;
                }

                // Skip data bytes
                for (uint8_t j = 0; j < dataBytes; j++) {
                    readTrackByte(i);
                }
            }
        }

        // Update max length
        if (absoluteTime > fileLengthTicks) {
            fileLengthTicks = absoluteTime;
        }

        // Restore minimal state
        tracks[i].filePosition = savedFilePos;
        tracks[i].currentTick = savedTick;
        tracks[i].runningStatus = savedRunningStatus;
        tracks[i].endOfTrack = savedEndOfTrack;
        tracks[i].bufferPos = 0;
        tracks[i].bufferSize = 0;
    }

    // Re-initialize all tracks to ensure clean state
    if (midiFile->seekSet(0)) {
        readMidiHeader();
        initializeTracks();
    }
}
