# MIDI-PI Portable MIDI Player - User Manual

## Quick Start

1. **Setup**: Insert FAT32-formatted SD card with MIDI files in `/MIDI` folder
2. **Connect**: Connect MIDI output to your synthesizer or sound module
3. **Power On**: Device displays file browser
4. **Navigate**: Use LEFT/RIGHT to select a file
5. **Play**: Press PLAY button to start playback
6. **Adjust**: Press MODE to access Channel Settings for mixing/configuration

---

## Table of Contents
1. [Introduction](#introduction)
2. [Button Controls](#button-controls)
3. [Playback Screen](#playback-screen)
4. [File Browser](#file-browser)
5. [Channel Settings](#channel-settings)
6. [Track Settings](#track-settings)
7. [MIDI Settings](#midi-settings)
8. [Clock Settings](#clock-settings)
9. [Visualizer](#visualizer)
10. [Troubleshooting](#troubleshooting)

---

## Introduction

The MIDI-PI is a standalone hardware MIDI file player for musicians and synth enthusiasts. Play MIDI files from an SD card without a computer. Features include real-time tempo/velocity control, per-file settings memory, 16-channel mixing, and MIDI clock output.

**Key Features:**
- Standard MIDI file playback (Type 0 and Type 1)
- Per-file configuration saves (`.cfg` files)
- **Precise BPM Control** - 0.01 BPM precision (40.00-300.00 BPM) with separate whole/decimal editing
- **Tap Tempo** - Set BPM by tapping rhythmically
- Real-time tempo and velocity (1-100) control
- 16-channel mixer with program/pan/volume/velocity/transpose overrides
- Solo and routing per channel
- SysEx enable/disable per-file (prevents MT-32 detuning)
- Playback modes: Single, Auto-Next, Loop One, Loop All
- MIDI Thru and Keyboard modes
- MIDI Clock output for sync
- Real-time 16-channel visualizer with animated bubbles

---

## Button Controls

**Navigation:**
- **LEFT/RIGHT** - Navigate options, adjust values
- **OK** - Select/activate option (hold 2s to reset active option)
- **MODE** - Cycle through menus (hold 2s to jump to playback screen)

**Playback:**
- **PLAY** - Start/resume playback
- **STOP** - Stop playback
- **PANIC** - Emergency all-notes-off (all 16 channels)

**Sequential Actions:**
- **STOP, then PLAY** - Stop playback, then press PLAY to reload config settings and resume

---

## Playback Screen

Main screen during playback with quick-access controls.

**Display:**
```
Track: SONG_NAME.MID
00:45 / 03:30  ► SNG
BPM:120.50  [TAP]  MODE
```

**Elements:**
- **Track**: Current MIDI file name
- **00:45 / 03:30**: Current position / Total length (MM:SS)
- **►**: Playback state (► Playing, ❚❚ Paused, ■ Stopped)
- **SNG**: Playback mode
- **BPM:120.50**: Current tempo in beats per minute (with 0.01 BPM precision)
- **[TAP]**: Tap tempo button for quick BPM setting
- **MODE**: Current playback mode indicator
- **Se**: SysEx indicator (shown when file contains SysEx messages)

**Playback Modes** (select with LEFT/RIGHT when "MODE" option active):
- **SNG** (Single) - Play once, then stop
- **NXT** (Auto-Next) - Play all tracks in sequence
- **LP1** (Loop One) - Repeat current track
- **LPA** (Loop All) - Loop all tracks

**Menu Options** (navigate with LEFT/RIGHT, activate with OK):

1. **TRACK** - Open file browser
2. **BPM** - Adjust tempo with 0.01 BPM precision (40.00-300.00 BPM range)
   - Press OK to activate whole number editing (underline shows under whole number)
   - LEFT/RIGHT adjusts by ±1.00 BPM
   - Press OK again to switch to decimal editing (underline moves to decimals)
   - LEFT/RIGHT adjusts by ±0.01 BPM
   - Press OK again to deactivate
   - Hold OK 2s to reset to saved config BPM (or file default if no config)
3. **TAP** - Tap tempo - tap LEFT or RIGHT button rhythmically to set BPM
4. **MODE** - Change playback mode
5. **TIME** - Fast forward/rewind 1 second per press
6. **PREV** (◄) - Skip to previous track
7. **NEXT** (►) - Skip to next track

**Tap Tempo Usage:**
- Navigate to TAP and press OK to activate (button shows inverted)
- Tap LEFT or RIGHT button rhythmically (minimum 2 taps)
- BPM updates immediately based on your tapping
- Each tap refines the tempo (uses average of last 4 taps)
- Press OK again to deactivate
- Auto-resets after 2 seconds of inactivity

---

## File Browser

Access: Press OK on "TRACK" option from playback screen.

**Display:**
```
♪ MIDI Files
> SONG01.MID
  SONG02.MID
```

**Controls:**
- **LEFT/RIGHT** - Navigate files
- **OK** - Load file and return to playback screen
- **PLAY** - Load file and start playing immediately
- **MODE** - Cancel and return

**File Organization:**
- Place MIDI files (.mid, .midi) in `/MIDI` folder on SD card
- Files sorted alphabetically
- Folder created automatically if missing

---

## Channel Settings

Per-channel MIDI configuration. Access: Press MODE from playback screen.

**Display:**
```
CH. [SAVE] [DEL]     Pa:64
Ch: 1  M:OF  S:OF  Tr: +0
P:--  Ve:--  Vo:100
```

**Options:**
- **[SAVE]** - Save settings to `.cfg` file (LEFT/RIGHT for YES/NO, OK to confirm)
- **[DEL]** - Delete `.cfg` file and revert to defaults
- **Ch: 1** - Select channel (1-16)
- **M:OF** - Mute status (OF=unmuted, ON=muted)
- **S:OF** - Solo status (OF=not solo, ON=solo). When any channel is solo, all non-solo channels are muted
- **Tr: +0** - Transpose in semitones (-24 to +24, 0=no transpose)
- **P:--** - Program/Instrument (--=MIDI file default, 0-127=override)
- **Ve:--** - Per-channel velocity scale (--=use global, 1-200, 100=normal)
- **Vo:100** - Volume (--=MIDI default, 0-127=set level)
- **Pa:64** - Pan (--=MIDI default, 0=left, 64=center, 127=right)

**Settings Saved:**
All 16 channels: mutes, programs, pan, volume, and global velocity

**Override Behavior:**
When you set a value other than "--", your setting overrides the MIDI file. The file's Program Change, CC7 (Volume), and CC10 (Pan) messages are ignored.

---

## Track Settings

Global track configuration. Access: Press MODE from Channel Settings.

**Display:**
```
TRCK [SAVE] [DEL]
BPM:120.50  Ve:50
SysEx: ON
```

**Options:**
- **[SAVE]** - Save all settings (channel + track) to `.cfg` file
- **[DEL]** - Delete `.cfg` file
- **BPM** - Adjust tempo with 0.01 BPM precision (same as playback screen)
  - Press OK to activate whole number editing (underline under whole number)
  - LEFT/RIGHT adjusts by ±1.00 BPM
  - Press OK to switch to decimal editing (underline under decimals)
  - LEFT/RIGHT adjusts by ±0.01 BPM
  - Press OK again to deactivate
- **Ve** - Global velocity (1-100, 50=normal)
- **SysEx** - Enable/disable System Exclusive messages (ON/OFF)

**Velocity Hierarchy:**
- **Global Velocity (Ve)** - Affects all notes on all channels (1-100, 50=normal)
- **Per-Channel Velocity (Ch. Ve)** - Multiplies global velocity per channel (--=use global, 1-200, 100=normal)
- **Volume (Vo)** - Per-channel CC7 volume control (--=MIDI default, 0-127)

**Use Cases:**
- Use **Global Velocity** to balance loud/quiet tracks overall
- Use **Per-Channel Velocity** to adjust instrument balance within a song
- Use **Volume** to set fixed volume levels per channel

**SysEx Messages:**
System Exclusive (SysEx) messages are device-specific MIDI commands. Some MIDI files (especially MT-32 game soundtracks) contain SysEx messages that change instrument parameters like tuning or patch settings.

**When to Disable SysEx:**
- If a file causes detuning or weird sounds that persist between songs
- If you want to prevent device-specific modifications
- When using a different synth than the file was designed for

**Note:** SysEx is enabled by default. Disable only if experiencing issues.

---

## MIDI Settings

MIDI input configuration. Access: Press MODE from Track Settings.

**Display:**
```
MIDI IN
Thru:      [OFF]
Keyboard:  [OFF]
Kbd Ch:      1
Kbd V:      50
```

**Options:**
- **Thru** - Pass MIDI input to output (ON/OFF)
- **Keyboard** - Enable keyboard mode (ON/OFF)
- **Kbd Ch** - Keyboard channel (1-16, active when Keyboard=ON)
- **Kbd V** - Keyboard velocity scaling (1-100, 50=normal, active when Keyboard=ON)

**Note:** Thru and Keyboard modes are mutually exclusive.

---

## Clock Settings

MIDI Clock output configuration. Access: Press MODE from MIDI Settings.

**Display:**
```
MIDI CLOCK
Enabled:   [OFF]
```

**Option:**
- **Enabled** - Send MIDI Clock/Start/Stop/Continue messages (ON/OFF)

**Behavior (when enabled):**
- Sends 24 clock pulses per quarter note during playback
- Sends Start when playing from beginning
- Sends Stop when stopped
- Sends Continue when resuming from pause

---

## Visualizer

Real-time 16-channel activity display. Access: Press MODE from Clock Settings.

**Display:**
```
 1  2  3  4  5  6  7  8
█  ▆  █  ▄     ▂  █  ▅
 9 10 11 12 13 14 15 16
▃     █  ▆  ▂     ▄  █
```

**What It Shows:**
- 16 columns (channels 1-16)
- Bar height = note activity (scaled by velocity × expression)
- Peak indicator = highest recent activity (holds briefly with decay)
- Animated bubbles = rising through active bars for visual interest
- Baseline markers = channel position reference

**Animation:**
The visualizer features animated bubbles that float upward through active channel bars. Each channel has 2 bubbles at different speeds, creating a dynamic visual effect that helps distinguish active channels.

**Use Cases:**
- Verify file playback
- Identify active channels
- Troubleshoot silent channels
- Visual feedback during setup
- Monitor channel activity and expression changes in real-time

**Note:** Channel 10 typically represents drums/percussion in General MIDI.

---

## Troubleshooting

### No Sound
1. Check MIDI cable connections
2. Verify channel isn't muted (M:OF = unmuted)
3. Check visualizer - do bars appear?
4. Verify volume (Vo) isn't 0
5. Delete `.cfg` file and reload

### Stuck Notes
- Press PANIC button
- Check MIDI cable quality
- Verify synth receives on correct channels

### File Won't Load
- Ensure file is in `/MIDI` folder
- Check extension is `.mid` or `.midi`
- Use shorter filename
- Verify SD card is FAT32

### Settings Not Saving
- Ensure SD card isn't write-protected
- Check free space
- Try different SD card
- Avoid special characters in filenames

### Fast Forward/Rewind Issues
- Slower on very long files (50,000 event limit per seek)
- Device pauses during seeking to prevent MIDI errors

---

## Settings Files (.cfg)

Per-file settings saved as `SONGNAME.cfg` in same folder as MIDI file.

**Format:** Plain text key=value pairs

**Example:**
```
[MIDI_SETTINGS_V1]
MUTES=0
SOLOS=0
PROGRAMS=128,128,128,0,128,128,128,128,128,128,128,128,128,128,128,128
VOLUMES=255,255,255,100,255,255,255,255,255,255,255,255,255,255,255,255
PAN=255,255,255,64,255,255,255,255,255,255,255,255,255,255,255,255
TRANSPOSE=0,0,0,12,0,0,0,0,0,0,0,0,0,0,0,0
ROUTING=255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
CH_VELOCITY=0,0,0,120,0,0,0,0,0,0,0,0,0,0,0,0
VELOCITY_SCALE=50
TARGET_BPM=12050
USE_TARGET_BPM=1
SYSEX_ENABLED=1
```

**Values:**
- **MUTES**: Bitmask (0=all unmuted, bit 0=ch1, bit 1=ch2, etc.)
- **SOLOS**: Bitmask (0=no solos, bit 0=ch1, bit 1=ch2, etc.)
- **PROGRAMS**: 0-127 (specific), 128 (use MIDI default)
- **VOLUMES**: 0-127 (specific), 255 (use MIDI default)
- **PAN**: 0-127 (specific, 0=left, 64=center, 127=right), 255 (use MIDI default)
- **TRANSPOSE**: -24 to +24 semitones (0=no transpose)
- **ROUTING**: 0-15 (route to channel), 255 (use original channel)
- **CH_VELOCITY**: 0 (use global), 1-200 (per-channel scale, 100=normal)
- **VELOCITY_SCALE**: 1-100 (global velocity, 50=normal)
- **TARGET_BPM**: BPM in hundredths (12050 = 120.50 BPM, range: 4000-30000)
- **USE_TARGET_BPM**: 0 (use file default), 1 (use TARGET_BPM value)
- **SYSEX_ENABLED**: 0 (disabled), 1 (enabled)

**Manual Editing:**
Remove SD card, edit `.cfg` with text editor, save, re-insert. Settings apply on next load.

---

## Tips & Tricks

**Fast Navigation:**
- PLAY from file browser loads and plays immediately
- Hold MODE 2s to jump to playback screen from anywhere
- Use NXT mode for continuous playback
- Use LPA mode for background music

**Balancing Tracks:**
- Use Velocity (Ve) to balance loud/quiet tracks (try 30-40 for loud, 60-70 for quiet)
- Save settings per-file for consistent playback

**Creating Custom Mixes:**
- Mute channels for karaoke tracks
- Adjust Volume (Vo) to balance instruments
- Use Pan to create stereo image
- Use program to change instruments for individual tracks (If your sound device supports it.)

**Tempo Control:**
- Use TAP tempo for quick BPM matching (great for live performance)
- Tap along with a metronome or reference track
- Fine-tune with BPM option after tapping (0.01 BPM precision)
- Edit whole numbers (±1.00 BPM) or decimals (±0.01 BPM) separately for precise control
- Perfect for syncing to other devices or matching specific tempos
- Tempo changes persist until you reload the file (not auto-saved)
- Save to config file to make tempo changes permanent for that track
- Hold OK 2s on BPM to reset to saved config tempo (or file default if no config)
- Press STOP then PLAY to reload config settings and resume playback

**Advanced Mixing:**
- Use Solo (S) to isolate channels while mixing
- Use Transpose (Tr) to fix out-of-range notes or create harmonies
- Use Per-Channel Velocity to balance loud/quiet instruments
- Use Routing to merge multiple channels to one output

**SysEx Issues:**
- If synth sounds wrong after a file, disable SysEx for that file
- SysEx settings are saved per-file
- MT-32/SC-55 users: Some game soundtracks include instrument patches via SysEx

**Testing Changes:**
- Use visualizer to verify activity and expression changes
- Watch for bubble animation to confirm channel activity
- PANIC silences stuck notes
- STOP + PLAY reloads `.cfg` settings

---

## Recommendations

**SD Card:**
- Format: FAT32
- Speed: Class 10+
- Size: 2-32GB
- Quality: Use reputable brands

**MIDI Files:**
- Format: Type 0 or Type 1
- Resolution: 96-480 PPQN
- Size: Under 1MB preferred
- Tempo: Embedded tempo changes supported

---

## Technical Specifications

**MIDI Implementation:**
- Standard MIDI 1.0, 31,250 bps
- All 16 channels supported
- Messages: Note On/Off, Program Change, Control Change, Pitch Bend, Aftertouch, SysEx
- MT-32 compatible (35ms SysEx delay)

**Hardware:**
- Processor: Raspberry Pi Pico (RP2040, dual-core @ 133MHz)
- Core 0: UI, display, files
- Core 1: MIDI timing (microsecond precision)
- Storage: MicroSD (FAT32, up to 32GB SDHC)
- Display: 128x32 OLED (SSD1306, I2C)

**Timing:**
- Microsecond-precision MIDI
- Tempo range: 50-200%
- Clock: 24 PPQN

---

## Version Information

**Firmware:** 1.0
**Manual:** 1.0
**Updated:** 2025

For support, bug reports, or feature requests, visit the project repository.

---

**End of Manual**
