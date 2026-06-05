#include "gcode_parser.h"
#include <stdlib.h>
#include <math.h>

static float getCodeValue(const char* line, char code, float def) {
    char buf[16];
    const char* p = strchr(line, code);
    if (!p) return def;
    p++;
    int i = 0;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    while (*p && (isDigit(*p) || *p == '.' || *p == '-') && i < 15)
        buf[i++] = *p++;
    buf[i] = '\0';
    return atof(buf);
}

// Generate arc points using center/radius method (G2=clockwise, G3=counter-clockwise)
static void generateArc(float cx, float cy, float startX, float startY,
                        float endX, float endY, bool clockwise, float feed,
                        std::function<void(float,float,float)> onMove) {
    float dx0 = startX - cx;
    float dy0 = startY - cy;
    float dx1 = endX - cx;
    float dy1 = endY - cy;
    float angle0 = atan2f(dy0, dx0);
    float angle1 = atan2f(dy1, dx1);

    // Determine total angle
    float totalAngle = angle1 - angle0;
    if (clockwise && totalAngle > 0) totalAngle -= 2.0f * M_PI;
    if (!clockwise && totalAngle < 0) totalAngle += 2.0f * M_PI;

    int segments = max(4, (int)(fabsf(totalAngle) / (2.0f * M_PI) * GCODE_ARC_SEGMENTS));
    float step = totalAngle / segments;
    float r = sqrtf(dx0 * dx0 + dy0 * dy0);

    float x = startX, y = startY;
    for (int i = 1; i <= segments; i++) {
        float a = angle0 + step * i;
        x = cx + r * cosf(a);
        y = cy + r * sinf(a);
        if (onMove) onMove(x, y, feed);
    }
}

// Get I/J or R from line for arc center or radius
static void getArcParams(const char* line, float& i, float& j, float& r) {
    i = getCodeValue(line, 'I', 0);
    j = getCodeValue(line, 'J', 0);
    r = getCodeValue(line, 'R', 0);
}

void GCodeParser::begin(const Callbacks& cb) {
    _cb = cb;
    _absolute = true;
    _x = _y = 0;
    _f = DEFAULT_FEED;
}

bool GCodeParser::parseLine(const char* line) {
    // strip whitespace, skip comments and empty lines
    while (*line == ' ') line++;
    if (!*line || *line == '(' || *line == ';' || *line == '\n' || *line == '\r')
        return true;

    int g = (int)getCodeValue(line, 'G', -1);
    int m = (int)getCodeValue(line, 'M', -1);

    // G codes
    switch (g) {
        case 0: // rapid move
        case 1: // linear move
            float x, y, f;
            if (_absolute) {
                x = getCodeValue(line, 'X', _x);
                y = getCodeValue(line, 'Y', _y);
            } else {
                x = _x + getCodeValue(line, 'X', 0);
                y = _y + getCodeValue(line, 'Y', 0);
            }
            f = getCodeValue(line, 'F', _f);
            x = constrain(x, 0.0f, X_MAX_MM);
            y = constrain(y, 0.0f, Y_MAX_MM);
            _x = x; _y = y; _f = f;
            if (_cb.onMove) _cb.onMove(x, y, f);
            return true;

        case 2: // clockwise arc
        case 3: { // counter-clockwise arc
            float x, y, f, i, j, r;
            if (_absolute) {
                x = getCodeValue(line, 'X', _x);
                y = getCodeValue(line, 'Y', _y);
            } else {
                x = _x + getCodeValue(line, 'X', 0);
                y = _y + getCodeValue(line, 'Y', 0);
            }
            f = getCodeValue(line, 'F', _f);
            getArcParams(line, i, j, r);
            x = constrain(x, 0.0f, X_MAX_MM);
            y = constrain(y, 0.0f, Y_MAX_MM);
            if (r != 0) {
                // R method: compute center from start/end/radius
                float dx = x - _x;
                float dy = y - _y;
                float distSq = dx * dx + dy * dy;
                float h = sqrtf(fmaxf(0, r * r - distSq / 4.0f));
                float mx = (_x + x) / 2.0f;
                float my = (_y + y) / 2.0f;
                float px = -dy / sqrtf(distSq) * h;
                float py = dx / sqrtf(distSq) * h;
                if (g == 2) { // clockwise: center on one side
                    i = mx - px - _x;
                    j = my - py - _y;
                } else {
                    i = mx + px - _x;
                    j = my + py - _y;
                }
            }
            float cx = _x + i;
            float cy = _y + j;
            _x = x; _y = y; _f = f;
            if (_cb.onMove) {
                generateArc(cx, cy, _x - i, _y - j, x, y, g == 2, f, _cb.onMove);
            }
            return true;
        }
        case 4: { // dwell
            float s = getCodeValue(line, 'S', 0);
            float p = getCodeValue(line, 'P', 0);
            unsigned long ms = (unsigned long)(s * 1000 + p);
            if (_cb.onDwell) _cb.onDwell(ms);
            return true;
        }

        case 17: // XY plane (default)
        case 20: // inches mode (ignored)
        case 21: // mm mode (default)
            return true;

        case 28: // home
            if (_cb.onHome) _cb.onHome();
            return true;

        case 90: // absolute positioning
            _absolute = true;
            return true;

        case 91: // relative positioning
            _absolute = false;
            return true;

        case 92: { // set position
            _x = getCodeValue(line, 'X', _x);
            _y = getCodeValue(line, 'Y', _y);
            return true;
        }
    }

    // M codes
    switch (m) {
        case 0: // program pause
        case 1: // optional stop
        case 2: // program end
            _x = _y = 0;
            return true;

        case 3: // solenoid on (spindle CW)
        case 4: // solenoid on (spindle CCW)
        case 5: // solenoid off
            if (_cb.onSolenoid) {
                float s = getCodeValue(line, 'S', m == 5 ? 0 : -1);
                _cb.onSolenoid(m != 5, s);
            }
            return true;

        case 114: // position report
            if (_cb.onReport) _cb.onReport();
            return true;

        case 117: // message (ignored)
            return true;
    }

    // unknown command
    if (_cb.onError) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Unknown: %s", line);
        _cb.onError(buf);
    }
    return false;
}
