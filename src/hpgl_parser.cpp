#include "hpgl_parser.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

// Map pen number (1-8) to a pressure percentage.
// Clamped at runtime, so we allow up to 16 pens here.
static const uint8_t PEN_PRESSURE[] = HPGL_PEN_PRESSURE;
#define PEN_COUNT (sizeof(PEN_PRESSURE) / sizeof(PEN_PRESSURE[0]))

static float parseNum(const char*& p) {
    while (*p && *p != '-' && *p != '+' && !(*p >= '0' && *p <= '9') && *p != '.') p++;
    if (!*p) return 0;
    char buf[24];
    int i = 0;
    if (*p == '-' || *p == '+') buf[i++] = *p++;
    while (*p && ((*p >= '0' && *p <= '9') || *p == '.') && i < 22)
        buf[i++] = *p++;
    buf[i] = '\0';
    return atof(buf);
}

void HPGLParser::begin(const Callbacks& cb) {
    _cb = cb;
    _x = _y = 0;
    _penDown = false;
    _pen = 1;
    _scale = HPGL_DEFAULT_SCALE;
    _ipW = HPGL_DEFAULT_IP_W;
    _ipH = HPGL_DEFAULT_IP_H;
    _scSet = false;
}

float HPGLParser::hpglToMM(float hpgl) const {
    // Apply user-unit scaling if SC was set
    if (_scSet) {
        float u = (hpgl - _scX1) / (_scX2 - _scX1) * _ipW;
        float v = (hpgl - _scY1) / (_scY2 - _scY1) * _ipH;
        return fmax(u, v) * _scale / HPGL_UNITS_PER_MM;
    }
    return hpgl * _scale / HPGL_UNITS_PER_MM;
}

void HPGLParser::executeMove(float x, float y) {
    x = constrain(x, 0.0f, X_MAX_MM);
    y = constrain(y, 0.0f, Y_MAX_MM);
    _x = x;
    _y = y;
    if (_cb.onMove) _cb.onMove(x, y, _speed);
}

bool HPGLParser::parseLine(const char* line) {
    while (*line == ' ') line++;
    if (!*line || *line == ';' || *line == '(' || *line == '\n' || *line == '\r')
        return true;

    // A line can contain multiple HPGL commands separated by ';'.
    // We split on ';' and process each segment.
    char buf[GCODE_LINE_MAX];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    const char* seg = buf;
    while (*seg) {
        // Skip leading whitespace and semicolons
        while (*seg == ' ' || *seg == ';') seg++;
        if (!*seg) break;

        // Read 2-letter command (uppercase)
        char cmd[3] = {0};
        cmd[0] = toupper((unsigned char)*seg++);
        cmd[1] = toupper((unsigned char)*seg++);
        if (cmd[0] < 'A' || cmd[0] > 'Z' || cmd[1] < 'A' || cmd[1] > 'Z') {
            // Not a valid HPGL command – skip to next ';'
            while (*seg && *seg != ';') seg++;
            continue;
        }

        // Find end of this command segment (next ';' or end of string)
        const char* end = seg;
        while (*end && *end != ';') end++;

        // Create a temporary copy of the arguments for this command
        char args[GCODE_LINE_MAX];
        size_t alen = min((size_t)(end - seg), sizeof(args) - 1);
        strncpy(args, seg, alen);
        args[alen] = '\0';
        const char* a = args;

        // Dispatch command
        bool handled = true;

        if (strcmp(cmd, "IN") == 0) {
            // Initialize
            _x = _y = 0;
            _penDown = false;
            _pen = 1;
        }
        else if (strcmp(cmd, "PU") == 0) {
            // Pen Up
            _penDown = false;
            if (_cb.onSolenoid) _cb.onSolenoid(false, -1);
            // If coordinates follow, move with pen up
            float px = parseNum(a);
            if (*a == ',' || (*a && *a != '\0' && *(a) != ';')) {
                a += strspn(a, " ,\t");
                float py = parseNum(a);
                executeMove(hpglToMM(px), hpglToMM(py));
            }
        }
        else if (strcmp(cmd, "PD") == 0) {
            // Pen Down
            _penDown = true;
            int idx = constrain(_pen - 1, 0, (int)PEN_COUNT - 1);
            float press = PEN_PRESSURE[idx];
            if (_cb.onSolenoid) _cb.onSolenoid(true, press);
            // If coordinates follow, draw with pen down
            float px = parseNum(a);
            if (*a == ',' || (*a && *a != '\0')) {
                a += strspn(a, " ,\t");
                float py = parseNum(a);
                executeMove(hpglToMM(px), hpglToMM(py));
            }
        }
        else if (strcmp(cmd, "PA") == 0) {
            // Plot Absolute
            float px = parseNum(a);
            if (*a == ',' || (*a && *a != '\0')) {
                a += strspn(a, " ,\t");
                float py = parseNum(a);
                executeMove(hpglToMM(px), hpglToMM(py));
            }
        }
        else if (strcmp(cmd, "PR") == 0) {
            // Plot Relative
            float px = parseNum(a);
            if (*a == ',' || (*a && *a != '\0')) {
                a += strspn(a, " ,\t");
                float py = parseNum(a);
                executeMove(hpglToMM(_x + px), hpglToMM(_y + py));
            }
        }
        else if (strcmp(cmd, "SP") == 0) {
            // Select Pen
            int n = (int)parseNum(a);
            _pen = constrain(n, 1, 16);
            // Immediately apply pressure for the new pen if pen is down
            if (_penDown && _cb.onSolenoid) {
                int idx = constrain(_pen - 1, 0, (int)PEN_COUNT - 1);
                _cb.onSolenoid(true, PEN_PRESSURE[idx]);
            }
        }
        else if (strcmp(cmd, "LT") == 0) {
            // Line Type – ignore for pen plotters
        }
        else if (strcmp(cmd, "SI") == 0) {
            // Character width/height – ignore
        }
        else if (strcmp(cmd, "DI") == 0) {
            // Direction – ignore
        }
        else if (strcmp(cmd, "LB") == 0) {
            // Label – skip to terminator and ignore
            const char* lp = args;
            (void)lp;
        }
        else if (strcmp(cmd, "IP") == 0) {
            // Input P1/P2 points: IP x1,y1,x2,y2
            float vals[4];
            int n = 0;
            while (n < 4 && *a) {
                vals[n++] = parseNum(a);
            }
            if (n >= 4) {
                _ipW = vals[2] - vals[0];
                _ipH = vals[3] - vals[1];
            }
        }
        else if (strcmp(cmd, "SC") == 0) {
            // Scale: SC x1,y1,x2,y2
            float vals[4];
            int n = 0;
            while (n < 4 && *a) {
                vals[n++] = parseNum(a);
            }
            if (n >= 4) {
                _scX1 = vals[0]; _scY1 = vals[1];
                _scX2 = vals[2]; _scY2 = vals[3];
                _scSet = true;
            }
        }
        else {
            // Unknown HPGL command – report but continue
            if (_cb.onError) {
                char ebuf[64];
                snprintf(ebuf, sizeof(ebuf), "Unknown HPGL: %s", cmd);
                _cb.onError(ebuf);
            }
            handled = false;
        }

        // Advance segment pointer past this command
        seg = end;
        // Skip the semicolon separator
        if (*seg == ';') seg++;
    }

    return true;
}
