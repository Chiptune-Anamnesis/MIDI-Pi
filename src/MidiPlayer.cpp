#include "MidiPlayer.h"

MidiPlayer::MidiPlayer(MidiOutput* output) {
    midiOut = output;
    midiFile = nullptr;  // Initialize file pointer
    state = STATE_STOPPED;
    ticksElapsed = 0;
    lastUpdateMicros = 0;
    tempoPercent = 100;
    channelMutes = 0;
    velocityScale = 50; // Default: 50 = normal MIDI velocity
    eventReady = false;
    reachedEnd = false;
    microsecondsPerTick = 0;

    // MIDI Clock and Transport
    clockEnabled = false;
    lastClockMicros = 0;
    lastTransportState = STATE_STOPPED;

    // SysEx Control
    sysexEnabled = true; // SysEx enabled by default

    // Initialize all channel velocities to 100% (normal) and programs/volume/pan to defaults (no override)
    for (uint8_t i = 0; i < 16; i++) {
        channelVelocities[i] = 100;
        userChannelPrograms[i] = 128; // 128 = use MIDI file, 0-127 = override
        userChannelVolumes[i] = 255;  // 255 = use MIDI file, 0-127 = override
        userChannelPan[i] = 255;      // 255 = use MIDI file, 0-127 = override
        userChannelTranspose[i] = 0;  // 0 = no transpose, -24 to +24 = transpose in semitones
        userChannelRouting[i] = 255;  // 255 = use original channel, 0-15 = route to that channel
    }
}

MidiPlayer::~MidiPlayer() {
    unloadFile();
}

bool MidiPlayer::loadFile(FatFile* file) {
    if (!file) return false;

    // Stop current playback
    stop();

    // Reset playback position for new file (in case stop() returned early)
    ticksElapsed = 0;

    // Store pointer to file (caller retains ownership)
    midiFile = file;

    // Open the parser
    if (!parser.open("", midiFile)) {
        midiFile = nullptr;  // Clear pointer on error
        return false;
    }

    // NOTE: Timing calculation happens in setTempoPercent() call after file loads
    // Don't calculate here - player's tempoPercent is stale from previous file!

    // Read first event
    eventReady = parser.readNextEvent(nextEvent);

    return true;
}

void MidiPlayer::unloadFile() {
    // Stop playback without resetting (skip wasted SD card I/O)
    // NOTE: Caller should wait ~10ms after stop() before calling this
    // to ensure Core 1 has exited update()
    stop(false);

    // Close parser - this properly cleans up all track state including SysEx data
    parser.close();
    midiFile = nullptr;  // Clear pointer (caller owns the file, will close it)
}

void MidiPlayer::calculateMicrosecondsPerTick() {
    MidiFileInfo info = parser.getFileInfo();
    uint32_t tempo = info.tempo; // microseconds per quarter note

    // Apply tempo adjustment (tenth-percent precision: 1000 = 100.0%)
    // Use 64-bit arithmetic to prevent overflow (tempo can be up to 4 billion)
    tempo = static_cast<uint32_t>((static_cast<uint64_t>(tempo) * 1000) / tempoPercent);

    microsecondsPerTick = tempo / info.ticksPerQuarter;
}

void MidiPlayer::play() {
    if (state == STATE_PLAYING) return;

    // Clear end flag when starting playback
    reachedEnd = false;

    // Always stop all notes before starting/resuming playback
    // This prevents hanging notes from previous playback
    stopAllNotes();

    // Small delay to ensure all MIDI messages are sent
    delay(10);

    bool wasStoppedAtStart = (state == STATE_STOPPED && ticksElapsed == 0);

    if (wasStoppedAtStart) {
        // Start from beginning only if we're at position 0
        if (!parser.reset()) {
            // Reset failed - SD card error
            state = STATE_STOPPED;
            return;
        }
        eventReady = parser.readNextEvent(nextEvent);
    }
    // Otherwise, resume from current position (ticksElapsed is preserved)

    state = STATE_PLAYING;
    lastUpdateMicros = micros();
    lastClockMicros = micros();

    // Send MIDI Clock transport message
    if (clockEnabled) {
        if (wasStoppedAtStart) {
            midiOut->sendStart();
        } else {
            midiOut->sendContinue();
        }
    }
}

void MidiPlayer::pause() {
    if (state != STATE_PLAYING) return;

    state = STATE_PAUSED;

    // Send MIDI Clock stop message
    if (clockEnabled) {
        midiOut->sendStop();
    }

    // Stop all playing notes to prevent stuck notes
    stopAllNotes();
    // Small delay to ensure all MIDI messages are sent
    delay(10);
    // ticksElapsed is preserved for resume
}

void MidiPlayer::stop(bool resetToBeginning) {
    if (state == STATE_STOPPED) return;

    state = STATE_STOPPED;

    // Send MIDI Clock stop message
    if (clockEnabled) {
        midiOut->sendStop();
    }

    // Stop all playing notes
    stopAllNotes();

    // Small delay to ensure all MIDI messages are sent
    delay(10);

    // Only reset parser if requested (skip for unload to avoid wasted SD card I/O)
    if (resetToBeginning) {
        // Reset parser to beginning
        if (parser.reset()) {
            ticksElapsed = 0;  // Reset position when explicitly stopped
            eventReady = parser.readNextEvent(nextEvent);
        }
        // If reset fails, keep current position (SD card may have error)
    }
}

void MidiPlayer::stopAllNotes() {
    if (!midiOut) return;

    // Send All Notes Off (CC 123) to all 16 channels
    // This is much faster than panic mode and sufficient for normal playback
    for (uint8_t ch = 1; ch <= 16; ch++) {
        midiOut->sendControlChange(ch, 123, 0); // All Notes Off
    }
}

void MidiPlayer::resetMidiDevice() {
    if (!midiOut) return;

    // Comprehensive MIDI reset for switching between songs
    // This clears notes, controllers, and sound state
    for (uint8_t ch = 1; ch <= 16; ch++) {
        midiOut->sendControlChange(ch, 120, 0); // All Sound Off (immediate silence)
        midiOut->sendControlChange(ch, 123, 0); // All Notes Off
        midiOut->sendControlChange(ch, 121, 0); // Reset All Controllers
    }

    // Small delay to ensure messages are processed
    delay(10);
}

void MidiPlayer::update() {
    if (state != STATE_PLAYING) return;
    if (!eventReady) {
        // End of file - set flag before stopping
        reachedEnd = true;
        stop();
        return;
    }

    // CRITICAL: Guard against division by zero if tempo not yet calculated
    if (microsecondsPerTick == 0) return;

    uint32_t currentMicros = micros();

    // Send MIDI Clock ticks (24 per quarter note)
    if (clockEnabled) {
        uint16_t bpm = getCurrentBPM();
        // Guard against division by zero if BPM is invalid
        if (bpm == 0) bpm = 120; // Default to 120 BPM
        // Calculate microseconds per clock tick: (60,000,000 / BPM) / 24
        uint32_t microsecondsPerClock = (60000000 / bpm) / 24;

        if (currentMicros - lastClockMicros >= microsecondsPerClock) {
            midiOut->sendClock();
            lastClockMicros = currentMicros;
        }
    }

    uint32_t elapsedMicros = currentMicros - lastUpdateMicros;

    // Calculate how many ticks have passed
    uint32_t ticksPassed = elapsedMicros / microsecondsPerTick;

    if (ticksPassed > 0) {
        ticksElapsed += ticksPassed;

        // Update lastUpdateMicros by the exact amount of microseconds consumed
        // This preserves fractional microseconds for accurate timing
        lastUpdateMicros += ticksPassed * microsecondsPerTick;

        // Process events that should happen by now
        // CRITICAL: Limit TIME spent in update() to avoid holding mutex too long
        // This prevents blocking Core 0 (UI thread) for extended periods
        // Large SysEx messages can take a long time, so use time-based limit instead of event count
        unsigned long updateStartMicros = micros();
        constexpr unsigned long MAX_UPDATE_TIME_MICROS = 15000;  // Max 15ms per update call (reduced from 50ms for better UI responsiveness)

        while (eventReady && nextEvent.absoluteTime <= ticksElapsed) {
            // Check if we should stop (allows fast exit when switching tracks)
            if (state != STATE_PLAYING) {
                return;
            }

            // CRITICAL: Check time budget BEFORE processing event
            // This prevents starting a large SysEx if already over budget
            unsigned long elapsed = micros() - updateStartMicros;
            if (elapsed > MAX_UPDATE_TIME_MICROS) {
                // Out of time - yield to Core 0 now
                break;
            }

            // Send the MIDI event
            sendMidiEvent(nextEvent);

            // SysEx data automatically freed by MidiEvent destructor

            // Read next event
            eventReady = parser.readNextEvent(nextEvent);

            if (!eventReady) {
                // End of file
                break;
            }
        }
    }
}

void MidiPlayer::sendMidiEvent(const MidiEvent& event) {
    if (!midiOut) return;

    // Handle tempo changes during playback
    if (event.isMetaEvent && event.data1 == META_TEMPO) {
        // Parser has already updated fileInfo.tempo when it read this event
        // Now we need to recalculate timing to match the new tempo
        calculateMicrosecondsPerTick();
        return; // Don't send meta events as MIDI
    }

    if (event.isMetaEvent) return; // Don't send other meta events

    // CRITICAL: Validate channel is in valid range (0-15)
    // Corrupted MIDI files could have invalid channels causing buffer overruns
    if (event.channel >= 16) return;

    uint8_t channel = event.channel + 1; // Convert to 1-based

    // Apply routing if configured
    if (userChannelRouting[event.channel] != 255) {
        // Route to different channel (0-15 in routing array = channels 1-16)
        channel = userChannelRouting[event.channel] + 1;
    }

    // Check if channel is muted
    if (channelMutes & (1 << event.channel)) {
        // Channel is muted, don't send note on/off
        if (event.type == MIDI_NOTE_ON || event.type == MIDI_NOTE_OFF) {
            return;
        }
    }

    switch (event.type) {
        case MIDI_NOTE_OFF:
            {
                // Apply transpose
                int16_t transposedNote = event.data1 + userChannelTranspose[event.channel];
                if (transposedNote < 0) transposedNote = 0;
                if (transposedNote > 127) transposedNote = 127;
                midiOut->sendNoteOff(channel, static_cast<uint8_t>(transposedNote), event.data2);
            }
            break;

        case MIDI_NOTE_ON:
            if (event.data2 == 0) {
                // Velocity 0 = note off
                int16_t transposedNote = event.data1 + userChannelTranspose[event.channel];
                if (transposedNote < 0) transposedNote = 0;
                if (transposedNote > 127) transposedNote = 127;
                midiOut->sendNoteOff(channel, static_cast<uint8_t>(transposedNote), 0);
            } else {
                // Scale velocity based on global velocityScale setting
                // velocityScale: 50 = use MIDI file velocity as-is (no change)
                //                100 = max velocity (127 for all notes)
                //                1 = very quiet
                // Formula: scaledVel = (originalVel * velocityScale * 2) / 100
                // At 50: scaledVel = originalVel * 100 / 100 = originalVel (no change)
                // At 100: scaledVel = originalVel * 200 / 100 = originalVel * 2 (max 127)
                // Explicit cast to uint16_t to prevent integer overflow
                uint16_t scaledVelocity = (static_cast<uint16_t>(event.data2) * static_cast<uint16_t>(velocityScale) * 2) / 100;

                // Also apply per-channel velocity scale if set (0 = use global only)
                if (channelVelocities[event.channel] != 0) {
                    // channelVelocities: 100 = normal, 50 = half, 200 = double
                    scaledVelocity = (scaledVelocity * channelVelocities[event.channel]) / 100;
                }

                if (scaledVelocity > 127) scaledVelocity = 127;
                if (scaledVelocity < 1) scaledVelocity = 1;

                // Apply transpose
                int16_t transposedNote = event.data1 + userChannelTranspose[event.channel];
                if (transposedNote < 0) transposedNote = 0;
                if (transposedNote > 127) transposedNote = 127;

                midiOut->sendNoteOn(channel, static_cast<uint8_t>(transposedNote), static_cast<uint8_t>(scaledVelocity));
            }
            break;

        case MIDI_POLY_AFTERTOUCH:
            midiOut->sendPolyAfterTouch(channel, event.data1, event.data2);
            break;

        case MIDI_CONTROL_CHANGE:
            // Check if user has overridden volume (CC7) or pan (CC10)
            if (event.data1 == 7 && userChannelVolumes[event.channel] < 128) {
                // User has set volume override (0-127) - ignore MIDI file volume changes
                break;
            } else if (event.data1 == 10 && userChannelPan[event.channel] < 128) {
                // User has set pan override (0-127) - ignore MIDI file pan changes
                break;
            } else {
                // Allow MIDI file to control this CC message
                midiOut->sendControlChange(channel, event.data1, event.data2);
            }
            break;

        case MIDI_PROGRAM_CHANGE:
            // Auto-detect override: if user has set a valid program (0-127), ignore MIDI file
            // We use 128 as "not set" to allow MIDI file to control this channel
            if (userChannelPrograms[event.channel] < 128) {
                // User has set a program (0-127) - ignore MIDI file program changes
                // User's manual settings will be sent at playback start
                break;
            } else {
                // User hasn't set a program (128) - allow MIDI file to control
                midiOut->sendProgramChange(channel, event.data1);
            }
            break;

        case MIDI_CHANNEL_AFTERTOUCH:
            midiOut->sendAfterTouch(channel, event.data1);
            break;

        case MIDI_PITCH_BEND:
            {
                int16_t bend = (event.data2 << 7) | event.data1;
                bend -= 8192; // Convert to signed
                midiOut->sendPitchBend(channel, bend);
            }
            break;

        case MIDI_SYSEX:
            // Filter SysEx if disabled (prevents MT-32 detuning and patch modifications)
            if (!sysexEnabled) break;

            if (event.sysexData && event.sysexLength > 0) {
                // Hardware MIDI transmission at 31.25kbaud takes ~80ms for 265 bytes
                // This is unavoidable with hardware MIDI, but won't crash with fixed memory leak
                midiOut->sendSysEx(event.sysexData, event.sysexLength);
            }
            break;
    }
}

void MidiPlayer::setTempoPercent(uint16_t percent) {
    // Clamp to 50.0% - 200.0% (tenth-percent precision)
    if (percent < 500) percent = 500;
    if (percent > 2000) percent = 2000;

    tempoPercent = percent;
    calculateMicrosecondsPerTick();
}

void MidiPlayer::setVelocityScale(uint8_t scale) {
    if (scale < 1) scale = 1;
    if (scale > 100) scale = 100;

    velocityScale = scale;
}

void MidiPlayer::setChannelPrograms(uint8_t* programs) {
    if (!programs) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        userChannelPrograms[i] = programs[i];
    }
}

void MidiPlayer::setChannelVolumes(uint8_t* volumes) {
    if (!volumes) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        userChannelVolumes[i] = volumes[i];
    }
}

void MidiPlayer::setChannelPan(uint8_t* pan) {
    if (!pan) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        userChannelPan[i] = pan[i];
    }
}

void MidiPlayer::setChannelTranspose(int8_t* transpose) {
    if (!transpose) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        userChannelTranspose[i] = transpose[i];
    }
}

void MidiPlayer::setChannelVelocityScales(uint8_t* velocities) {
    if (!velocities) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        // Clamp value to valid range (0 = use MIDI file, 1-200)
        uint8_t value = velocities[i];
        if (value > 200) value = 200;
        channelVelocities[i] = value;
    }
}

void MidiPlayer::setChannelRouting(uint8_t* routing) {
    if (!routing) return; // Null pointer check
    for (uint8_t i = 0; i < 16; i++) {
        userChannelRouting[i] = routing[i];
    }
}

uint16_t MidiPlayer::getCurrentBPM() {
    MidiFileInfo info = parser.getFileInfo();
    uint32_t tempo = info.tempo; // microseconds per quarter note

    // Apply tempo adjustment (tenth-percent precision: 1000 = 100.0%)
    tempo = (tempo * 1000) / tempoPercent;

    // Guard against division by zero
    if (tempo == 0) return 120; // Default to 120 BPM

    // BPM = 60,000,000 / tempo
    return 60000000 / tempo;
}

void MidiPlayer::muteChannel(uint8_t channel) {
    if (channel >= 16) return;
    channelMutes |= (1 << channel);

    // Stop any playing notes on this channel
    if (midiOut) {
        midiOut->sendControlChange(channel + 1, 123, 0);
    }
}

void MidiPlayer::unmuteChannel(uint8_t channel) {
    if (channel >= 16) return;
    channelMutes &= ~(1 << channel);
}

void MidiPlayer::toggleMuteChannel(uint8_t channel) {
    if (isChannelMuted(channel)) {
        unmuteChannel(channel);
    } else {
        muteChannel(channel);
    }
}

bool MidiPlayer::isChannelMuted(uint8_t channel) {
    if (channel >= 16) return false;
    return channelMutes & (1 << channel);
}

uint32_t MidiPlayer::ticksToMilliseconds(uint32_t ticks) {
    // Guard against division by zero
    if (microsecondsPerTick == 0) return 0;

    // Use 64-bit arithmetic to prevent overflow for long files
    uint64_t microseconds = (uint64_t)ticks * microsecondsPerTick;
    return (uint32_t)(microseconds / 1000);
}

uint32_t MidiPlayer::millisecondsToTicks(uint32_t ms) {
    // Guard against division by zero
    if (microsecondsPerTick == 0) return 0;

    // Use 64-bit arithmetic to prevent overflow
    uint64_t ticks = ((uint64_t)ms * 1000) / microsecondsPerTick;
    return (uint32_t)ticks;
}

uint32_t MidiPlayer::getCurrentTimeMs() {
    if (state != STATE_PLAYING) {
        // When stopped/paused, return time based on current tick position
        return ticksToMilliseconds(ticksElapsed);
    }

    // When playing, add fractional time for smoother display
    uint32_t currentMicros = micros();
    uint32_t elapsedMicros = currentMicros - lastUpdateMicros;

    // Calculate fractional milliseconds since last tick update
    uint32_t fractionalMs = elapsedMicros / 1000;

    // Add to tick-based time
    return ticksToMilliseconds(ticksElapsed) + fractionalMs;
}

uint32_t MidiPlayer::getTotalTimeMs() {
    // Use the pre-calculated file length (scanned at load time)
    uint32_t lengthTicks = parser.getFileLengthTicks();
    return ticksToMilliseconds(lengthTicks);
}

void MidiPlayer::fastForward(uint32_t milliseconds) {
    // Pause playback during seeking to prevent MIDI leakage and SD card conflicts
    bool wasPlaying = (state == STATE_PLAYING);
    if (wasPlaying) {
        pause();  // Pause playback
        delay(10);  // Brief delay to ensure update() has exited
    }

    // Stop all notes before seeking to prevent stuck notes
    stopAllNotes();

    // Convert to ticks and calculate target
    uint32_t targetTicks = ticksElapsed + millisecondsToTicks(milliseconds);
    uint32_t maxTicks = parser.getFileLengthTicks();

    // Clamp to file length
    if (targetTicks > maxTicks) {
        targetTicks = maxTicks;
    }

    // Fast-forward by processing events without sending them
    uint32_t eventsProcessed = 0;
    const uint32_t MAX_EVENTS_PER_FF = 50000; // Safety limit: ~10 minutes of dense MIDI
    while (eventReady && nextEvent.absoluteTime <= targetTicks && eventsProcessed < MAX_EVENTS_PER_FF) {
        if (nextEvent.sysexData) {
            delete[] nextEvent.sysexData;
            nextEvent.sysexData = nullptr;
        }
        eventReady = parser.readNextEvent(nextEvent);

        // Yield every 100 events to prevent SD card timeout and give other tasks CPU time
        eventsProcessed++;
        if (eventsProcessed % 100 == 0) {
            yield();
        }
    }

    // If we hit the safety limit, stop seeking
    if (eventsProcessed >= MAX_EVENTS_PER_FF) {
        // Reached safety limit
    }

    ticksElapsed = targetTicks;
    lastUpdateMicros = micros();

    // Stop all notes again after seeking to be safe
    stopAllNotes();

    // Resume playback if we were playing before (stay paused if already paused)
    if (wasPlaying) {
        play();
    }
}

void MidiPlayer::rewind(uint32_t milliseconds) {
    // Pause playback during seeking to prevent MIDI leakage and SD card conflicts
    bool wasPlaying = (state == STATE_PLAYING);
    if (wasPlaying) {
        pause();  // Pause playback
        delay(10);  // Brief delay to ensure update() has exited
    }

    // Stop all notes before seeking to prevent stuck notes
    stopAllNotes();

    uint32_t rewindTicks = millisecondsToTicks(milliseconds);
    uint32_t targetTicks = 0;

    if (ticksElapsed > rewindTicks) {
        // Calculate target position
        targetTicks = ticksElapsed - rewindTicks;
    } else {
        // Rewind would go past beginning, go to start
        targetTicks = 0;
    }

    // Reset parser and seek to target position
    if (!parser.reset()) {
        // Reset failed - SD card error, abort rewind
        return;
    }
    ticksElapsed = 0;
    eventReady = parser.readNextEvent(nextEvent);

    // Fast-forward to target position (silently - events not sent)
    if (targetTicks > 0) {
        uint32_t eventsProcessed = 0;
        const uint32_t MAX_EVENTS_PER_REWIND = 50000; // Safety limit
        while (eventReady && nextEvent.absoluteTime <= targetTicks && eventsProcessed < MAX_EVENTS_PER_REWIND) {
            if (nextEvent.sysexData) {
                delete[] nextEvent.sysexData;
                nextEvent.sysexData = nullptr;
            }
            eventReady = parser.readNextEvent(nextEvent);

            // Yield every 100 events to prevent SD card timeout
            eventsProcessed++;
            if (eventsProcessed % 100 == 0) {
                yield();
            }
        }

        // If we hit the safety limit, stop seeking
        if (eventsProcessed >= MAX_EVENTS_PER_REWIND) {
            // Reached safety limit
        }

        ticksElapsed = targetTicks;
    }

    lastUpdateMicros = micros();

    // Stop all notes again after seeking to be safe
    stopAllNotes();

    // Resume playback if we were playing before (stay paused if already paused)
    if (wasPlaying) {
        play();
    }
}

void MidiPlayer::seek(uint32_t milliseconds) {
    // Stop all notes before seeking
    stopAllNotes();

    // Restart and fast-forward to position
    if (!parser.reset()) {
        // Reset failed - SD card error, abort seek
        return;
    }
    ticksElapsed = 0;
    eventReady = parser.readNextEvent(nextEvent);
    fastForward(milliseconds);

    // Note: fastForward() already calls stopAllNotes() at end
}
