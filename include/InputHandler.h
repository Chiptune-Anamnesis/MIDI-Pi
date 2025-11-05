#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <Arduino.h>

enum Button {
    BTN_NONE = 0,
    BTN_PLAY,       // GP19 - Play button
    BTN_STOP,       // GP17 - Stop button
    BTN_LEFT,       // GP16 - Left/Previous button
    BTN_RIGHT,      // GP20 - Right/Next button
    BTN_MODE,       // GP24 - Mode/Menu button
    BTN_OK,         // GP15 - OK/Enter button
    BTN_PANIC       // GP18 - MIDI Panic button
};

class InputHandler {
public:
    InputHandler();
    void begin();
    Button readButton();
    Button readButtonWithRepeat();  // For held button acceleration
    bool isButtonHeld(Button btn);  // Check if a specific button is currently held

private:
    // Debouncing for individual buttons
    unsigned long lastPlayTime;
    unsigned long lastStopTime;
    unsigned long lastLeftTime;
    unsigned long lastRightTime;
    unsigned long lastModeTime;
    unsigned long lastOkTime;
    unsigned long lastPanicTime;

    // Button states for edge detection
    bool lastPlayState;
    bool lastStopState;
    bool lastLeftState;
    bool lastRightState;
    bool lastModeState;
    bool lastOkState;
    bool lastPanicState;

    // Button hold and repeat state
    Button currentHeldButton;
    unsigned long buttonHoldStartTime;
    unsigned long lastRepeatTime;
    uint16_t repeatDelay;  // Dynamic repeat delay with acceleration

    // Timing constants
    static const uint16_t BUTTON_DEBOUNCE = 150;     // 150ms debounce for MX switches (prevent accidental double-press)
    static const uint16_t HOLD_THRESHOLD = 250;      // 250ms before repeat starts
    static const uint16_t REPEAT_DELAY_INITIAL = 80;  // Initial repeat delay (ms)
    static const uint16_t REPEAT_DELAY_FAST = 30;     // Fast repeat delay (ms)
    static const uint16_t REPEAT_DELAY_FASTEST = 10;  // Fastest repeat delay (ms)
    static const uint16_t ACCEL_THRESHOLD_1 = 500;    // Time to reach fast speed
    static const uint16_t ACCEL_THRESHOLD_2 = 1000;   // Time to reach fastest speed
};

#endif // INPUT_HANDLER_H
