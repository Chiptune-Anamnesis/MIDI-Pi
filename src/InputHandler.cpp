#include "InputHandler.h"
#include "pins.h"

InputHandler::InputHandler() {
    lastPlayTime = 0;
    lastStopTime = 0;
    lastLeftTime = 0;
    lastRightTime = 0;
    lastModeTime = 0;
    lastOkTime = 0;
    lastPanicTime = 0;

    lastPlayState = HIGH;
    lastStopState = HIGH;
    lastLeftState = HIGH;
    lastRightState = HIGH;
    lastModeState = HIGH;
    lastOkState = HIGH;
    lastPanicState = HIGH;

    // Initialize hold/repeat state
    currentHeldButton = BTN_NONE;
    buttonHoldStartTime = 0;
    lastRepeatTime = 0;
    repeatDelay = REPEAT_DELAY_INITIAL;
}

void InputHandler::begin() {
    // Initialize all button pins with internal pullup resistors
    pinMode(BTN_PLAY_PIN, INPUT_PULLUP);
    pinMode(BTN_STOP_PIN, INPUT_PULLUP);
    pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
    pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
    pinMode(BTN_OK_PIN, INPUT_PULLUP);
    pinMode(BTN_PANIC_PIN, INPUT_PULLUP);
}

Button InputHandler::readButton() {
    unsigned long currentTime = millis();

    // Check all buttons with debouncing and edge detection
    // Buttons are active LOW (pressed = LOW, released = HIGH)

    // Play Button (GP19)
    bool playState = digitalRead(BTN_PLAY_PIN);
    if (playState == LOW && lastPlayState == HIGH) {
        if (currentTime - lastPlayTime >= BUTTON_DEBOUNCE) {
            lastPlayTime = currentTime;
            lastPlayState = playState;
            return BTN_PLAY;
        }
    }
    lastPlayState = playState;

    // Stop Button (GP17)
    bool stopState = digitalRead(BTN_STOP_PIN);
    if (stopState == LOW && lastStopState == HIGH) {
        if (currentTime - lastStopTime >= BUTTON_DEBOUNCE) {
            lastStopTime = currentTime;
            lastStopState = stopState;
            return BTN_STOP;
        }
    }
    lastStopState = stopState;

    // Left Button (GP16)
    bool leftState = digitalRead(BTN_LEFT_PIN);
    if (leftState == LOW && lastLeftState == HIGH) {
        if (currentTime - lastLeftTime >= BUTTON_DEBOUNCE) {
            lastLeftTime = currentTime;
            lastLeftState = leftState;
            return BTN_LEFT;
        }
    }
    lastLeftState = leftState;

    // Right Button (GP20)
    bool rightState = digitalRead(BTN_RIGHT_PIN);
    if (rightState == LOW && lastRightState == HIGH) {
        if (currentTime - lastRightTime >= BUTTON_DEBOUNCE) {
            lastRightTime = currentTime;
            lastRightState = rightState;
            return BTN_RIGHT;
        }
    }
    lastRightState = rightState;

    // Mode Button (GP24)
    bool modeState = digitalRead(BTN_MODE_PIN);
    if (modeState == LOW && lastModeState == HIGH) {
        if (currentTime - lastModeTime >= BUTTON_DEBOUNCE) {
            lastModeTime = currentTime;
            lastModeState = modeState;
            return BTN_MODE;
        }
    }
    lastModeState = modeState;

    // OK Button (GP15)
    bool okState = digitalRead(BTN_OK_PIN);
    if (okState == LOW && lastOkState == HIGH) {
        if (currentTime - lastOkTime >= BUTTON_DEBOUNCE) {
            lastOkTime = currentTime;
            lastOkState = okState;
            return BTN_OK;
        }
    }
    lastOkState = okState;

    // Panic Button (GP18)
    bool panicState = digitalRead(BTN_PANIC_PIN);
    if (panicState == LOW && lastPanicState == HIGH) {
        if (currentTime - lastPanicTime >= BUTTON_DEBOUNCE) {
            lastPanicTime = currentTime;
            lastPanicState = panicState;
            return BTN_PANIC;
        }
    }
    lastPanicState = panicState;

    return BTN_NONE;
}

Button InputHandler::readButtonWithRepeat() {
    unsigned long currentTime = millis();

    // First check for new button press (edge detection)
    Button pressedButton = readButton();

    if (pressedButton != BTN_NONE) {
        // New button pressed
        currentHeldButton = pressedButton;
        buttonHoldStartTime = currentTime;
        lastRepeatTime = currentTime;
        repeatDelay = REPEAT_DELAY_INITIAL;
        return pressedButton;
    }

    // Check if a button is currently being held
    Button heldButton = BTN_NONE;
    if (digitalRead(BTN_LEFT_PIN) == LOW) heldButton = BTN_LEFT;
    else if (digitalRead(BTN_RIGHT_PIN) == LOW) heldButton = BTN_RIGHT;
    else if (digitalRead(BTN_PLAY_PIN) == LOW) heldButton = BTN_PLAY;
    else if (digitalRead(BTN_STOP_PIN) == LOW) heldButton = BTN_STOP;
    else if (digitalRead(BTN_MODE_PIN) == LOW) heldButton = BTN_MODE;
    else if (digitalRead(BTN_OK_PIN) == LOW) heldButton = BTN_OK;
    else if (digitalRead(BTN_PANIC_PIN) == LOW) heldButton = BTN_PANIC;

    if (heldButton == BTN_NONE) {
        currentHeldButton = BTN_NONE;
        return BTN_NONE;
    }

    // Button is being held - check if it's the same button that was initially pressed
    if (heldButton != currentHeldButton) {
        currentHeldButton = BTN_NONE;
        return BTN_NONE;
    }

    // Only allow repeat for LEFT and RIGHT buttons
    if (heldButton != BTN_LEFT && heldButton != BTN_RIGHT) {
        // Other buttons don't repeat - only fire once on initial press
        return BTN_NONE;
    }

    unsigned long holdDuration = currentTime - buttonHoldStartTime;

    // Wait for hold threshold before starting repeats
    if (holdDuration < HOLD_THRESHOLD) {
        return BTN_NONE;
    }

    // Calculate acceleration based on hold duration
    if (holdDuration > ACCEL_THRESHOLD_2) {
        repeatDelay = REPEAT_DELAY_FASTEST;  // Fastest after 2 seconds
    } else if (holdDuration > ACCEL_THRESHOLD_1) {
        repeatDelay = REPEAT_DELAY_FAST;     // Fast after 1 second
    } else {
        repeatDelay = REPEAT_DELAY_INITIAL;  // Initial speed
    }

    // Check if enough time has passed for next repeat
    if (currentTime - lastRepeatTime >= repeatDelay) {
        lastRepeatTime = currentTime;
        return heldButton;
    }

    return BTN_NONE;
}

bool InputHandler::isButtonHeld(Button btn) {
    switch (btn) {
        case BTN_PLAY:  return digitalRead(BTN_PLAY_PIN) == LOW;
        case BTN_STOP:  return digitalRead(BTN_STOP_PIN) == LOW;
        case BTN_LEFT:  return digitalRead(BTN_LEFT_PIN) == LOW;
        case BTN_RIGHT: return digitalRead(BTN_RIGHT_PIN) == LOW;
        case BTN_MODE:  return digitalRead(BTN_MODE_PIN) == LOW;
        case BTN_OK:    return digitalRead(BTN_OK_PIN) == LOW;
        case BTN_PANIC: return digitalRead(BTN_PANIC_PIN) == LOW;
        default:        return false;
    }
}
