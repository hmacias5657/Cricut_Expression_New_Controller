#pragma once
#include <stdint.h>
#include <Arduino.h>

// ============================================================
// Plotter UI Emulation — Mode, Function & Display Spec
// ============================================================

// --- Cutting Modes (toggle via keys) ---
enum PlotterMode : uint8_t {
    MODE_LANDSCAPE   = 0,  // default — characters left-to-right
    MODE_PORTRAIT    = 1,  // rotate 90°
    MODE_MIXMATCH    = 2,  // alternate between 2 characters
    MODE_QUANTITY    = 3,  // same character N times
    MODE_FITPAGE     = 4,  // scale to fit mat boundaries
    MODE_FITLENGTH   = 5,  // scale to fit length
    MODE_AUTOFILL    = 6,  // fill page with max copies (max 300)
};

static const char* const MODE_LABELS[] = {
    "Land", "Port", "MixN", "Qty", "FPg", "FLn", "AF"
};

// --- Functions applied per cut (toggled via keys) ---
struct PlotterFunctions {
    uint8_t multiCut     : 2;  // 0=off, 1=2pass, 2=3pass, 3=4pass
    bool    centerPoint  : 1;
    bool    lineReturn   : 1;
    bool    flip         : 1;
    bool    paperSaver   : 1;
};

// --- Settings (persistent across power cycles) ---
enum PlotterLanguage : uint8_t {
    LANG_EN = 0,
    LANG_FR = 1,
    LANG_ES = 2,
    LANG_DE = 3,
};

static const char* const LANG_LABELS[] = { "EN", "FR", "ES", "DE" };

enum PlotterUnit : uint8_t {
    UNIT_IN_QUARTER = 0,  // inches, 1/4 increments
    UNIT_IN_TENTH   = 1,  // inches, 1/10 increments
    UNIT_CM         = 2,  // centimeters
    UNIT_MM         = 3,  // millimeters
};

static const char* const UNIT_LABELS[] = { "in(1/4)", "in(1/10)", "cm", "mm" };

enum PlotterMatSize : uint8_t {
    MAT_12X12 = 0,
    MAT_12X24 = 1,
};

static const float MAT_WIDTH_MM[]  = { 304.8f, 304.8f };  // 12 inches
static const float MAT_HEIGHT_MM[] = { 304.8f, 609.6f };  // 12 or 24 inches

static const char* const MAT_LABELS[] = { "12x12", "12x24" };

// --- Speed / Pressure Dial values (1–5 bars) ---
#define SPEED_BARS     5
#define PRESSURE_BARS  5

// --- Auto Fill limit ---
#define AUTOFILL_MAX_COPIES  300

// --- Character buffer limits ---
#define MAX_ONSCREEN_CHARS   32

// --- Display layout constants (128x64 OLED, profont10 = 5x10) ---
#define DISPLAY_W       128
#define DISPLAY_H       64
#define FONT_SMALL_W    5
#define FONT_SMALL_H    10

// Y-offsets for each status line (baseline)
#define LINE_MODE_BAR   10
#define LINE_POS_SIZE   22
#define LINE_SPEED_PRS  34
#define LINE_FUNCTIONS  46
#define LINE_STATUS     58

// Bar graph dimensions
#define BAR_X_START     60
#define BAR_W           6
#define BAR_GAP         2
#define BAR_H           7
#define BAR_Y_OFF       3  // offset from baseline

// --- Sound / beep ---
#define BEEP_FREQ       2000
#define BEEP_SHORT_MS   50
#define BEEP_LONG_MS    200
#define BEEP_ERROR_MS   400
#define BEEP_ERROR_PATTERN  { BEEP_ERROR_MS, 100, BEEP_ERROR_MS, 100, BEEP_ERROR_MS, 0 }

// Error messages
#define ERR_FILE_TOO_LARGE   "FILE TOO LARGE"
#define ERR_USB_NOT_READY    "USB NOT READY"
#define ERR_PSRAM_FAIL       "PSRAM FAIL"

// ============================================================
// Runtime state — instantiated in main.cpp
// ============================================================
struct PlotterState {
    PlotterMode      mode{MODE_LANDSCAPE};
    PlotterFunctions funcs{};
    PlotterLanguage  lang{LANG_EN};
    PlotterUnit      unit{UNIT_IN_QUARTER};
    PlotterMatSize   matSize{MAT_12X12};
    bool            soundOn{true};
    bool            charImages{true};

    // Dials
    uint8_t         speedLevel{3};     // 1-5
    uint8_t         pressureLevel{3};  // 1-5
    float           sizeInches{1.0f};

    // Quantity & Auto Fill
    uint8_t         quantityCopies{1};
    uint16_t        autoFillCopies{AUTOFILL_MAX_COPIES};
    uint8_t         cutsRemaining{0};

    // Shift
    bool            shiftLock{false};
    int8_t          shiftState{0};

    // On-screen character buffer
    char            chars[MAX_ONSCREEN_CHARS]{};
    int             charCount{0};
    int             cursorPos{0};

    // WiFi settings (editable from menu)
    char            wifiSSID[33]{};
    char            wifiPass[65]{};
    bool            wifiChanged{false};

    // Potentiometer calibration (5 detent ADC values + validity flags)
    uint16_t        calSpeed[5]{};
    uint16_t        calPressure[5]{};
    bool            calSpeedValid{false};
    bool            calPressureValid{false};

    // Blade position (mm)
    float           bladeX{0};
    float           bladeY{0};
};
