#include <Arduino.h>
#include <ctype.h>
#include <atomic>
#include <Preferences.h>
#include <Update.h>
#include "config.h"
#include "plotter_ui.h"
#include "stepper.h"
#include "gcode_parser.h"
#include "hpgl_parser.h"
#include "usb_drive.h"
#include "psram_buffer.h"
#include "wifi_server.h"
#include "display.h"
#include "menu.h"
#include "svg_parser.h"
#include "knife_comp.h"
#if KBD_ENABLE
#include "keyboard.h"
#endif

StepperControl stepper;
GCodeParser parser;
HPGLParser hpgl;
USBDrive   usbDrive;
PSRAMBuffer psramBuf;
PlotterServer wifiServer;
PlotterDisplay display;
PlotterMenu menu;

#if KBD_ENABLE
PlotterKeyboard kbd;
#endif

// Magnification (SVG zoom) — defaults to 1.0x on startup
static float currentZoom = MAG_DEFAULT;

// Quadrature encoder state
static uint8_t encPrev = 0;

// Plotter Expression UI runtime state
PlotterState plotter;

// State machine
enum State { IDLE, RUNNING, HOMING, DWELL, PLAYING_SD, PAUSED, PLOTTING_SVG, KNIFE_PIVOT };
static std::atomic<State> state{IDLE};
static std::atomic<bool> moveComplete{false};
static unsigned long dwellUntil = 0;
static bool solenoidOn = false;
static int potPressure = DEFAULT_PRESSURE;
static int gcodePressure = -1;
static int smoothSpeed = -1;
static float currentFeed = DEFAULT_FEED;
static char serialBuf[GCODE_LINE_MAX];
static size_t serialIdx = 0;
static bool menuActive = false;
static bool hpglMode = false;  // explicit HPGL mode; auto-detect when false

// File playback from PSRAM buffer
static size_t filePlayOffset = 0;   // current read position in psramBuf
static char currentFilePath[80] = "";

// Multi-cut tracking
static int multiCutPass = 0;

// Non-blocking beep state
static bool beeping = false;
static unsigned long beepOffAt = 0;
static unsigned long beepNextAt = 0;
static int beepPatternIdx = 0;
static const int* beepPatternPtr = nullptr;

// Non-blocking post-cut action state
enum PostCutAction { POST_NONE, POST_LINE_RETURN, POST_MULTI_CUT, POST_REPLAY_COPY };
static PostCutAction postCutAction = POST_NONE;

// Shift/character sequencing
static int shiftMode = 0; // 0=normal, 1=shift, 2=shift-lock

// --- Mode/Function Transform State ---
static struct {
    // Bounding box of current design (mm)
    float bbMinX, bbMinY, bbMaxX, bbMaxY;
    bool  bbValid;

    // Fit to Page / Fit to Length scale factor
    float scaleX, scaleY;

    // Center Point offset (blade position - design center)
    float centerOffX, centerOffY;

    // Quantity / Auto Fill replay
    int   replayRemaining;
    float replayOffY;       // Y offset per copy
    int   autoFillCols;     // copies across
    int   autoFillRow;      // current row
    int   autoFillCol;      // current column
    float autoFillOffX;     // X offset per copy in row
} modeXform;

// Forward declarations
void processLine(const char* line, size_t len = 0);
void handleUpload(const char* data, size_t dataLen);
void plotSVG(const char* path);
void handleButtons();
void handlePlotterKey(int key);
void beep(int freq, int duration);
void beepError();
void startCut();
void playFileFromBuffer();
void usbError(const char* msg);
static void setupModeTransforms();
static void applyMoveTransform(float &x, float &y);

// --- G-code callback implementations ---

void onMove(float x, float y, float f) {
    if (!stepper.isHomed()) {
        Serial.println("error: not homed");
        return;
    }
    applyMoveTransform(x, y);
    x = constrain(x, 0, X_MAX_MM);
    y = constrain(y, 0, Y_MAX_MM);
    bool penDown = solenoidOn;
    knifeMove(x, y, f, penDown);
    if (!knifeHasPendingMove()) {
        state = RUNNING;
    } else {
        state = KNIFE_PIVOT;
    }
    display.setPosition(x, y);
}

void onSetSpeed(float f) {
}

void onHome() {
    state = HOMING;
    stepper.homeX();
    state = IDLE;
    // Home is at X_MAX_MM (rightmost); Y stays at 0
    display.setPosition(X_MAX_MM, 0);
    Serial.println("ok");
}

void onSolenoid(bool on, float pressure) {
    solenoidOn = on;
    if (pressure >= 0) {
        gcodePressure = constrain((int)pressure, 0, 100);
    }
    int pct = (gcodePressure >= 0) ? gcodePressure : potPressure;
    pct = constrain(pct, 0, 100);
    int duty = on ? map(pct, 0, 100, 0, 255) : 0;
    ledcWrite(SOLENOID_PWM_CH, duty);
    display.setPressure(pct);
    Serial.printf("// solenoid %s  pressure:%d%%  duty:%d\n", on ? "DOWN" : "UP", pct, duty);
}

void onReport() {
    int pct = (gcodePressure >= 0) ? gcodePressure : potPressure;
    Serial.printf("X:%.2f Y:%.2f P:%d%% State:%d\n", stepper.currentX(), stepper.currentY(), pct, (int)state.load());
    char buf[96];
    snprintf(buf, sizeof(buf), "<X%.2f Y%.2f P:%d%%>", stepper.currentX(), stepper.currentY(), pct);
    wifiServer.broadcast(buf);
}

void onDwell(unsigned long ms) {
    dwellUntil = millis() + ms;
    state = DWELL;
}

void onError(const char* msg) {
    Serial.printf("error: %s\n", msg);
}

void onFile(const char* filename) {
    knifeCompReset();
    strncpy(currentFilePath, filename, sizeof(currentFilePath) - 1);

    // Try USB drive first
    if (usbDrive.isReady()) {
        if (usbDrive.loadFile(filename, psramBuf)) {
            filePlayOffset = 0;
            state = PLAYING_SD;
            Serial.printf("// playing %s from USB (%u bytes)\n", filename, psramBuf.size());
            return;
        }
        // If file not found on USB, fall through and try PSRAM buffer if file was uploaded
    }

    // File not found on USB — check if it was previously uploaded to PSRAM
    if (psramBuf.isReady() && psramBuf.size() > 0) {
        // Assume the PSRAM content is the requested file
        filePlayOffset = 0;
        state = PLAYING_SD;
        Serial.printf("// playing from PSRAM buffer (%u bytes)\n", psramBuf.size());
        return;
    }

    Serial.printf("error: cannot open %s\n", filename);
}

// --- SVG plotting callback ---

static bool svgPlotting = false;
static float svgPrevX = 0, svgPrevY = 0;
static char gcodePath[64] = "";

// Bounding box accumulation during SVG conversion (P3-3)
static float svgBbMinX, svgBbMinY, svgBbMaxX, svgBbMaxY;
static bool svgBbValid;

void svgMoveTo(float x, float y) {
    svgPrevX = x; svgPrevY = y;
    if (x < svgBbMinX) svgBbMinX = x;
    if (x > svgBbMaxX) svgBbMaxX = x;
    if (y < svgBbMinY) svgBbMinY = y;
    if (y > svgBbMaxY) svgBbMaxY = y;
    svgBbValid = true;
    char line[64];
    int n = snprintf(line, sizeof(line), "G0 X%.3f Y%.3f\n", x, y);
    psramBuf.write((uint8_t*)line, n);
}

void svgLineTo(float x, float y) {
    svgPrevX = x; svgPrevY = y;
    if (x < svgBbMinX) svgBbMinX = x;
    if (x > svgBbMaxX) svgBbMaxX = x;
    if (y < svgBbMinY) svgBbMinY = y;
    if (y > svgBbMaxY) svgBbMaxY = y;
    svgBbValid = true;
    char line[64];
    int n = snprintf(line, sizeof(line), "G1 X%.3f Y%.3f F%.0f\n", x, y, (float)currentFeed);
    psramBuf.write((uint8_t*)line, n);
}

void svgPenUp() {
    psramBuf.write((uint8_t*)"M5\n", 3);
}

// --- Build G-code filename from SVG path and zoom ---

static void buildGcodePath(const char* svgPath, float zoom, char* out, size_t outSize) {
    // Extract basename without extension
    const char* slash = strrchr(svgPath, '/');
    const char* base = slash ? slash + 1 : svgPath;
    char baseName[9];
    int bi = 0;
    while (*base && *base != '.' && bi < 8)
        baseName[bi++] = toupper((unsigned char)*base++);
    while (bi < 8) baseName[bi++] = ' ';
    baseName[8] = '\0';

    // Trim trailing spaces for snprintf
    while (bi > 0 && baseName[bi - 1] == ' ') bi--;
    baseName[bi] = '\0';

    // Check if multi-cut needs magnification in name
    int passes = (plotter.funcs.multiCut > 0) ? (plotter.funcs.multiCut + 1) : 1;
    if (passes > 1) {
        int zoomCode = (int)(zoom * 10.0f + 0.5f);
        if (zoomCode > 99) zoomCode = 99;
        if (zoomCode < 0) zoomCode = 0;
        // Truncate base to make room for M## suffix (max 6 chars + M + 2 digits = 8)
        int maxBase = 5;
        if (bi > maxBase) bi = maxBase;
        baseName[bi] = '\0';
        snprintf(out, outSize, "/GCODE/%.5sM%02d.GCO", baseName, zoomCode);
    } else {
        snprintf(out, outSize, "/GCODE/%.8s.GCO", baseName);
    }
}

// --- SVG file plotting ---

void plotSVG(const char* path) {
    // Step 1: Load SVG into a temp buffer (stack-allocated or PSRAM copy)
    bool loaded = false;
    if (usbDrive.isReady()) {
        loaded = usbDrive.loadFile(path, psramBuf);
    }
    if (!loaded && psramBuf.isReady() && psramBuf.size() > 0) {
        loaded = true;
    }
    if (!loaded) {
        Serial.printf("error: cannot open %s\n", path);
        return;
    }

    size_t len = psramBuf.size();
    if (len > SVG_MAX_FILE_SIZE) {
        Serial.printf("error: SVG too large (%zu)\n", len);
        psramBuf.clear();
        return;
    }

    // Step 2: Copy SVG source to a separate buffer so we can reuse PSRAM for G-code output
    char* svgSrc = (char*)ps_malloc(len + 1);
    if (!svgSrc) {
        Serial.println("error: out of memory for SVG copy");
        psramBuf.clear();
        return;
    }
    memcpy(svgSrc, psramBuf.buffer(), len);
    svgSrc[len] = '\0';

    // Step 3: Clear PSRAM and use it for G-code output
    psramBuf.clear();
    psramBuf.begin(); // ensure buffer is writable

    // Home first (home = X_MAX_MM, rightmost position)
    svgPlotting = true;
    // Init bounding box accumulation for SVG (P3-3)
    svgBbMinX = svgBbMinY = 1e10f;
    svgBbMaxX = svgBbMaxY = -1e10f;
    svgBbValid = false;
    if (!stepper.isHomed()) {
        stepper.homeX();
        display.setPosition(X_MAX_MM, 0);
    }

    // Step 4: Parse SVG, callbacks write G-code to PSRAM
    SVGParser svg;
    svg.setZoom(currentZoom);
    SVGParser::Callbacks cb;
    cb.onMoveTo = svgMoveTo;
    cb.onLineTo = svgLineTo;
    cb.onPenUp = svgPenUp;

    bool parseOk = svg.parse(svgSrc, len, cb);
    free(svgSrc);

    if (!parseOk) {
        Serial.println("error: no paths found in SVG");
        psramBuf.clear();
        svgPlotting = false;
        return;
    }

    Serial.printf("// SVG: %d paths -> %u bytes G-code\n",
                  svg.info().pathCount, psramBuf.size());

    // Step 5: If multi-cut, save G-code to USB /GCODE/ for replay
    int passes = (plotter.funcs.multiCut > 0) ? (plotter.funcs.multiCut + 1) : 1;
    if (passes > 1 && usbDrive.isReady()) {
        buildGcodePath(path, currentZoom, gcodePath, sizeof(gcodePath));
        // Ensure /GCODE directory exists
        usbDrive.makeDir("/GCODE");
        // Save only if file doesn't already exist
        if (!usbDrive.exists(gcodePath)) {
            if (usbDrive.writeFile(gcodePath, (const uint8_t*)psramBuf.buffer(), psramBuf.size())) {
                Serial.printf("// saved G-code to %s\n", gcodePath);
            } else {
                Serial.println("error: failed to save G-code to USB");
                gcodePath[0] = '\0';
            }
        } else {
            Serial.printf("// reusing existing %s\n", gcodePath);
        }
        // Point currentFilePath at the G-code file for multi-cut replay
        if (gcodePath[0]) {
            strncpy(currentFilePath, gcodePath, sizeof(currentFilePath) - 1);
        }
    } else {
        // Single copy: keep original SVG path for Repeat Last
        strncpy(currentFilePath, path, sizeof(currentFilePath) - 1);
        gcodePath[0] = '\0';
    }

    // Step 6: Play the G-code from PSRAM
    filePlayOffset = 0;
    state = PLAYING_SD;
    svgPlotting = false;
}

// --- Command routing ---

// ─── Non-blocking Beeper ────────────────────────────────────
#if BUZZER_PIN >= 0
void beep(int freq, int duration) {
    if (!plotter.soundOn) return;
    ledcWriteTone(BUZZER_PWM_CH, freq);
    beepOffAt = millis() + duration;
    beeping = true;
}

void beepError() {
    if (!plotter.soundOn) return;
    beepPatternPtr = (const int[]) BEEP_ERROR_PATTERN;
    beepPatternIdx = 0;
    beepNextAt = 0;
    beeping = true;
}

static void updateBeep() {
    if (!beeping) return;
    unsigned long now = millis();

    if (beepPatternPtr) {
        // Multi-beep pattern mode
        if (now >= beepNextAt) {
            if (beepPatternPtr[beepPatternIdx] > 0) {
                ledcWriteTone(BUZZER_PWM_CH, BEEP_FREQ);
                beepOffAt = now + beepPatternPtr[beepPatternIdx];
                beepPatternIdx++;
                if (beepPatternPtr[beepPatternIdx] > 0) {
                    beepNextAt = beepOffAt + beepPatternPtr[beepPatternIdx];
                    beepPatternIdx++;
                } else {
                    beepNextAt = beepOffAt;
                    beepPatternIdx++;
                }
            } else {
                beeping = false;
                beepPatternPtr = nullptr;
                ledcWriteTone(BUZZER_PWM_CH, 0);
            }
        } else if (beepOffAt > 0 && now >= beepOffAt) {
            ledcWriteTone(BUZZER_PWM_CH, 0);
            beepOffAt = 0;
        }
    } else {
        // Single beep mode
        if (now >= beepOffAt) {
            ledcWriteTone(BUZZER_PWM_CH, 0);
            beeping = false;
        }
    }
}
#else
void beep(int, int) {}
void beepError() {}
static void updateBeep() {}
#endif

// ─── Show error on display + sound beep pattern ─────────────
void usbError(const char* msg) {
    Serial.printf("error: %s\n", msg);
    display.setState(msg);
    display.showError(msg);
    beepError();
}

// ─── Start cut (applies multi-pass, modes, functions) ───────
void startCut() {
    if (state == RUNNING || state == PLAYING_SD) return;
    beep(BEEP_FREQ, BEEP_SHORT_MS);
    multiCutPass = 0;
    int passes = (plotter.funcs.multiCut > 0) ? (plotter.funcs.multiCut + 1) : 1;
    plotter.cutsRemaining = passes;
    Serial.printf("// cut started passes=%d mode=%d\n", passes, plotter.mode);
    if (currentFilePath[0] && (usbDrive.isReady() || psramBuf.isReady())) {
        if (usbDrive.isReady()) {
            usbDrive.loadFile(currentFilePath, psramBuf);
        }
        // Pre-compute mode transforms before playback
        setupModeTransforms();
        filePlayOffset = 0;
        if (psramBuf.size() > 0) {
            state = PLAYING_SD;
        }
    }
}

// ─── Mode Transform helpers ──────────────────────────────────

// Scan PSRAM buffer for min/max X,Y in G0/G1 moves
static void scanBoundingBox() {
    modeXform.bbMinX = 1e10f; modeXform.bbMinY = 1e10f;
    modeXform.bbMaxX = -1e10f; modeXform.bbMaxY = -1e10f;
    size_t end = psramBuf.size();
    size_t off = 0;
    char line[GCODE_LINE_MAX];
    while (off < end) {
        int i = 0;
        while (off < end && i < GCODE_LINE_MAX - 1) {
            char c = (char)psramBuf.at(off++);
            if (c == '\n') break;
            if (c == '\r') continue;
            line[i++] = c;
        }
        line[i] = '\0';
        const char* p = line;
        while (*p == ' ') p++;
        if (p[0] != 'G' || (p[1] != '0' && p[1] != '1')) continue;
        float x = 0, y = 0;
        while (*p) {
            if ((*p == 'X' || *p == 'x')) { x = strtof(p + 1, (char**)&p); }
            else if ((*p == 'Y' || *p == 'y')) { y = strtof(p + 1, (char**)&p); }
            else { p++; }
        }
        if (x < modeXform.bbMinX) modeXform.bbMinX = x;
        if (x > modeXform.bbMaxX) modeXform.bbMaxX = x;
        if (y < modeXform.bbMinY) modeXform.bbMinY = y;
        if (y > modeXform.bbMaxY) modeXform.bbMaxY = y;
    }
    modeXform.bbValid = (modeXform.bbMaxX >= modeXform.bbMinX &&
                         modeXform.bbMaxY >= modeXform.bbMinY);
    Serial.printf("// bbox: %.1f-%.1f X  %.1f-%.1f Y\n",
                  modeXform.bbMinX, modeXform.bbMaxX,
                  modeXform.bbMinY, modeXform.bbMaxY);
}

// Calculate all transform parameters before playback begins
static void setupModeTransforms() {
    modeXform.scaleX = modeXform.scaleY = 1.0f;
    modeXform.centerOffX = modeXform.centerOffY = 0;

    bool needBBox = (plotter.mode == MODE_FITPAGE ||
                     plotter.mode == MODE_FITLENGTH ||
                     plotter.funcs.centerPoint);
    if (needBBox) scanBoundingBox();

    // Fit to Page — scale to fill mat, preserve aspect ratio
    if (plotter.mode == MODE_FITPAGE && modeXform.bbValid) {
        float dw = modeXform.bbMaxX - modeXform.bbMinX;
        float dh = modeXform.bbMaxY - modeXform.bbMinY;
        if (dw > 0 && dh > 0) {
            float sx = MAT_WIDTH_MM[plotter.matSize] / dw;
            float sy = MAT_HEIGHT_MM[plotter.matSize] / dh;
            float s = (sx < sy) ? sx : sy;
            modeXform.scaleX = modeXform.scaleY = s;
            Serial.printf("// fit-to-page scale: %.3f\n", s);
        }
    }

    // Fit to Length — scale to fit length, preserve aspect ratio
    if (plotter.mode == MODE_FITLENGTH && modeXform.bbValid) {
        float dw = modeXform.bbMaxX - modeXform.bbMinX;
        float dh = modeXform.bbMaxY - modeXform.bbMinY;
        float matW = MAT_WIDTH_MM[plotter.matSize];
        float matH = MAT_HEIGHT_MM[plotter.matSize];
        float matLen = (matW > matH) ? matW : matH;
        float designLen = (dw > dh) ? dw : dh;
        if (designLen > 0) {
            float s = matLen / designLen;
            modeXform.scaleX = modeXform.scaleY = s;
            Serial.printf("// fit-to-length scale: %.3f\n", s);
        }
    }

    // Center Point — offset so blade position becomes design centre
    if (plotter.funcs.centerPoint && modeXform.bbValid) {
        float cx = (modeXform.bbMinX + modeXform.bbMaxX) / 2.0f;
        float cy = (modeXform.bbMinY + modeXform.bbMaxY) / 2.0f;
        modeXform.centerOffX = plotter.bladeX - cx * modeXform.scaleX;
        modeXform.centerOffY = plotter.bladeY - cy * modeXform.scaleY;
        Serial.printf("// center offset: %.1f, %.1f\n",
                      modeXform.centerOffX, modeXform.centerOffY);
    }

    // Quantity — stack copies vertically
    if (plotter.mode == MODE_QUANTITY && plotter.quantityCopies > 1) {
        modeXform.replayRemaining = plotter.quantityCopies - 1;
        if (modeXform.bbValid) {
            float h = (modeXform.bbMaxY - modeXform.bbMinY) * modeXform.scaleY;
            modeXform.replayOffY = h + 2.0f;
        } else {
            modeXform.replayOffY = 10.0f;
        }
        Serial.printf("// quantity: %d copies, Y-offset %.1f\n",
                      plotter.quantityCopies, modeXform.replayOffY);
    }

    // Auto Fill — tile to fill mat
    if (plotter.mode == MODE_AUTOFILL) {
        modeXform.replayRemaining = AUTOFILL_MAX_COPIES;
        modeXform.autoFillCol = 0;
        modeXform.autoFillRow = 0;
        if (modeXform.bbValid) {
            float cw = (modeXform.bbMaxX - modeXform.bbMinX) * modeXform.scaleX;
            float ch = (modeXform.bbMaxY - modeXform.bbMinY) * modeXform.scaleY;
            if (cw < 1) cw = 10; if (ch < 1) ch = 10;
            modeXform.autoFillCols = (int)(MAT_WIDTH_MM[plotter.matSize] / (cw + 2));
            if (modeXform.autoFillCols < 1) modeXform.autoFillCols = 1;
            int rows = (int)(MAT_HEIGHT_MM[plotter.matSize] / (ch + 2));
            if (rows < 1) rows = 1;
            int total = modeXform.autoFillCols * rows;
            if (total > AUTOFILL_MAX_COPIES) total = AUTOFILL_MAX_COPIES;
            modeXform.replayRemaining = total - 1;
            modeXform.replayOffY = ch + 2.0f;
            modeXform.autoFillOffX = cw + 2.0f;
            Serial.printf("// auto-fill: %dx%d = %d copies\n",
                          modeXform.autoFillCols, rows, total);
        } else {
            modeXform.replayOffY = 10.0f;
            modeXform.autoFillOffX = 10.0f;
            modeXform.autoFillCols = 1;
        }
    }
}

// Apply active mode/function transforms to a move coordinate
static void applyMoveTransform(float &x, float &y) {
    // 1. Flip (mirror X)
    if (plotter.funcs.flip) {
        x = X_MAX_MM - x;
    }

    // 2. Portrait (90° CW rotation around origin)
    if (plotter.mode == MODE_PORTRAIT) {
        float tmp = x;
        x = y;
        y = X_MAX_MM - tmp;
    }

    // 3. Fit to Page / Fit to Length scaling
    if (plotter.mode == MODE_FITPAGE || plotter.mode == MODE_FITLENGTH) {
        x *= modeXform.scaleX;
        y *= modeXform.scaleY;
    }

    // 4. Center Point offset
    if (plotter.funcs.centerPoint) {
        x += modeXform.centerOffX;
        y += modeXform.centerOffY;
    }
}

void handleMenuCmd(const char* cmd) {
    if (strcmp(cmd, "up") == 0) menu.up();
    else if (strcmp(cmd, "down") == 0) menu.down();
    else if (strcmp(cmd, "ok") == 0 || strcmp(cmd, "select") == 0) menu.select();
    else if (strcmp(cmd, "back") == 0 || strcmp(cmd, "exit") == 0) menu.back();
}

void processLine(const char* line, size_t len) {
    const char* p = line;
    while (*p == ' ') p++;
    if (!*p || *p == ';' || *p == '(' || *p == '\n' || *p == '\r') return;

    if (line[0] == '$') {
        if (strncmp(line, "$upload ", 8) == 0) {
            if (len > 8) {
                handleUpload(line + 8, len - 8);
            }
        } else if (strcmp(line, "$files") == 0) {
            usbDrive.listFiles(Serial);
        } else if (strcmp(line, "$pause") == 0) {
            if (state == PLAYING_SD) { state = PAUSED; }
        } else if (strcmp(line, "$resume") == 0) {
            if (state == PAUSED) { state = PLAYING_SD; }
        } else if (strcmp(line, "$stop") == 0) {
            psramBuf.clear();
            state = IDLE;
            stepper.stop();
        } else if (strcmp(line, "$pressure") == 0) {
            int pct = (gcodePressure >= 0) ? gcodePressure : potPressure;
            Serial.printf("// pressure: %d%%\n", pct);
        } else if (strcmp(line, "$menu") == 0 || strcmp(line, "$exit") == 0) {
            menuActive = !menuActive;
            display.showMenu(menuActive);
            if (menuActive) menu.begin();
            Serial.printf("// menu %s\n", menuActive ? "ON" : "OFF");
        } else if (strncmp(line, "$menu ", 6) == 0) {
            handleMenuCmd(line + 6);
        } else if (strcmp(line, "$hpgl") == 0) {
            hpglMode = true;
            Serial.println("// HPGL mode ON");
        } else if (strcmp(line, "$gcode") == 0) {
            hpglMode = false;
            Serial.println("// HPGL mode OFF");
        } else if (strcmp(line, "$status") == 0) {
            int pct = (gcodePressure >= 0) ? gcodePressure : potPressure;
            Serial.printf("{\"mode\":%d,\"speed\":%d,\"pressure\":%d,"
                          "\"x\":%.2f,\"y\":%.2f,\"state\":%d,\"zoom\":%.2f}\n",
                          plotter.mode, plotter.speedLevel, plotter.pressureLevel,
                          stepper.currentX(), stepper.currentY(), (int)state.load(), currentZoom);
        } else if (strcmp(line, "$help") == 0) {
            Serial.println("$upload $files $pause $resume $stop $pressure $menu $hpgl $gcode $status");
        }
        return;
    }

    if (line[0] == '?') {
        onReport();
        return;
    }

    // Auto-detect HPGL vs G-code
    bool isHPGL = hpglMode;
    if (!isHPGL) {
        // Heuristic: line starts with 2 uppercase letters (HPGL command)
        const char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (isupper((unsigned char)s[0]) && isupper((unsigned char)s[1])) {
            isHPGL = true;
        }
    }

    if (isHPGL) {
        hpgl.parseLine(line);
    } else {
        parser.parseLine(line);
    }
}

void handleUpload(const char* data, size_t dataLen) {
    const char* colon = (const char*)memchr(data, ':', dataLen);
    if (!colon) {
        Serial.println("error: bad upload format");
        return;
    }
    char filename[64];
    int len = min((int)(colon - data), 63);
    strncpy(filename, data, len);
    filename[len] = '\0';
    const char* content = colon + 1;
    size_t contentLen = dataLen - (content - data);

    // Buffer into PSRAM
    if (!psramBuf.isReady()) {
        Serial.println("error: PSRAM not ready");
        return;
    }
    psramBuf.clear();
    if (contentLen > psramBuf.capacity()) {
        usbError(ERR_FILE_TOO_LARGE);
        return;
    }
    if (!psramBuf.write((const uint8_t*)content, contentLen)) {
        if (psramBuf.isFull()) {
            usbError(ERR_FILE_TOO_LARGE);
        }
        return;
    }
    strncpy(currentFilePath, filename, sizeof(currentFilePath) - 1);
    Serial.printf("// buffered %s (%zu bytes) in PSRAM\n", filename, contentLen);

    // Optionally save to USB drive if available
    if (usbDrive.isReady()) {
        // USB write not implemented yet — saved in PSRAM only
        Serial.println("// file in PSRAM only (USB write TBD)");
    }
}

// --- File playback from PSRAM buffer ---

void playFileFromBuffer() {
    if (!psramBuf.isReady() || psramBuf.size() == 0) {
        state = IDLE;
        return;
    }
    if (state == RUNNING) return;

    size_t end = psramBuf.size();
    char line[GCODE_LINE_MAX];

    while (filePlayOffset < end) {
        int i = 0;
        while (filePlayOffset < end && i < GCODE_LINE_MAX - 1) {
            char c = (char)psramBuf.at(filePlayOffset++);
            if (c == '\n') break;
            if (c == '\r') continue;
            line[i++] = c;
        }
        line[i] = '\0';

        const char* p = line;
        while (*p == ' ') p++;
        if (!*p || *p == ';' || *p == '(') continue;

        processLine(line);

        if (state == RUNNING) return;
    }

    // File fully consumed
    psramBuf.clear();
    state = IDLE;
    Serial.println("// done");
    wifiServer.broadcast("// done");
}

// --- WiFi command callback ---

void onWiFiCmd(const char* msg, size_t len) {
    if (strncmp(msg, "$upload ", 8) == 0) {
        processLine(msg, len);
        return;
    }
    processLine(msg);
    wifiServer.broadcast(msg);
}

// --- Serial handling ---

void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            serialBuf[serialIdx] = '\0';
            if (serialIdx > 0) processLine(serialBuf);
            serialIdx = 0;
        } else if (c != '\r' && serialIdx < GCODE_LINE_MAX - 1) {
            serialBuf[serialIdx++] = c;
        }
    }
}

// ─── Calibration: NVS load/save and detent snapping ────────

static void loadCalibration() {
    Preferences prefs;
    prefs.begin("plotter", true);
    uint8_t flags = prefs.getUChar("cal_flags", 0);
    size_t sz = prefs.getBytes("spd_cal", plotter.calSpeed, sizeof(plotter.calSpeed));
    plotter.calSpeedValid = (flags & 1) && (sz == sizeof(plotter.calSpeed));
    sz = prefs.getBytes("prs_cal", plotter.calPressure, sizeof(plotter.calPressure));
    plotter.calPressureValid = (flags & 2) && (sz == sizeof(plotter.calPressure));
    prefs.end();
    Serial.printf("// cal: speed=%d pressure=%d\n", plotter.calSpeedValid, plotter.calPressureValid);
}

void saveCalSpeed() {
    Preferences prefs;
    prefs.begin("plotter", false);
    prefs.putBytes("spd_cal", plotter.calSpeed, sizeof(plotter.calSpeed));
    uint8_t flags = prefs.getUChar("cal_flags", 0);
    flags |= 1;
    prefs.putUChar("cal_flags", flags);
    prefs.end();
    plotter.calSpeedValid = true;
}

void saveCalPressure() {
    Preferences prefs;
    prefs.begin("plotter", false);
    prefs.putBytes("prs_cal", plotter.calPressure, sizeof(plotter.calPressure));
    uint8_t flags = prefs.getUChar("cal_flags", 0);
    flags |= 2;
    prefs.putUChar("cal_flags", flags);
    prefs.end();
    plotter.calPressureValid = true;
}

void resetCalibration() {
    Preferences prefs;
    prefs.begin("plotter", false);
    prefs.remove("spd_cal");
    prefs.remove("prs_cal");
    prefs.putUChar("cal_flags", 0);
    prefs.end();
    plotter.calSpeedValid = false;
    plotter.calPressureValid = false;
    memset(plotter.calSpeed, 0, sizeof(plotter.calSpeed));
    memset(plotter.calPressure, 0, sizeof(plotter.calPressure));
}

static int snapToDetent(int raw, const uint16_t cal[5]) {
    int bestDetent = 0;
    int bestDist = INT_MAX;
    for (int i = 0; i < 5; i++) {
        int d = abs(raw - (int)cal[i]);
        int r = max(50, (int)(cal[i] * 0.05f));
        if (d <= r && d < bestDist) {
            bestDist = d;
            bestDetent = i;
        }
    }
    if (bestDist == INT_MAX) {
        bestDist = INT_MAX;
        for (int i = 0; i < 5; i++) {
            int d = abs(raw - (int)cal[i]);
            if (d < bestDist) {
                bestDist = d;
                bestDetent = i;
            }
        }
    }
    return bestDetent + 1;
}

static int adcToLevel(int raw, bool useCal, const uint16_t cal[5]) {
    if (useCal) return snapToDetent(raw, cal);
    return constrain((raw * 5 + 4095) / 4096, 1, 5);
}

// --- Potentiometer reading ---

static int smoothPot = -1;

void updatePressure() {
    int raw = analogRead(POT_PIN);
    if (smoothPot < 0) {
        smoothPot = raw;
    } else {
        smoothPot += (raw - smoothPot) >> POT_LOOKBACK;
    }
    int level = adcToLevel(smoothPot, plotter.calPressureValid, plotter.calPressure);
    if (level != (int)plotter.pressureLevel) {
        plotter.pressureLevel = level;
        display.setPressureLevel(level);
        int pct = (level - 1) * 25;  // 0, 25, 50, 75, 100
        potPressure = pct;
        if (solenoidOn && gcodePressure < 0) {
            int duty = map(pct, 0, 100, 0, 255);
            ledcWrite(SOLENOID_PWM_CH, duty);
        }
        display.setPressure(pct);
    }
}

// --- Non-blocking Button handling ---

void handleButtons() {
    static unsigned long lastBtnTime = 0;
    unsigned long now = millis();
    if (now - lastBtnTime < BTN_DEBOUNCE_MS) return;

#if BTN_UP_PIN < 255
    static bool lastUp = HIGH;
    bool up = digitalRead(BTN_UP_PIN);
    if (up == LOW && lastUp == HIGH) {
        if (menuActive) menu.up();
        lastBtnTime = now;
    }
    lastUp = up;
#endif

#if BTN_DOWN_PIN < 255
    static bool lastDown = HIGH;
    bool down = digitalRead(BTN_DOWN_PIN);
    if (down == LOW && lastDown == HIGH) {
        if (menuActive) menu.down();
        lastBtnTime = now;
    }
    lastDown = down;
#endif

#if BTN_SELECT_PIN < 255
    static bool lastSel = HIGH;
    bool sel = digitalRead(BTN_SELECT_PIN);
    if (sel == LOW && lastSel == HIGH) {
        if (menuActive) menu.select();
        lastBtnTime = now;
    }
    lastSel = sel;
#endif

#if BTN_BACK_PIN < 255
    static bool lastBack = HIGH;
    bool back = digitalRead(BTN_BACK_PIN);
    if (back == LOW && lastBack == HIGH) {
        if (menuActive) menu.back();
        else { menuActive = true; display.showMenu(true); menu.begin(); }
        lastBtnTime = now;
    }
    lastBack = back;
#endif
}

// --- Keyboard handling (Plotter Expression full keymap) ---

#if KBD_ENABLE
static unsigned long lastKbdScan = 0;

void handlePlotterKey(int key) {
    beep(BEEP_FREQ, BEEP_SHORT_MS);

    // ── Global keys (work even in menu) ──
    switch (key) {
        case KEY_SOUNDONOFF:
            plotter.soundOn = !plotter.soundOn;
            Serial.printf("// sound %s\n", plotter.soundOn ? "ON" : "OFF");
            return;
        case KEY_SETTINGS:
            menuActive = !menuActive;
            display.showMenu(menuActive);
            if (menuActive) menu.begin();
            return;
        default:
            break;
    }

    // ── Menu mode: route navigation keys ──
    if (menuActive) {
        // If menu is in text editing mode, route ASCII keys
        if (menu.isEditing()) {
            if (key == KEY_OK || key == KEY_MOVERIGHT) {
                menu.feedConfirm();
            } else if (key == KEY_MOVELEFT || key == KEY_SETTINGS) {
                menu.feedCancel();
            } else if (key == KEY_BACKSPACE) {
                menu.feedBackspace();
            } else if (key >= KEY_A && key <= KEY_Z) {
                char c = 'A' + (key - KEY_A);
                if (shiftMode) c = tolower(c);
                menu.feedChar(c);
                if (shiftMode == 1) shiftMode = 0;
                plotter.shiftState = shiftMode;
            } else if (key >= KEY_0 && key <= KEY_9) {
                menu.feedChar('0' + (key - KEY_0));
            } else if (key == KEY_SPACE) {
                menu.feedChar(' ');
            } else if (key == KEY_MINUS) {
                menu.feedChar('-');
            } else if (key == KEY_PERIOD) {
                menu.feedChar('.');
            }
            return;
        }
        switch (key) {
            case KEY_MOVEUP:        menu.up();     break;
            case KEY_MOVEDN:        menu.down();   break;
            case KEY_MOVELEFT:      menu.back();   break;
            case KEY_MOVERIGHT:
            case KEY_CUT:
            case KEY_OK:            menu.select(); break;
            default:                                  break;
        }
        return;
    }

    // ── Status mode: Plotter Expression key actions ──
    switch (key) {
        // ── Blade Navigation (8-way) ──
        // (0,0) = upper-left; ▲ decreases Y, ▼ increases Y
        case KEY_MOVEUPLEFT:
            plotter.bladeX -= 1.0f; plotter.bladeY -= 1.0f; break;
        case KEY_MOVEUP:
            plotter.bladeY -= 1.0f; break;
        case KEY_MOVEUPRIGHT:
            plotter.bladeX += 1.0f; plotter.bladeY -= 1.0f; break;
        case KEY_MOVELEFT:
            plotter.bladeX -= 1.0f; break;
        case KEY_MOVERIGHT:
            plotter.bladeX += 1.0f; break;
        case KEY_MOVEDNLEFT:
            plotter.bladeX -= 1.0f; plotter.bladeY += 1.0f; break;
        case KEY_MOVEDN:
            plotter.bladeY += 1.0f; break;
        case KEY_MOVEDNRIGHT:
            plotter.bladeX += 1.0f; plotter.bladeY += 1.0f; break;

        // ── Shift / Shift Lock ──
        case KEY_SHIFT:
            shiftMode = (shiftMode == 1) ? 0 : 1;
            plotter.shiftState = shiftMode;
            break;
        case KEY_CUT_SHIFTLOCK:
            shiftMode = (shiftMode == 2) ? 0 : 2;
            plotter.shiftLock = (shiftMode == 2);
            plotter.shiftState = shiftMode;
            break;

        // ── Text entry ──
        case KEY_SPACE:
            if (plotter.charCount < MAX_ONSCREEN_CHARS) {
                plotter.chars[plotter.charCount++] = ' ';
                plotter.chars[plotter.charCount] = '\0';
            }
            break;
        case KEY_BACKSPACE:
            if (plotter.charCount > 0) {
                plotter.charCount--;
                plotter.chars[plotter.charCount] = '\0';
            }
            break;
        case KEY_CHARDISPLAY:
            memset(plotter.chars, 0, MAX_ONSCREEN_CHARS);
            plotter.charCount = 0;
            plotter.cursorPos = 0;
            Serial.println("// display cleared");
            break;
        case KEY_RESETALL:
            memset(plotter.chars, 0, MAX_ONSCREEN_CHARS);
            plotter.charCount = 0;
            plotter.cursorPos = 0;
            plotter.mode = MODE_LANDSCAPE;
            plotter.funcs = PlotterFunctions{};
            plotter.quantityCopies = 1;
            plotter.sizeInches = SIZE_DEFAULT_INCHES;
            memset(&modeXform, 0, sizeof(modeXform));
            shiftMode = 0;
            plotter.shiftState = 0;
            plotter.shiftLock = false;
            Serial.println("// all reset");
            break;
        case KEY_REPEATLAST:
            // Re-execute last file from buffer
            if (psramBuf.size() > 0) {
                filePlayOffset = 0;
                state = PLAYING_SD;
            }
            break;

        // ── Paper / Mat handling ──
        case KEY_LOADMAT:
            Serial.println("// load mat");
            break;
        case KEY_UNLOADMAT:
            Serial.println("// unload mat");
            break;
        case KEY_MATSIZE:
            plotter.matSize = (plotter.matSize == MAT_12X12) ? MAT_12X24 : MAT_12X12;
            Serial.printf("// mat size: %s\n", MAT_LABELS[plotter.matSize]);
            break;
        case KEY_SETCUTAREA:
            Serial.println("// set cut area");
            break;
        case KEY_LOADLAST:
            // Re-plot last file
            break;

        // ── Mode keys ──
        // Per manual: size modes (Fit to Page, Fit to Length, Auto Fill)
        // are mutually exclusive; Portrait, Mix 'n Match, Quantity can combine.
        case KEY_PORTRAIT:
            plotter.mode = (plotter.mode == MODE_PORTRAIT) ? MODE_LANDSCAPE : MODE_PORTRAIT;
            Serial.printf("// mode: %s\n", MODE_LABELS[plotter.mode]);
            break;
        case KEY_FITPAGE:
            if (plotter.mode != MODE_FITPAGE) {
                plotter.mode = MODE_FITPAGE;
                // Deselect other size modes
                plotter.quantityCopies = 1;
                plotter.autoFillCopies = AUTOFILL_MAX_COPIES;
            } else {
                plotter.mode = MODE_LANDSCAPE;
            }
            Serial.printf("// mode: %s\n", MODE_LABELS[plotter.mode]);
            break;
        case KEY_FITLENGTH:
            if (plotter.mode != MODE_FITLENGTH) {
                plotter.mode = MODE_FITLENGTH;
                plotter.quantityCopies = 1;
                plotter.autoFillCopies = AUTOFILL_MAX_COPIES;
            } else {
                plotter.mode = MODE_LANDSCAPE;
            }
            Serial.printf("// mode: %s\n", MODE_LABELS[plotter.mode]);
            break;
        case KEY_MIXMATCH:
            plotter.mode = (plotter.mode == MODE_MIXMATCH) ? MODE_LANDSCAPE : MODE_MIXMATCH;
            Serial.printf("// mode: %s\n", MODE_LABELS[plotter.mode]);
            break;
        case KEY_QUANTITY:
            if (plotter.mode != MODE_QUANTITY) {
                plotter.mode = MODE_QUANTITY;
                if (plotter.quantityCopies < 2) plotter.quantityCopies = 2;
                // Size modes off when using Quantity
                plotter.autoFillCopies = AUTOFILL_MAX_COPIES;
            } else {
                plotter.mode = MODE_LANDSCAPE;
            }
            Serial.printf("// mode: %s qty=%d\n", MODE_LABELS[plotter.mode], plotter.quantityCopies);
            break;
        case KEY_AUTOFILL:
            if (plotter.mode != MODE_AUTOFILL) {
                plotter.mode = MODE_AUTOFILL;
                // Size modes are mutually exclusive
                plotter.quantityCopies = 1;
            } else {
                plotter.mode = MODE_LANDSCAPE;
            }
            Serial.printf("// mode: %s\n", MODE_LABELS[plotter.mode]);
            break;
        case KEY_MATERIALSAVER:
            plotter.funcs.paperSaver = !plotter.funcs.paperSaver;
            Serial.printf("// paper saver %s\n", plotter.funcs.paperSaver ? "ON" : "OFF");
            break;

        // ── Function keys ──
        case KEY_MULTICUT:
            {
                uint8_t next = (plotter.funcs.multiCut + 1) % 4; // 0→1→2→3→0
                plotter.funcs.multiCut = next;
                const char* labels[] = {"Off", "2x", "3x", "4x"};
                Serial.printf("// multi cut: %s\n", labels[next]);
            }
            break;
        case KEY_CENTERPOINT:
            // Center Point only available in Landscape mode (per manual)
            if (plotter.mode == MODE_LANDSCAPE) {
                plotter.funcs.centerPoint = !plotter.funcs.centerPoint;
                Serial.printf("// center point %s\n", plotter.funcs.centerPoint ? "ON" : "OFF");
            } else {
                Serial.println("// center point requires landscape mode");
            }
            break;
        case KEY_LINERETURN:
            plotter.funcs.lineReturn = !plotter.funcs.lineReturn;
            break;
        case KEY_FLIP:
            plotter.funcs.flip = !plotter.funcs.flip;
            break;

        // ── Cut / Execute ──
        case KEY_CUT:
            startCut();
            break;

        // ── Menu / OK ──
        case KEY_OK:
            menuActive = true;
            display.showMenu(true);
            menu.begin();
            break;

        // ── Unmapped keys ──
        default:
            // Letter keys (A-Z, 0-9) go into character buffer
            if (key >= KEY_A && key <= KEY_Z) {
                char c = 'A' + (key - KEY_A);
                if (shiftMode) c = tolower(c);
                if (plotter.charCount < MAX_ONSCREEN_CHARS) {
                    plotter.chars[plotter.charCount++] = c;
                    plotter.chars[plotter.charCount] = '\0';
                }
                if (shiftMode == 1) shiftMode = 0; // momentary shift
                plotter.shiftState = shiftMode;
            } else if (key >= KEY_0 && key <= KEY_9) {
                char c = '0' + (key - KEY_0);
                if (plotter.charCount < MAX_ONSCREEN_CHARS) {
                    plotter.chars[plotter.charCount++] = c;
                    plotter.chars[plotter.charCount] = '\0';
                }
            }
            break;
    }

    // Constrain blade position to mat bounds
    float maxX = MAT_WIDTH_MM[plotter.matSize];
    float maxY = MAT_HEIGHT_MM[plotter.matSize];
    plotter.bladeX = constrain(plotter.bladeX, 0, maxX);
    plotter.bladeY = constrain(plotter.bladeY, 0, maxY);
    display.setPosition(plotter.bladeX, plotter.bladeY);
}

void handleKeyboard() {
    unsigned long now = millis();
    if (now - lastKbdScan < KBD_DEBOUNCE_MS) return;
    lastKbdScan = now;

    int key = kbd.scan();
    if (key < 0) return;

    handlePlotterKey(key);
}
#endif

// --- Speed potentiometer ---

void updateSpeed() {
    int raw = analogRead(SPEED_PIN);
    if (smoothSpeed < 0) {
        smoothSpeed = raw;
    } else {
        smoothSpeed += (raw - smoothSpeed) >> SPEED_LOOKBACK;
    }
    int level = adcToLevel(smoothSpeed, plotter.calSpeedValid, plotter.calSpeed);
    if (level != (int)plotter.speedLevel) {
        plotter.speedLevel = level;
        display.setSpeedLevel(level);
        currentFeed = HOMING_FEED + (MAX_FEEDRATE - HOMING_FEED) * (level - 1) / 4.0f;
        stepper.setMaxFeedrateOverride(currentFeed);
    }
}

// --- Quadrature encoder for zoom control ---

void updateEncoder() {
    uint8_t a = digitalRead(ENC_A_PIN);
    uint8_t b = digitalRead(ENC_B_PIN);
    uint8_t curr = (a << 1) | b;
    static const int8_t table[16] = {
        0,  1, -1,  0,
       -1,  0,  0,  1,
        1,  0,  0, -1,
        0, -1,  1,  0
    };
    int8_t inc = table[(encPrev << 2) | curr];
    if (inc) {
        encPrev = curr;
        float newZoom = currentZoom + inc * ZOOM_STEP;
        newZoom = constrain(newZoom, ZOOM_MIN, ZOOM_MAX);
        if (fabsf(newZoom - currentZoom) > 0.001f) {
            currentZoom = newZoom;
            display.setZoom(currentZoom);
        }
    } else {
        encPrev = curr;
    }
}

// --- Core 0: Motion control task ---

void motionTask(void *pvParameters) {
    while (true) {
        if (state.load() == RUNNING) {
            stepper.run();
            if (!stepper.isRunning()) {
                state.store(IDLE);
                moveComplete.store(true);
            }
        }
        vTaskDelay(1);
    }
}

// --- Menu plot callback ---

void onMenuPlot(const char* path) {
    strncpy(currentFilePath, path, sizeof(currentFilePath) - 1);
    const char* ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".svg") == 0)) {
        plotSVG(path);
    } else {
        onFile(path);
    }
}

// ─── Firmware update via USB pendrive ──────────────────────────

static void performFirmwareUpdate(const char* path) {
    state = IDLE;
    stepper.stop();

    display.showFwUpdate("Opening...", 0);
    display.update();

    if (!usbDrive.openFile(path)) {
        display.showFwUpdate("FAIL: cannot open", 0);
        display.update();
        delay(1500);
        display.clearFwUpdate();
        display.showError("Cannot open file");
        return;
    }

    size_t fwSize = usbDrive.currentFileSize();
    char msg[32];
    snprintf(msg, sizeof(msg), "Size: %u KB", (unsigned)(fwSize / 1024));
    display.showFwUpdate(msg, 0);
    display.update();

    if (!Update.begin(fwSize)) {
        usbDrive.closeFile();
        display.showFwUpdate("FAIL: OTA begin", 0);
        display.update();
        delay(1500);
        display.clearFwUpdate();
        display.showError("OTA begin failed");
        return;
    }

    display.showFwUpdate("Flashing...", 0);
    display.update();

    uint8_t buf[512];
    size_t written = 0;
    bool ok = true;

    while (written < fwSize) {
        size_t toRead = min(sizeof(buf), fwSize - written);
        size_t got = usbDrive.readFile(buf, toRead);
        if (got == 0) { ok = false; break; }
        if (Update.write(buf, got) != got) { ok = false; break; }
        written += got;
        int pct = (int)(written * 100 / fwSize);
        snprintf(msg, sizeof(msg), "%d%% (%u/%u KB)", pct,
                 (unsigned)(written / 1024), (unsigned)(fwSize / 1024));
        display.showFwUpdate(msg, pct);
        display.update();
        pollUSB();
    }

    usbDrive.closeFile();

    if (!ok) {
        Update.abort();
        display.showFwUpdate("FAIL: write error", 0);
        display.update();
        delay(1500);
        display.clearFwUpdate();
        display.showError("Firmware write failed");
        return;
    }

    if (!Update.end()) {
        display.showFwUpdate("FAIL: validation", 0);
        display.update();
        delay(1500);
        display.clearFwUpdate();
        display.showError("Update validation failed");
        return;
    }

    display.showFwUpdate("Success! Rebooting...", 100);
    display.update();
    delay(2000);
    ESP.restart();
}

void onFwUpdate(const char* path) {
    menuActive = false;
    display.showMenu(false);
    performFirmwareUpdate(path);
    menuActive = true;
    display.showMenu(true);
    menu.begin();
}

// --- Setup ---

void setup() {
    Serial.begin(115200);
    Serial.printf("\nPlotter Firmware v%s (build %d)\n", FIRMWARE_VERSION, FIRMWARE_BUILD);
    Serial.println("Type $help for commands");

    pinMode(ENDSTOP_PIN, INPUT_PULLUP);
#if !KBD_ENABLE
  #if BTN_UP_PIN < 255
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
  #endif
  #if BTN_DOWN_PIN < 255
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  #endif
  #if BTN_SELECT_PIN < 255
    pinMode(BTN_SELECT_PIN, INPUT_PULLUP);
  #endif
  #if BTN_BACK_PIN < 255
    pinMode(BTN_BACK_PIN, INPUT_PULLUP);
  #endif
#endif

    ledcSetup(SOLENOID_PWM_CH, SOLENOID_PWM_FREQ, SOLENOID_PWM_RES);
    ledcAttachPin(SOLENOID_PIN, SOLENOID_PWM_CH);
    ledcWrite(SOLENOID_PWM_CH, 0);

    stepper.init();

    // PSRAM buffer for file operations
    if (!psramBuf.begin()) {
        Serial.printf("error: %s\n", psramBuf.errorMsg());
    } else {
        Serial.printf("PSRAM buffer ready (%u bytes)\n", psramBuf.capacity());
    }

    // USB flash drive
    if (usbDrive.begin()) {
        Serial.println("USB host started");
    } else {
        Serial.println("USB host init failed");
    }

    GCodeParser::Callbacks cb;
    cb.onMove = onMove;
    cb.onSetSpeed = onSetSpeed;
    cb.onHome = onHome;
    cb.onSolenoid = onSolenoid;
    cb.onReport = onReport;
    cb.onDwell = onDwell;
    cb.onFile = onFile;
    cb.onError = onError;
    parser.begin(cb);

    {
        HPGLParser::Callbacks hcb;
        hcb.onMove = onMove;
        hcb.onSolenoid = onSolenoid;
        hcb.onReport = onReport;
        hcb.onError = onError;
        hpgl.begin(hcb);
        Serial.println("HPGL ready");
    }

    // Wire Plotter UI state
    display.setPlotterState(&plotter);
    menu.setPlotterState(&plotter);

    // Initialize WiFi credentials from config.h defaults, then NVS override
    strncpy(plotter.wifiSSID, WIFI_SSID, sizeof(plotter.wifiSSID) - 1);
    strncpy(plotter.wifiPass, WIFI_PASS, sizeof(plotter.wifiPass) - 1);
    {
        Preferences prefs;
        prefs.begin("plotter", true);
        String ssid = prefs.getString("wifi_ssid", "");
        String pass = prefs.getString("wifi_pass", "");
        if (ssid.length() > 0) {
            strncpy(plotter.wifiSSID, ssid.c_str(), sizeof(plotter.wifiSSID) - 1);
        }
        if (pass.length() > 0) {
            strncpy(plotter.wifiPass, pass.c_str(), sizeof(plotter.wifiPass) - 1);
        }
        prefs.end();
    }

    // Load calibration from NVS
    loadCalibration();

    memset(&modeXform, 0, sizeof(modeXform));

    display.begin();
    display.setState("IDLE");
    display.setPressure(DEFAULT_PRESSURE);
    display.setZoom(currentZoom);
    display.setSpeedLevel(plotter.speedLevel);
    display.setPressureLevel(plotter.pressureLevel);
    display.setSizeInches(plotter.sizeInches);

#if BUZZER_PIN >= 0
    ledcSetup(BUZZER_PWM_CH, BUZZER_PWM_FREQ, BUZZER_PWM_RES);
    ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CH);
#endif
    beep(BEEP_FREQ, BEEP_SHORT_MS);
    delay(100);
    beep(BEEP_FREQ, BEEP_LONG_MS);

#if KBD_ENABLE
    kbd.begin();
    Serial.println("Keyboard OK");
#endif

    menu.begin();
    menu.setPlotCb(onMenuPlot);
    menu.setFwUpdateCb(onFwUpdate);
    menu.setUSBDrive(&usbDrive);

    // Quadrature encoder for zoom
    pinMode(ENC_A_PIN, INPUT_PULLUP);
    pinMode(ENC_B_PIN, INPUT_PULLUP);
    encPrev = (digitalRead(ENC_A_PIN) << 1) | digitalRead(ENC_B_PIN);

    // Speed pot initial read
    pinMode(SPEED_PIN, INPUT);
    updateSpeed();

    wifiServer.begin(plotter.wifiSSID, plotter.wifiPass, WIFI_AP_MODE);
    wifiServer.setCmdCallback(onWiFiCmd);

    display.setIP(WiFi.softAPIP().toString().c_str());

    xTaskCreatePinnedToCore(
        motionTask,
        "motion",
        8192,
        NULL,
        1,
        NULL,
        0
    );
}

// --- Main loop (Core 1) ---

void loop() {
    handleSerial();
    updatePressure();
    updateSpeed();
    updateEncoder();
#if !KBD_ENABLE
    handleButtons();
#else
    handleKeyboard();
#endif
    wifiServer.handleClient();

    // Poll USB host stack (required for native USB OTG host)
    pollUSB();

    // Restart WiFi if credentials changed via menu
    if (plotter.wifiChanged) {
        plotter.wifiChanged = false;
        Serial.printf("// restarting WiFi with SSID: %s\n", plotter.wifiSSID);
        wifiServer.begin(plotter.wifiSSID, plotter.wifiPass, WIFI_AP_MODE);
        display.setIP(WiFi.softAPIP().toString().c_str());
    }

    updateBeep();

    // Update display (fw update > error > menu > status)
    if (display.isFwUpdateMode()) {
        display.update();
    } else if (display.isErrorMode()) {
        display.update();
    } else if (menuActive) {
        display.drawMenu(menu);
    } else {
        display.update();
    }

    // STOP button check (dedicated pin, not in matrix)
#if KBD_ENABLE
    if (kbd.stopPressed()) {
        State s = state.load();
        if (s == RUNNING || s == PLAYING_SD || s == PAUSED) {
            psramBuf.clear();
            state.store(IDLE);
            stepper.stop();
            beep(BEEP_FREQ, BEEP_LONG_MS);
            Serial.println("// stopped");
        }
    }
#endif

    bool mc = moveComplete.exchange(false);
    if (mc) {
        display.setPosition(stepper.currentX(), stepper.currentY());
        Serial.println("ok");
    }

    // ── KNIFE_PIVOT: after pivot move completes, lower blade and continue ──
    if (state.load() == KNIFE_PIVOT && mc) {
        knifeExecutePending();
        if (!knifeHasPendingMove()) {
            state.store(RUNNING);
        }
    }

    switch (state.load()) {
        case DWELL:
            if (millis() >= dwellUntil) {
                state.store(IDLE);
                Serial.println("ok");
            }
            break;

        case PLAYING_SD: {
            playFileFromBuffer();
            // File finished — apply post-cut transforms + replay (non-blocking)
            if (state.load() == IDLE) {
                // ── Line Return: go back to X origin ──
                if (plotter.funcs.lineReturn && stepper.isHomed()) {
                    stepper.setTarget(0, stepper.currentY(), DEFAULT_FEED);
                    state.store(RUNNING);
                    postCutAction = POST_LINE_RETURN;
                    break;
                }

                // ── Multi-cut pass ──
                if (plotter.cutsRemaining > 0) {
                    plotter.cutsRemaining--;
                    if (plotter.cutsRemaining > 0 && currentFilePath[0]) {
                        bool ok = false;
                        if (usbDrive.isReady()) {
                            ok = usbDrive.loadFile(currentFilePath, psramBuf);
                        }
                        if (ok) {
                            filePlayOffset = 0;
                            postCutAction = POST_MULTI_CUT;
                            state.store(PLAYING_SD);
                            Serial.printf("// multi-cut pass %d/%d\n",
                                          plotter.cutsRemaining,
                                          plotter.funcs.multiCut + 1);
                            break;
                        }
                    }
                }

                // ── Quantity / Auto Fill replay ──
                if (modeXform.replayRemaining > 0 && currentFilePath[0]) {
                    modeXform.replayRemaining--;
                    bool ok = false;
                    if (usbDrive.isReady()) {
                        ok = usbDrive.loadFile(currentFilePath, psramBuf);
                    }
                    if (ok) {
                        onSolenoid(false, 0);
                        float copyOffX = 0, copyOffY = modeXform.replayOffY;
                        if (plotter.mode == MODE_AUTOFILL && modeXform.autoFillCols > 1) {
                            modeXform.autoFillCol++;
                            if (modeXform.autoFillCol >= modeXform.autoFillCols) {
                                modeXform.autoFillCol = 0;
                                modeXform.autoFillRow++;
                            }
                            copyOffX = modeXform.autoFillCol * modeXform.autoFillOffX;
                            copyOffY = modeXform.autoFillRow * modeXform.replayOffY;
                        }
                        float targetX = copyOffX;
                        float targetY = constrain(stepper.currentY() + copyOffY, 0, Y_MAX_MM);
                        stepper.setTarget(constrain(targetX, 0, X_MAX_MM), targetY, DEFAULT_FEED * 2);
                        state.store(RUNNING);
                        postCutAction = POST_REPLAY_COPY;
                        break;
                    }
                }
                plotter.cutsRemaining = 0;
                modeXform.replayRemaining = 0;
                postCutAction = POST_NONE;
            }
            break;
        }

        case PAUSED:
            stepper.enable(false);
            break;

        default:
            break;
    }

    // Non-blocking post-cut continuation
    if (mc && postCutAction != POST_NONE) {
        if (postCutAction == POST_LINE_RETURN) {
            postCutAction = POST_NONE;
            if (plotter.cutsRemaining > 0 && currentFilePath[0]) {
                bool ok = usbDrive.isReady() ? usbDrive.loadFile(currentFilePath, psramBuf) : false;
                if (ok) {
                    filePlayOffset = 0;
                    state.store(PLAYING_SD);
                    return;
                }
            }
            if (modeXform.replayRemaining > 0 && currentFilePath[0]) {
                bool ok = usbDrive.isReady() ? usbDrive.loadFile(currentFilePath, psramBuf) : false;
                if (ok) {
                    onSolenoid(false, 0);
                    float offY = modeXform.replayOffY;
                    float ty = constrain(stepper.currentY() + offY, 0, Y_MAX_MM);
                    stepper.setTarget(0, ty, DEFAULT_FEED * 2);
                    state.store(RUNNING);
                    postCutAction = POST_REPLAY_COPY;
                    return;
                }
            }
            plotter.cutsRemaining = 0;
            modeXform.replayRemaining = 0;
        } else if (postCutAction == POST_REPLAY_COPY) {
            postCutAction = POST_NONE;
            filePlayOffset = 0;
            state.store(PLAYING_SD);
            Serial.printf("// replay copy %d\n", modeXform.replayRemaining + 1);
        }
    }
}
