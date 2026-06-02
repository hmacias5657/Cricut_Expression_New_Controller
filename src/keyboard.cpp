#include "keyboard.h"
#include "config.h"
#include <Arduino.h>

void PlotterKeyboard::begin() {
    pinMode(KBD_CLK, OUTPUT);
    pinMode(KBD_DATA, OUTPUT);
    digitalWrite(KBD_CLK, LOW);
    digitalWrite(KBD_DATA, LOW);

#if KBD_LED_EN >= 0
    pinMode(KBD_LED_EN, OUTPUT);
    digitalWrite(KBD_LED_EN, HIGH);
#endif

    for (int i = 0; i < KBD_MAX_ROWS; i++) {
        int pin = (i == 0) ? KBD_ROW0 :
                  (i == 1) ? KBD_ROW1 :
                  (i == 2) ? KBD_ROW2 :
                  (i == 3) ? KBD_ROW3 : KBD_ROW4;
        if (pin >= 0) pinMode(pin, INPUT_PULLUP);
    }

#if KBD_STOP >= 0
    pinMode(KBD_STOP, INPUT_PULLUP);
#endif

    memset(_state, 0, sizeof(_state));
    memset(_prev, 0, sizeof(_prev));
}

// Shift 24 bits MSB-first into the daisy-chained shift register.
// Only the lower 24 bits of 'val' are used.
void PlotterKeyboard::writeCols(uint32_t val) {
    for (int i = 0; i < KBD_MAX_COLS; i++) {
        digitalWrite(KBD_DATA, (val & 0x800000UL) ? HIGH : LOW);
        digitalWrite(KBD_CLK, HIGH);
        delayMicroseconds(1);
        val <<= 1;
        digitalWrite(KBD_CLK, LOW);
        delayMicroseconds(1);
    }
}

uint8_t PlotterKeyboard::readRows() {
    uint8_t rows = 0;
    if (KBD_ROW0 >= 0) rows |= (digitalRead(KBD_ROW0) == LOW) ? (1 << 0) : 0;
    if (KBD_ROW1 >= 0) rows |= (digitalRead(KBD_ROW1) == LOW) ? (1 << 1) : 0;
    if (KBD_ROW2 >= 0) rows |= (digitalRead(KBD_ROW2) == LOW) ? (1 << 2) : 0;
    if (KBD_ROW3 >= 0) rows |= (digitalRead(KBD_ROW3) == LOW) ? (1 << 3) : 0;
    if (KBD_ROW4 >= 0) rows |= (digitalRead(KBD_ROW4) == LOW) ? (1 << 4) : 0;
    return rows;
}

void PlotterKeyboard::saveRestoreRow4(bool restore) {
    if (KBD_ROW4 < 0) return;
    static bool savedOut = false;
    static uint8_t savedLevel = HIGH;
    if (restore) {
        pinMode(KBD_ROW4, OUTPUT);
        digitalWrite(KBD_ROW4, savedLevel);
    } else {
        savedLevel = digitalRead(KBD_ROW4);
        pinMode(KBD_ROW4, INPUT_PULLUP);
    }
}

int PlotterKeyboard::scan() {
    ledsOff();
    saveRestoreRow4(false);

    // Load pattern: column 0 LOW (selected), columns 1-23 HIGH (deselected)
    writeCols(0xFFFFFEUL);

    // Shift the single LOW through all 24 columns, reading rows at each position
    digitalWrite(KBD_DATA, HIGH);
    for (int col = 0; col < KBD_MAX_COLS; col++) {
        _state[col] = readRows();
        digitalWrite(KBD_CLK, HIGH);
        delayMicroseconds(1);
        digitalWrite(KBD_CLK, LOW);
        delayMicroseconds(1);
    }

    saveRestoreRow4(true);
    writeCols((~_leds) & 0xFFFFFFUL);
    ledsOn();

    // Debounce: find newly pressed keys
    for (int col = 0; col < KBD_MAX_COLS; col++) {
        uint8_t diff = _state[col] ^ _prev[col];
        if (diff) {
            for (int row = 0; row < KBD_MAX_ROWS; row++) {
                uint8_t mask = 1 << row;
                if (diff & mask & _state[col]) {
                    _prev[col] = _state[col];
                    return row * KBD_MAX_COLS + col;
                }
            }
        }
        _prev[col] = _state[col];
    }
    return -1;
}

bool PlotterKeyboard::keyPressed(int idx) {
    if (idx < 0 || idx >= KBD_MAX_ROWS * KBD_MAX_COLS) return false;
    int row = idx / KBD_MAX_COLS;
    int col = idx % KBD_MAX_COLS;
    return (_state[col] & (1 << row)) != 0;
}

bool PlotterKeyboard::stopPressed() {
#if KBD_STOP >= 0
    return digitalRead(KBD_STOP) == LOW;
#else
    return false;
#endif
}

void PlotterKeyboard::setLED(uint16_t mask) {
    _leds = mask;
}

void PlotterKeyboard::ledsOn() {
#if KBD_LED_EN >= 0
    digitalWrite(KBD_LED_EN, LOW);
#endif
}

void PlotterKeyboard::ledsOff() {
#if KBD_LED_EN >= 0
    digitalWrite(KBD_LED_EN, HIGH);
#endif
}
