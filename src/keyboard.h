#pragma once
#include <stdint.h>
#include "config.h"

class PlotterKeyboard {
public:
    void begin();
    int  scan();          // returns key index or -1
    bool keyPressed(int idx);  // non-blocking peek
    bool stopPressed();   // direct STOP button

    // LED control (requires KBD_LED_EN wired)
    void setLED(uint16_t mask);
    void ledsOn();
    void ledsOff();

private:
    uint8_t _state[KBD_MAX_COLS]{0};
    uint8_t _prev[KBD_MAX_COLS]{0};
    uint16_t _leds{0};

    void writeCols(uint32_t val);
    uint8_t readRows();
    void saveRestoreRow4(bool restore);
};

// Key indices (from FreeExpression keypad_expression.h)
enum {
    KEY_F1 = 0,
    KEY_F2 = 1,
    KEY_1 = 2,
    KEY_2 = 3,
    KEY_3 = 4,
    KEY_4 = 5,
    KEY_5 = 6,
    KEY_6 = 7,
    KEY_7 = 8,
    KEY_8 = 9,
    KEY_9 = 10,
    KEY_0 = 11,
    KEY_SPACE = 12,
    KEY_BACKSPACE = 13,
    KEY_PORTRAIT = 14,
    KEY_FLIP = 15,
    KEY_MINUS = 16,
    KEY_OK = 17,
    KEY_PLUS = 18,
    KEY_XTRA1 = 19,
    KEY_F3 = 24,
    KEY_F4 = 25,
    KEY_Q = 26,
    KEY_W = 27,
    KEY_E = 28,
    KEY_R = 29,
    KEY_T = 30,
    KEY_Y = 31,
    KEY_U = 32,
    KEY_I = 33,
    KEY_O = 34,
    KEY_P = 35,
    KEY_CHARDISPLAY = 36,
    KEY_RESETALL = 37,
    KEY_FITPAGE = 38,
    KEY_QUANTITY = 39,
    KEY_XTRA2 = 43,
    KEY_F5 = 48,
    KEY_F6 = 49,
    KEY_A = 50,
    KEY_S = 51,
    KEY_D = 52,
    KEY_F = 53,
    KEY_G = 54,
    KEY_H = 55,
    KEY_J = 56,
    KEY_K = 57,
    KEY_L = 58,
    KEY_SEMICOLON = 59,
    KEY_REPEATLAST = 60,
    KEY_SOUNDONOFF = 61,
    KEY_MIXMATCH = 62,
    KEY_MULTICUT = 63,
    KEY_MOVEUPLEFT = 64,
    KEY_MOVEUP = 65,
    KEY_MOVEUPRIGHT = 66,
    KEY_LINERETURN = 67,
    KEY_MATERIALSAVER = 72,
    KEY_REALDIALSIZE = 73,
    KEY_Z = 74,
    KEY_X = 75,
    KEY_C = 76,
    KEY_V = 77,
    KEY_B = 78,
    KEY_N = 79,
    KEY_M = 80,
    KEY_COMMA = 81,
    KEY_PERIOD = 82,
    KEY_SLASH = 83,
    KEY_LOADLAST = 84,
    KEY_SETCUTAREA = 85,
    KEY_FITLENGTH = 86,
    KEY_CENTERPOINT = 87,
    KEY_MOVELEFT = 88,
    KEY_CUT = 89,
    KEY_MOVERIGHT = 90,
    KEY_SETTINGS = 91,
    KEY_SHIFT = 96,
    KEY_CUT_SHIFTLOCK = 97,
    KEY_EQUALS = 100,
    KEY_LEFTBRACKET = 101,
    KEY_RIGHTBRACKET = 102,
    KEY_LEFTBRACE = 103,
    KEY_RIGHTBRACE = 104,
    KEY_QUOTE = 105,
    KEY_49 = 106,
    KEY_50 = 107,
    KEY_LOADMAT = 108,
    KEY_UNLOADMAT = 109,
    KEY_AUTOFILL = 110,
    KEY_MATSIZE = 111,
    KEY_MOVEDNLEFT = 112,
    KEY_MOVEDN = 113,
    KEY_MOVEDNRIGHT = 114,
};
