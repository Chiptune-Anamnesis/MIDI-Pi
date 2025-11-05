#ifndef PINS_H
#define PINS_H

// OLED Display Pins (I2C)
#define OLED_SDA_PIN 8
#define OLED_SCL_PIN 9
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_ADDRESS 0x3C

// SD Card Pins (SPI)
#define SD_CS_PIN 5
#define SD_MISO_PIN 4
#define SD_SCK_PIN 6
#define SD_MOSI_PIN 7

// MIDI Pins (UART0 / Serial1)
// MIDI IN and OUT share the same UART (GP0=TX OUT, GP1=RX IN)
#define MIDI_TX_PIN 0     // GP0 - MIDI OUT transmit
#define MIDI_RX_PIN 1     // GP1 - MIDI IN receive (shared UART)
#define MIDI_BAUD_RATE 31250

// Button Pins (new PCB layout - no encoder)
#define BTN_PLAY_PIN 19         // GP19 - Play button
#define BTN_STOP_PIN 17         // GP17 - Stop button
#define BTN_LEFT_PIN 16         // GP16 - Left/Previous button
#define BTN_RIGHT_PIN 20        // GP20 - Right/Next button
#define BTN_MODE_PIN 14         // GP14 - Mode/Menu button
#define BTN_OK_PIN 15           // GP15 - OK/Enter button
#define BTN_PANIC_PIN 18        // GP18 - MIDI Panic button

#endif // PINS_H
