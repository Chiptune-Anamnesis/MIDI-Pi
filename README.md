# MIDI-PI - Portable MIDI File Player

Standalone MIDI file player for Raspberry Pi Pico (RP2040). Plays Standard MIDI Files from SD card through TRS 3.5mm MIDI output.

## Key Features

- **Playback**: Format 0/1 MIDI files, all 16 channels, unlimited file length
- **Precise BPM Control**: 0.01 BPM precision (40.00-300.00), tap tempo, separate whole/decimal editing
- **Playback Modes**: Single, Auto-Next, Loop One, Loop All
- **Per-File Settings**: Save/load channel configurations, tempo, velocity
- **Channel Mixer**: 16-channel mute/solo, program/pan/volume/transpose override, routing
- **Real-time Visualizer**: 16-channel animated VU meters with bubbles
- **MIDI I/O**: Hardware UART, MIDI Thru, Keyboard mode, Clock output
- **Dual-Core**: UI on Core 0, MIDI timing on Core 1 (microsecond precision)

## Hardware

### Components
- Raspberry Pi Pico (RP2040)
- MicroSD card reader (SPI, FAT32)
- 128x32 OLED (I2C, SSD1306)
- 7 buttons (PLAY, STOP, OK, LEFT, RIGHT, MODE, PANIC)
- MIDI TRS 3.5mm jacks (in/out) with 220Ω resistors

### Pin Connections

**OLED (I2C):** SDA=GP8, SCL=GP9
**SD Card (SPI):** CS=GP5, MISO=GP4, SCK=GP6, MOSI=GP7
**MIDI Output:** TX=GP0 (UART0)
**MIDI Input:** RX=GP5 (UART1)
**Buttons:** PLAY=GP19, STOP=GP17, OK=GP15, LEFT=GP16, RIGHT=GP20, MODE=GP24, PANIC=GP18

### MIDI Circuit (TRS Type A)

**Output:**
```
GP0 (TX) --[220Ω]--> Tip (Signal)
3.3V -----[220Ω]--> Ring (Current)
GND -----------------> Sleeve (Ground)
```

**Input:**
```
Tip ---> GP5 (RX) via optocoupler (6N138)
Ring --> 3.3V via 220Ω
Sleeve -> GND
```

## Quick Start

1. **SD Card Setup**: Format as FAT32, create `/MIDI` folder, add `.mid` files
2. **Hardware**: Connect components per pin layout above
3. **Build**: `pio run`
4. **Upload**: `pio run --target upload`
5. **Use**: Navigate with LEFT/RIGHT, load with OK, play with PLAY

## SD Card Structure

```
/MIDI/
  ├── song1.mid
  ├── song1.cfg         (auto-saved settings)
  ├── Artist1/
  │   └── track.mid
  └── ...
```

## Basic Controls

**File Browser:**
- LEFT/RIGHT: Navigate files/folders
- OK: Load file
- PLAY: Load and play immediately

**Playback Screen:**
- PLAY: Play/pause
- STOP: Stop playback
- MODE: Cycle menus (Channel Settings → Track Settings → MIDI Settings → Clock → Visualizer)
- LEFT/RIGHT: Navigate options
- OK: Activate/edit option
- Hold OK (2s): Reset option to default

**Menu Options:**
- TRACK, BPM, TAP, MODE, TIME, PREV/NEXT
- Per-channel: Mute, Solo, Program, Pan, Volume, Transpose, Velocity, Routing
- Global: Velocity scale, SysEx enable/disable, MIDI Thru, Keyboard mode, Clock output

See [USER MANUAL](MIDI-PI_USER_MANUAL.md) for detailed operation.

## Building

### PlatformIO (Recommended)
```bash
pio run               # Build
pio run -t upload     # Upload
```

### Arduino IDE
1. Install `arduino-pico` core
2. Install libraries: Adafruit GFX, Adafruit SSD1306, SdFat, MIDI Library
3. Select "Raspberry Pi Pico" board
4. Upload

## Troubleshooting

**No SD Card:** Check SPI wiring, ensure FAT32, try different card
**Files Won't Load:** Reboot device, check SD card errors
**No MIDI Output:** Verify TX=GP0, check 220Ω resistors, test channel mutes
**Display Issues:** Check I2C (SDA=GP8, SCL=GP9), verify 0x3C address
**Buttons Bouncy:** Increase `BUTTON_DEBOUNCE` in InputHandler.h

## Technical Specs

- **MIDI Timing**: Microsecond precision (±2μs)
- **Display Refresh**: 60Hz (visualizer), 10Hz (UI), 2Hz (idle)
- **Tempo Range**: 40.00-300.00 BPM (0.01 precision)
- **Supported Formats**: Standard MIDI Format 0/1
- **Max File Length**: Unlimited (tested 47+ minutes)
- **Channels**: All 16 MIDI channels

## License

Open source. Modify and distribute freely.

## Credits

Built with: arduino-pico, Adafruit GFX/SSD1306, SdFat, FortySevenEffects MIDI Library
