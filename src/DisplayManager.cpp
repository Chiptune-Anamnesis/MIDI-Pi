#include "DisplayManager.h"
#include "pins.h"
#include <Wire.h>

DisplayManager::DisplayManager() : display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {
    currentMode = MODE_FILE_BROWSER;
    scrollOffset = 0;
    lastScrollTime = 0;
    lastBubbleUpdate = 0;

    // Initialize bubbles with random positions and speeds
    for (uint8_t ch = 0; ch < 16; ch++) {
        for (uint8_t b = 0; b < 2; b++) {
            bubbles[ch][b].y = (ch * 7 + b * 13) % 32;  // Pseudo-random starting positions
            bubbles[ch][b].speed = 0.3f + ((ch + b) % 3) * 0.15f;  // Vary speeds: 0.3, 0.45, 0.6
        }
    }
}

bool DisplayManager::begin() {
    Wire.setSDA(OLED_SDA_PIN);
    Wire.setSCL(OLED_SCL_PIN);
    Wire.setClock(400000);
    Wire.begin();

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        return false;
    }

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.clearDisplay();
    display.display();

    return true;
}

void DisplayManager::clear() {
    display.clearDisplay();
    display.display();
}

void DisplayManager::setMode(DisplayMode mode) {
    currentMode = mode;
}

void DisplayManager::update() {
    display.display();
}

void DisplayManager::showMessage(const char* line1, const char* line2) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(line1);
    if (line2) {
        display.setCursor(0, 16);
        display.println(line2);
    }
    display.display();
}

void DisplayManager::showError(const char* error) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ERROR:");
    display.setCursor(0, 16);
    display.println(error);
    display.display();
}

void DisplayManager::showConfirmation(const char* message, bool yesSelected) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print(message);

    int16_t y = 18;

    if (!yesSelected) {
        display.fillRect(24, y - 1, 18, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    }
    display.setCursor(28, y);
    display.print("NO");
    display.setTextColor(SSD1306_WHITE);

    if (yesSelected) {
        display.fillRect(80, y - 1, 24, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    }
    display.setCursor(84, y);
    display.print("YES");
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showFileBrowser(FileBrowser* browser) {
    if (!browser) return;

    display.clearDisplay();
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print(browser->getCurrentIndex() + 1);
    display.print("/");
    display.print(browser->getFileCount());
    display.print(" ");

    FileEntry* current = browser->getCurrentFile();
    if (current) {
        if (current->isDirectory) {
            display.print("[D]");
        }

        int remainingWidth = 21 - 6;
        if (strlen(current->filename) > remainingWidth) {
            char truncated[16];
            strncpy(truncated, current->filename, remainingWidth - 3);
            truncated[remainingWidth - 3] = '\0';
            strcat(truncated, "...");
            display.print(truncated);
        } else {
            display.print(current->filename);
        }
    }

    display.setCursor(0, 12);
    display.print("OK:Select MODE:Back");

    display.setCursor(0, 24);
    const char* path = browser->getCurrentPath();
    if (strlen(path) > 21) {
        display.print("...");
        display.print(path + strlen(path) - 18);
    } else {
        display.print(path);
    }

    display.display();
}

void DisplayManager::formatTime(uint32_t milliseconds, char* buffer) {
    uint32_t seconds = milliseconds / 1000;
    uint32_t minutes = seconds / 60;
    seconds = seconds % 60;

    sprintf(buffer, "%02d:%02d", minutes, seconds);
}

void DisplayManager::drawProgressBar(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t progress) {
    // Draw border
    display.drawRect(x, y, width, height, SSD1306_WHITE);

    // Fill progress
    uint16_t fillWidth = (width - 2) * progress / 100;
    if (fillWidth > 0) {
        display.fillRect(x + 1, y + 1, fillWidth, height - 2, SSD1306_WHITE);
    }
}

void DisplayManager::showPlayback(const PlaybackInfo& info) {
    display.clearDisplay();
    display.setTextSize(1);

    // Line 1 (0-9): Scrolling song name - MENU_TRACK option
    bool trackHighlighted = (info.selectedOption == MENU_TRACK);

    // Draw highlight box around song name if selected
    if (trackHighlighted && info.optionActive) {
        // Active - draw filled white box
        display.fillRect(0, 0, OLED_WIDTH, 10, SSD1306_WHITE);
        // Set text to black so it's visible on white background
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        drawScrollingText(info.songName, 1, OLED_WIDTH - 2);
        display.setTextColor(SSD1306_WHITE); // Reset to normal
    } else {
        // Not active - just draw outline if highlighted
        if (trackHighlighted) {
            display.drawRect(0, 0, OLED_WIDTH, 10, SSD1306_WHITE);
        }
        drawScrollingText(info.songName, 1, OLED_WIDTH - 2);
    }

    // Line 2 (12-21): BPM and Velocity options
    int16_t y = 13;

    // BPM option - with "BPM:" label
    int16_t bpmX = 0;
    bool bpmHighlighted = (info.selectedOption == MENU_BPM);

    // Display format: "BPM:120.50" (targetBPM is in hundredths)
    char bpmText[16];
    uint16_t wholeBPM = info.targetBPM / 100;
    uint8_t decimalBPM = info.targetBPM % 100;
    sprintf(bpmText, "BPM:%d.%02d", wholeBPM, decimalBPM);
    int16_t bpmWidth = strlen(bpmText) * 6 + 4;

    if (bpmHighlighted && info.optionActive) {
        // Active - draw filled box with inverse text
        display.fillRect(bpmX, y - 1, bpmWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.setCursor(bpmX + 1, y);
        display.print(bpmText);
        display.setTextColor(SSD1306_WHITE); // Reset to normal

        // Draw underline indicator to show which part is being edited
        // Calculate position of whole vs decimal in "BPM:120.50"
        // "BPM:" = 4 chars, whole number varies (1-3 digits), "." = 1 char, decimal = 2 chars
        int16_t wholeStartX = bpmX + 1 + (4 * 6); // After "BPM:" (4 chars × 6 pixels)
        int16_t wholeDigits = (wholeBPM >= 100) ? 3 : (wholeBPM >= 10) ? 2 : 1;
        int16_t wholeWidth = wholeDigits * 6; // 6 pixels per character
        int16_t decimalStartX = wholeStartX + wholeWidth + 6; // After whole + "."
        int16_t decimalWidth = 2 * 6; // 2 decimal digits × 6 pixels

        // Draw a small line beneath the active part (in black since background is white)
        if (info.bpmEditingWhole) {
            // Underline whole number
            display.drawFastHLine(wholeStartX, y + 7, wholeWidth, SSD1306_BLACK);
        } else {
            // Underline decimal
            display.drawFastHLine(decimalStartX, y + 7, decimalWidth, SSD1306_BLACK);
        }
    } else {
        // Not active - just draw outline if highlighted
        if (bpmHighlighted) {
            display.drawRect(bpmX, y - 1, bpmWidth, 9, SSD1306_WHITE);
        }
        display.setCursor(bpmX + 1, y);
        display.print(bpmText);
    }

    // TAP option (tap tempo button)
    int16_t tapX = bpmX + bpmWidth + 6;
    bool tapHighlighted = (info.selectedOption == MENU_TAP);

    // Display as "TAP" button
    const char* tapText = "TAP";
    int16_t tapWidth = 22; // Fixed width for TAP

    if (tapHighlighted && info.optionActive) {
        // Active - draw filled box with inverse text
        display.fillRect(tapX, y - 1, tapWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.setCursor(tapX + 1, y);
        display.print(tapText);
        display.setTextColor(SSD1306_WHITE); // Reset to normal
    } else {
        // Not active - just draw outline if highlighted
        if (tapHighlighted) {
            display.drawRect(tapX, y - 1, tapWidth, 9, SSD1306_WHITE);
        }
        display.setCursor(tapX + 1, y);
        display.print(tapText);
    }

    // PLAYBACK MODE option
    int16_t modeX = tapX + tapWidth + 6;
    bool modeHighlighted = (info.selectedOption == MENU_MODE);

    // Display mode: "SNG", "NXT", "LP1", "LPA"
    const char* modeText;
    switch (info.playbackMode) {
        case PLAYBACK_SINGLE: modeText = "SNG"; break;
        case PLAYBACK_AUTO_NEXT: modeText = "NXT"; break;
        case PLAYBACK_LOOP_ONE: modeText = "LP1"; break;
        case PLAYBACK_LOOP_ALL: modeText = "LPA"; break;
        default: modeText = "SNG"; break;
    }
    int16_t modeWidth = 22; // Fixed width for 3 characters

    if (modeHighlighted && info.optionActive) {
        // Active - draw filled box with inverse text
        display.fillRect(modeX, y - 1, modeWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.setCursor(modeX + 1, y);
        display.print(modeText);
        display.setTextColor(SSD1306_WHITE); // Reset to normal
    } else {
        // Not active - just draw outline if highlighted
        if (modeHighlighted) {
            display.drawRect(modeX, y - 1, modeWidth, 9, SSD1306_WHITE);
        }
        display.setCursor(modeX + 1, y);
        display.print(modeText);
    }

    // Playback state indicator (after mode) - draw icon
    int16_t stateX = modeX + modeWidth + 3;
    int16_t stateY = y;

    if (info.isPlaying) {
        // Draw play triangle (►)
        display.fillTriangle(stateX, stateY, stateX, stateY + 6, stateX + 4, stateY + 3, SSD1306_WHITE);
    } else if (info.isPaused) {
        // Draw pause bars (❚❚)
        display.fillRect(stateX, stateY, 2, 7, SSD1306_WHITE);
        display.fillRect(stateX + 3, stateY, 2, 7, SSD1306_WHITE);
    } else {
        // Draw stop square (■)
        display.fillRect(stateX, stateY, 5, 7, SSD1306_WHITE);
    }

    // Line 3 (23-31): Time progress and track counter
    int16_t timeY = 24;

    // Time display (left side) - selectable
    bool timeHighlighted = (info.selectedOption == MENU_TIME);
    char timeStr[16];
    formatTime(info.currentTime, timeStr);
    char timeDisplay[16];
    sprintf(timeDisplay, "%s/", timeStr);
    formatTime(info.totalTime, timeStr);
    strcat(timeDisplay, timeStr);

    int16_t timeWidth = strlen(timeDisplay) * 6 + 4;

    if (timeHighlighted && info.optionActive) {
        // Active - draw filled box with inverse text
        display.fillRect(0, timeY - 1, timeWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.setCursor(1, timeY);
        display.print(timeDisplay);
        display.setTextColor(SSD1306_WHITE); // Reset
    } else {
        if (timeHighlighted) {
            display.drawRect(0, timeY - 1, timeWidth, 9, SSD1306_WHITE);
        }
        display.setCursor(1, timeY);
        display.print(timeDisplay);
    }

    // SysEx indicator and Previous/Next song icons (right side)
    // SysEx indicator "Se" (if file has SysEx messages)
    int16_t seX = OLED_WIDTH - 44;
    if (info.sysexCount > 0) {
        display.setCursor(seX, timeY);
        display.print("Se");
    }

    // PREV icon (left arrow/triangle)
    int16_t prevX = OLED_WIDTH - 32;
    int16_t iconY = timeY + 1;
    bool prevHighlighted = (info.selectedOption == MENU_PREV);

    if (prevHighlighted && info.optionActive) {
        display.fillRect(prevX - 1, iconY - 1, 12, 9, SSD1306_WHITE);
        // Draw left-pointing triangle in black
        display.fillTriangle(prevX + 7, iconY, prevX + 7, iconY + 6, prevX + 2, iconY + 3, SSD1306_BLACK);
    } else {
        if (prevHighlighted) {
            display.drawRect(prevX - 1, iconY - 1, 12, 9, SSD1306_WHITE);
        }
        // Draw left-pointing triangle
        display.fillTriangle(prevX + 7, iconY, prevX + 7, iconY + 6, prevX + 2, iconY + 3, SSD1306_WHITE);
    }

    // NEXT icon (right arrow/triangle)
    int16_t nextX = OLED_WIDTH - 16;
    bool nextHighlighted = (info.selectedOption == MENU_NEXT);

    if (nextHighlighted && info.optionActive) {
        display.fillRect(nextX - 1, iconY - 1, 12, 9, SSD1306_WHITE);
        // Draw right-pointing triangle in black
        display.fillTriangle(nextX + 2, iconY, nextX + 2, iconY + 6, nextX + 7, iconY + 3, SSD1306_BLACK);
    } else {
        if (nextHighlighted) {
            display.drawRect(nextX - 1, iconY - 1, 12, 9, SSD1306_WHITE);
        }
        // Draw right-pointing triangle
        display.fillTriangle(nextX + 2, iconY, nextX + 2, iconY + 6, nextX + 7, iconY + 3, SSD1306_WHITE);
    }

    display.display();
}

void DisplayManager::showSettings(uint16_t settingIndex, const char* label, const char* value) {
    display.clearDisplay();

    display.setCursor(0, 0);
    display.println("SETTINGS");

    display.setCursor(0, 12);
    display.print("> ");
    display.println(label);

    display.setCursor(10, 22);
    display.println(value);

    display.display();
}

void DisplayManager::drawScrollingText(const char* text, int16_t y, int16_t maxWidth) {
    int16_t textWidth = strlen(text) * 6; // 6 pixels per character in size 1

    if (textWidth <= maxWidth) {
        // Text fits, no need to scroll
        display.setCursor(1, y);
        // Use write() instead of print() to preserve text color settings
        for (size_t i = 0; i < strlen(text); i++) {
            display.write(text[i]);
        }
        scrollOffset = 0;
    } else {
        // Text needs scrolling
        unsigned long currentTime = millis();
        if (currentTime - lastScrollTime > SCROLL_DELAY) {
            scrollOffset++;
            if (scrollOffset > textWidth + 20) {
                scrollOffset = 0; // Reset to start
            }
            lastScrollTime = currentTime;
        }

        // Manually draw characters to prevent text wrapping
        int16_t charX = -scrollOffset + 1;
        for (size_t i = 0; i < strlen(text); i++) {
            if (charX >= 0 && charX < maxWidth - 6) {
                display.setCursor(charX, y);
                display.write(text[i]);
            }
            charX += 6;
        }

        // Draw second instance for seamless loop
        charX = -scrollOffset + textWidth + 20 + 1;
        for (size_t i = 0; i < strlen(text); i++) {
            if (charX >= 0 && charX < maxWidth - 6) {
                display.setCursor(charX, y);
                display.write(text[i]);
            }
            charX += 6;
        }
    }
}

void DisplayManager::drawPlayIcon(int16_t x, int16_t y, bool highlighted) {
    // Draw play triangle
    if (highlighted) {
        display.fillTriangle(x, y, x, y + 6, x + 5, y + 3, SSD1306_WHITE);
    } else {
        display.drawTriangle(x, y, x, y + 6, x + 5, y + 3, SSD1306_WHITE);
    }
}

void DisplayManager::drawPauseIcon(int16_t x, int16_t y, bool highlighted) {
    // Draw pause bars
    if (highlighted) {
        display.fillRect(x, y, 2, 7, SSD1306_WHITE);
        display.fillRect(x + 3, y, 2, 7, SSD1306_WHITE);
    } else {
        display.drawRect(x, y, 2, 7, SSD1306_WHITE);
        display.drawRect(x + 3, y, 2, 7, SSD1306_WHITE);
    }
}

void DisplayManager::drawStopIcon(int16_t x, int16_t y, bool highlighted) {
    // Draw stop square
    if (highlighted) {
        display.fillRect(x, y, 6, 6, SSD1306_WHITE);
    } else {
        display.drawRect(x, y, 6, 6, SSD1306_WHITE);
    }
}

void DisplayManager::showChannelMenu(uint8_t selectedChannel, uint16_t channelMutes) {
    // Note: This function signature is kept for compatibility but now shows
    // a different layout - it needs channelPrograms and menuState passed via showProgramMenu
    display.clearDisplay();
    display.setTextSize(1);

    // This is just a placeholder - the actual display is now in showProgramMenu
    display.setCursor(0, 0);
    display.print("CHANNEL SETTINGS");
    display.display();
}
void DisplayManager::showProgramMenu(uint8_t selectedChannel, uint8_t* channelPrograms) {
    display.clearDisplay();
    display.setTextSize(1);

    // Title
    display.setCursor(0, 0);
    display.print("MIDI PROGRAM");

    // Calculate which 8 channels to show based on selection
    uint8_t startChannel = (selectedChannel < 8) ? 0 : 8;
    uint8_t endChannel = startChannel + 8;

    // Display 8 channels in 2 rows of 4
    for (uint8_t ch = startChannel; ch < endChannel; ch++) {
        uint8_t displayIndex = ch - startChannel;
        uint8_t row = displayIndex / 4;
        uint8_t col = displayIndex % 4;

        int16_t x = col * 32;
        int16_t y = 12 + (row * 10);

        uint8_t program = channelPrograms[ch];
        bool isSelected = (ch == selectedChannel);

        // Draw selection box with filled background if selected
        if (isSelected) {
            display.fillRect(x, y, 30, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }

        // Draw channel number and program
        display.setCursor(x + 2, y + 1);

        // Channel number (1-based for display)
        if (ch < 9) {
            display.print(" ");
        }
        display.print(ch + 1);

        display.print(":");

        // Program number (0-127)
        if (program < 10) {
            display.print("  ");
        } else if (program < 100) {
            display.print(" ");
        }
        display.print(program);

        // Reset text color
        display.setTextColor(SSD1306_WHITE);
    }

    display.display();
}

void DisplayManager::showChannelSettingsMenu(uint8_t selectedChannel, uint16_t channelMutes, uint16_t channelSolos, uint8_t* channelPrograms, uint8_t* channelPan, uint8_t* channelVolume, int8_t* channelTranspose, uint8_t* channelVelocity, uint8_t currentOption, bool optionActive) {
    display.clearDisplay();
    display.setTextSize(1);

    // Line 0: Title, Save/Delete buttons, and Pan
    int16_t y0 = 0;
    display.setCursor(0, y0);
    display.print("CH.");

    // Save button
    int16_t saveX = 24;
    bool saveSelected = (currentOption == 7);
    const char* saveText = "SAVE";
    int16_t saveWidth = 24;

    if (saveSelected && optionActive) {
        display.fillRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (saveSelected) {
        display.drawRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(saveX + 1, y0);
    display.print(saveText);
    display.setTextColor(SSD1306_WHITE);

    // Delete button
    int16_t deleteX = 54;
    bool deleteSelected = (currentOption == 8);
    const char* deleteText = "DEL";
    int16_t deleteWidth = 18;

    if (deleteSelected && optionActive) {
        display.fillRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (deleteSelected) {
        display.drawRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(deleteX + 1, y0);
    display.print(deleteText);
    display.setTextColor(SSD1306_WHITE);

    // Pan (moved to top row after DEL)
    display.setCursor(78, y0);
    display.print("Pa:");
    bool panSelected = (currentOption == 6);
    uint8_t pan = channelPan[selectedChannel];

    int16_t panWidth = 18;
    if (panSelected && optionActive) {
        display.fillRect(96, y0 - 1, panWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (panSelected) {
        display.drawRect(96, y0 - 1, panWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(98, y0);
    if (pan == 255) {
        display.print("--");
    } else {
        if (pan < 10) display.print(" ");
        if (pan < 100) display.print(" ");
        display.print(pan);
    }
    display.setTextColor(SSD1306_WHITE);

    // Line 1: Channel and Mute
    int16_t y1 = 11;

    // Channel
    display.setCursor(0, y1);
    display.print("Ch:");
    bool channelSelected = (currentOption == 0);
    if (channelSelected && optionActive) {
        display.fillRect(18, y1 - 1, 18, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (channelSelected) {
        display.drawRect(18, y1 - 1, 18, 9, SSD1306_WHITE);
    }
    display.setCursor(20, y1);
    if (selectedChannel < 9) display.print(" ");
    display.print(selectedChannel + 1);
    display.setTextColor(SSD1306_WHITE);

    // Mute/Solo
    display.setCursor(44, y1);
    display.print("M:");
    bool muteSelected = (currentOption == 1);
    bool isMuted = (channelMutes & (1 << selectedChannel)) != 0;
    bool isSolo = (channelSolos & (1 << selectedChannel)) != 0;

    if (muteSelected && optionActive) {
        display.fillRect(56, y1 - 1, 12, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (muteSelected) {
        display.drawRect(56, y1 - 1, 12, 9, SSD1306_WHITE);
    }

    // Draw symbol based on state
    uint16_t color = muteSelected && optionActive ? SSD1306_BLACK : SSD1306_WHITE;
    int16_t cx = 62;
    int16_t cy = y1 + 3;

    if (isSolo) {
        // Draw 'S' for solo
        display.setCursor(59, y1);
        if (muteSelected && optionActive) {
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        }
        display.print("S");
        display.setTextColor(SSD1306_WHITE);
    } else if (isMuted) {
        // Draw filled circle for muted
        display.fillCircle(cx, cy, 3, color);
    } else {
        // Draw X for unmuted
        display.drawLine(cx - 2, cy - 2, cx + 2, cy + 2, color);
        display.drawLine(cx - 2, cy + 2, cx + 2, cy - 2, color);
    }
    display.setTextColor(SSD1306_WHITE);

    // Transpose
    display.setCursor(72, y1);
    display.print("T:");
    bool transposeSelected = (currentOption == 2);
    int8_t transpose = channelTranspose[selectedChannel];

    if (transposeSelected && optionActive) {
        display.fillRect(84, y1 - 1, 24, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (transposeSelected) {
        display.drawRect(84, y1 - 1, 24, 9, SSD1306_WHITE);
    }
    display.setCursor(86, y1);
    if (transpose > 0) {
        display.print("+");
    } else if (transpose == 0) {
        display.print(" ");
    }
    if (transpose == 0) {
        display.print(" 0");
    } else if (abs(transpose) < 10) {
        display.print(" ");
        display.print(transpose);
    } else {
        display.print(transpose);
    }
    display.setTextColor(SSD1306_WHITE);

    // Line 2: Program, Velocity, and Volume
    int16_t y2 = 21;

    // Program
    display.setCursor(0, y2);
    display.print("P:");
    bool programSelected = (currentOption == 3);
    uint8_t program = channelPrograms[selectedChannel];
    if (programSelected && optionActive) {
        display.fillRect(12, y2 - 1, 18, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (programSelected) {
        display.drawRect(12, y2 - 1, 18, 9, SSD1306_WHITE);
    }
    display.setCursor(14, y2);
    if (program == 128) {
        // 128 = use MIDI file (not set)
        display.print("--");
    } else {
        if (program < 10) display.print(" ");
        display.print(program);
    }
    display.setTextColor(SSD1306_WHITE);

    // Velocity (per-channel velocity scale)
    display.setCursor(36, y2);
    display.print("Ve:");
    bool velocitySelected = (currentOption == 4);
    uint8_t velocity = channelVelocity[selectedChannel];

    int16_t velWidth = 18;
    if (velocitySelected && optionActive) {
        display.fillRect(54, y2 - 1, velWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (velocitySelected) {
        display.drawRect(54, y2 - 1, velWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(56, y2);
    // Display velocity scale (1-200, 100=normal, 0 = use MIDI file default)
    if (velocity == 0) {
        display.print(" --");
    } else {
        if (velocity < 10) display.print(" ");
        if (velocity < 100) display.print(" ");
        display.print(velocity);
    }
    display.setTextColor(SSD1306_WHITE);

    // Volume (per-channel)
    display.setCursor(78, y2);
    display.print("Vo:");
    bool volumeSelected = (currentOption == 5);
    uint8_t volume = channelVolume[selectedChannel];
    if (volumeSelected && optionActive) {
        display.fillRect(96, y2 - 1, 18, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (volumeSelected) {
        display.drawRect(96, y2 - 1, 18, 9, SSD1306_WHITE);
    }
    display.setCursor(98, y2);
    if (volume == 255) {
        // 255 = use MIDI file default (not changed)
        display.print(" --");
    } else {
        if (volume < 10) display.print("  ");
        else if (volume < 100) display.print(" ");
        display.print(volume);
    }
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showTrackSettingsMenu(uint32_t targetBPM, bool useDefaultTempo, uint8_t velocityScale, bool sysexEnabled, uint8_t currentOption, bool optionActive, bool bpmEditingWhole) {
    display.clearDisplay();
    display.setTextSize(1);

    // Line 0: Title and Save/Delete buttons
    int16_t y0 = 0;
    display.setCursor(0, y0);
    display.print("TRCK");

    // Save button
    int16_t saveX = 30;
    bool saveSelected = (currentOption == 0);
    const char* saveText = "SAVE";
    int16_t saveWidth = 24;

    if (saveSelected && optionActive) {
        display.fillRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (saveSelected) {
        display.drawRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(saveX + 1, y0);
    display.print(saveText);
    display.setTextColor(SSD1306_WHITE);

    // Delete button
    int16_t deleteX = 60;
    bool deleteSelected = (currentOption == 1);
    const char* deleteText = "DEL";
    int16_t deleteWidth = 18;

    if (deleteSelected && optionActive) {
        display.fillRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (deleteSelected) {
        display.drawRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(deleteX + 1, y0);
    display.print(deleteText);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: BPM and Velocity
    int16_t y1 = 11;

    // BPM option
    display.setCursor(0, y1);
    display.print("BPM:");
    bool bpmSelected = (currentOption == 2);

    int16_t bpmWidth = 42;  // Wide enough for "120.50" (6 chars * 6px + padding)

    if (bpmSelected && optionActive) {
        display.fillRect(24, y1 - 1, bpmWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (bpmSelected) {
        display.drawRect(24, y1 - 1, bpmWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(26, y1);
    if (useDefaultTempo) {
        display.print(" --");
    } else {
        // Display targetBPM with 2 decimal places (targetBPM is in hundredths)
        uint16_t wholeBPM = targetBPM / 100;
        uint8_t decimalBPM = targetBPM % 100;
        char bpmText[8];
        sprintf(bpmText, "%d.%02d", wholeBPM, decimalBPM);
        display.print(bpmText);

        // Draw underline indicator when active to show which part is being edited
        if (bpmSelected && optionActive) {
            // Calculate position of whole vs decimal
            int16_t textStartX = 26;
            int16_t wholeDigits = (wholeBPM >= 100) ? 3 : (wholeBPM >= 10) ? 2 : 1;
            int16_t wholeWidth = wholeDigits * 6; // 6 pixels per character
            int16_t decimalStartX = textStartX + wholeWidth + 6; // After whole + "."
            int16_t decimalWidth = 2 * 6; // 2 decimal digits × 6 pixels

            // Draw a small line beneath the active part (in black since background is white)
            if (bpmEditingWhole) {
                // Underline whole number
                display.drawFastHLine(textStartX, y1 + 7, wholeWidth, SSD1306_BLACK);
            } else {
                // Underline decimal
                display.drawFastHLine(decimalStartX, y1 + 7, decimalWidth, SSD1306_BLACK);
            }
        }
    }
    display.setTextColor(SSD1306_WHITE);

    // Velocity option
    int16_t velX = 72;  // Moved right to avoid BPM overlap
    display.setCursor(velX, y1);
    display.print("Ve:");
    bool velSelected = (currentOption == 3);

    int16_t velWidth = 18;

    if (velSelected && optionActive) {
        display.fillRect(velX + 18, y1 - 1, velWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (velSelected) {
        display.drawRect(velX + 18, y1 - 1, velWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(velX + 20, y1);
    if (velocityScale == 0) {
        display.print("--");
    } else {
        if (velocityScale < 10) display.print(" ");
        display.print(velocityScale);
    }
    display.setTextColor(SSD1306_WHITE);

    // Line 2: SysEx option
    int16_t y2 = 21;
    display.setCursor(0, y2);
    display.print("SysEx:");
    bool sysexSelected = (currentOption == 4);

    int16_t sysexWidth = 18;

    if (sysexSelected && optionActive) {
        display.fillRect(36, y2 - 1, sysexWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (sysexSelected) {
        display.drawRect(36, y2 - 1, sysexWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(38, y2);
    display.print(sysexEnabled ? "ON" : "OFF");
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showMidiSettingsMenu(bool thruEnabled, bool keyboardEnabled, uint8_t keyboardChannel, uint8_t keyboardVelocity, uint8_t currentOption, bool optionActive) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Title
    display.setCursor(0, 0);
    display.print("MIDI IN");

    // Line 1: MIDI Thru
    int16_t y1 = 10;
    display.setCursor(0, y1);
    display.print("Thru:");

    bool thruSelected = (currentOption == 0);
    const char* thruText = thruEnabled ? "ON " : "OFF";
    int16_t thruWidth = 18;

    if (thruSelected && optionActive) {
        display.fillRect(36, y1 - 1, thruWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (thruSelected) {
        display.drawRect(36, y1 - 1, thruWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(38, y1);
    display.print(thruText);
    display.setTextColor(SSD1306_WHITE);

    // Line 2: MIDI Keyboard, Channel
    int16_t y2 = 19;
    display.setCursor(0, y2);
    display.print("Kbd:");

    bool kbdSelected = (currentOption == 1);
    const char* kbdText = keyboardEnabled ? "ON " : "OFF";
    int16_t kbdWidth = 18;

    if (kbdSelected && optionActive) {
        display.fillRect(24, y2 - 1, kbdWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (kbdSelected) {
        display.drawRect(24, y2 - 1, kbdWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(26, y2);
    display.print(kbdText);
    display.setTextColor(SSD1306_WHITE);

    // Keyboard Channel
    display.setCursor(48, y2);
    display.print("Ch:");

    bool chSelected = (currentOption == 2);
    char chText[4];
    if (keyboardChannel < 10) {
        sprintf(chText, " %d", keyboardChannel);
    } else {
        sprintf(chText, "%d", keyboardChannel);
    }
    int16_t chWidth = 14;

    if (chSelected && optionActive) {
        display.fillRect(66, y2 - 1, chWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (chSelected) {
        display.drawRect(66, y2 - 1, chWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(68, y2);
    display.print(chText);
    display.setTextColor(SSD1306_WHITE);

    // Keyboard Velocity
    display.setCursor(86, y2);
    display.print("V:");

    bool velSelected = (currentOption == 3);
    int16_t velWidth = 18;

    if (velSelected && optionActive) {
        display.fillRect(98, y2 - 1, velWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (velSelected) {
        display.drawRect(98, y2 - 1, velWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(100, y2);
    if (keyboardVelocity < 10) display.print(" ");
    display.print(keyboardVelocity);
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showClockSettingsMenu(bool clockEnabled, bool optionActive) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Title
    display.setCursor(0, 0);
    display.print("MIDI CLOCK");

    // Clock Enabled setting
    int16_t y1 = 12;
    display.setCursor(0, y1);
    display.print("ClkOut:");

    const char* clockText = clockEnabled ? "ON " : "OFF";
    int16_t clockWidth = 18;

    if (optionActive) {
        display.fillRect(48, y1 - 1, clockWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else {
        display.drawRect(48, y1 - 1, clockWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(50, y1);
    display.print(clockText);
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showRoutingMenu(uint8_t selectedChannel, uint8_t* channelRouting, uint8_t currentOption, bool optionActive) {
    display.clearDisplay();
    display.setTextSize(1);

    // Line 0: Title and Save/Delete buttons
    int16_t y0 = 0;
    display.setCursor(0, y0);
    display.print("RT");

    // Save button
    int16_t saveX = 30;
    bool saveSelected = (currentOption == 0);
    const char* saveText = "SAVE";
    int16_t saveWidth = 24;

    if (saveSelected && optionActive) {
        display.fillRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (saveSelected) {
        display.drawRect(saveX, y0 - 1, saveWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(saveX + 1, y0);
    display.print(saveText);
    display.setTextColor(SSD1306_WHITE);

    // Delete button
    int16_t deleteX = 60;
    bool deleteSelected = (currentOption == 1);
    const char* deleteText = "DEL";
    int16_t deleteWidth = 18;

    if (deleteSelected && optionActive) {
        display.fillRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (deleteSelected) {
        display.drawRect(deleteX, y0 - 1, deleteWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(deleteX + 1, y0);
    display.print(deleteText);
    display.setTextColor(SSD1306_WHITE);

    // Line 1: Channel selection and routing
    int16_t y1 = 11;

    // Channel option
    display.setCursor(0, y1);
    display.print("Ch:");
    bool channelSelected = (currentOption == 2);

    int16_t channelWidth = 12;

    if (channelSelected && optionActive) {
        display.fillRect(18, y1 - 1, channelWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (channelSelected) {
        display.drawRect(18, y1 - 1, channelWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(20, y1);
    display.print(selectedChannel + 1);
    display.setTextColor(SSD1306_WHITE);

    // Route To option (arrow symbol)
    display.setCursor(36, y1);
    display.print(">");

    bool routeSelected = (currentOption == 3);
    int16_t routeWidth = 18;

    if (routeSelected && optionActive) {
        display.fillRect(42, y1 - 1, routeWidth, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else if (routeSelected) {
        display.drawRect(42, y1 - 1, routeWidth, 9, SSD1306_WHITE);
    }
    display.setCursor(44, y1);

    // Display routing value: 255 = "--" (no routing), 0-15 = channel number
    if (channelRouting[selectedChannel] == 255) {
        display.print("--");
    } else {
        display.print(channelRouting[selectedChannel] + 1);
    }
    display.setTextColor(SSD1306_WHITE);

    display.display();
}

void DisplayManager::showVisualizer(uint8_t* channelActivity, uint8_t* channelPeak) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Update bubble positions
    unsigned long currentTime = millis();
    if (currentTime - lastBubbleUpdate > BUBBLE_UPDATE_DELAY) {
        for (uint8_t ch = 0; ch < 16; ch++) {
            for (uint8_t b = 0; b < 2; b++) {
                // Move bubble upward (increase Y to go up on screen)
                bubbles[ch][b].y += bubbles[ch][b].speed;

                // Wrap around when bubble reaches top
                if (bubbles[ch][b].y > 31.0f) {
                    bubbles[ch][b].y = 0.0f;
                }
            }
        }
        lastBubbleUpdate = currentTime;
    }

    // Draw 16 vertical bars with peak indicators
    // Screen: 128px wide, 32px tall
    // 16 channels × 8px = 128px (7px bar + 1px gap)
    // Bars anchored at bottom (Y=30) and grow upward toward numbers at top (Y=0)

    int16_t barBaseline = 30; // Baseline where bars start (bottom of screen area)
    int16_t numberY = 0;      // Channel numbers at top
    int16_t maxBarHeight = 22; // Max bar height

    for (uint8_t ch = 0; ch < 16; ch++) {
        int16_t x = ch * 8;  // Each channel gets 8 pixels spacing

        // Map activity (0-127) to bar height with full dynamic range
        int16_t barHeight = map(channelActivity[ch], 0, 127, 0, maxBarHeight);
        int16_t peakHeight = map(channelPeak[ch], 0, 127, 0, maxBarHeight);

        // Clamp to max height
        if (barHeight > maxBarHeight) barHeight = maxBarHeight;
        if (peakHeight > maxBarHeight) peakHeight = maxBarHeight;

        // Draw bars pixel by pixel from baseline upward
        // This ensures bars grow UP from the bottom, not down from top
        for (int16_t i = 0; i < barHeight; i++) {
            int16_t y = barBaseline - i;  // Start at baseline, move up (decreasing Y)
            display.drawFastHLine(x, y, 7, SSD1306_WHITE);
        }

        // Draw bubbles floating through the bar (only if bar is active)
        if (barHeight > 2) {
            for (uint8_t b = 0; b < 2; b++) {
                int16_t bubbleY = (int16_t)bubbles[ch][b].y;

                // Scale bubble position to bar height (bubbles only show in active bar area)
                int16_t scaledBubbleY = map(bubbleY, 0, 31, 0, barHeight);

                // Only draw bubble if it's within the active bar area and screen bounds
                if (scaledBubbleY >= 0 && scaledBubbleY < barHeight) {
                    int16_t bubbleScreenY = barBaseline - scaledBubbleY;  // Position from baseline upward

                    // Validate screen coordinates before drawing
                    if (bubbleScreenY >= 0 && bubbleScreenY < OLED_HEIGHT) {
                        int16_t bubbleX = x + 2 + (b * 2);  // b=0: x+2, b=1: x+4 for variety
                        display.fillCircle(bubbleX, bubbleScreenY, 1, SSD1306_BLACK);
                    }
                }
            }
        }

        // Draw peak hold indicator (2 pixel tall line)
        if (peakHeight > barHeight && peakHeight > 0) {
            int16_t peakY = barBaseline - peakHeight;
            display.drawFastHLine(x, peakY, 7, SSD1306_WHITE);
            display.drawFastHLine(x, peakY - 1, 7, SSD1306_WHITE);
        }

        // Draw baseline indicator (dash) at bottom
        display.drawFastHLine(x + 2, barBaseline + 1, 3, SSD1306_WHITE);

        // Draw channel number at top near peaks
        char channelLabel[2];
        if (ch < 9) {
            channelLabel[0] = '1' + ch;
        } else {
            channelLabel[0] = '0' + (ch - 9);
        }
        channelLabel[1] = '\0';
        display.setCursor(x, numberY);
        display.print(channelLabel);
    }

    display.display();
}
