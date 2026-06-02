#include "gcode_parser.h"
#include <stdlib.h>

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
