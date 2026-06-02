#include "svg_parser.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>

static bool isCmd(char c) {
    return c == 'M' || c == 'L' || c == 'C' || c == 'Q' || c == 'Z' ||
           c == 'm' || c == 'l' || c == 'c' || c == 'q' || c == 'z' ||
           c == 'H' || c == 'h' || c == 'V' || c == 'v';
}

float SVGParser::nextNum(const char*& p) {
    while (*p && *p != '-' && *p != '+' && !(*p >= '0' && *p <= '9') && *p != '.') p++;
    if (!*p) return 0;
    char buf[32];
    int i = 0;
    if (*p == '-' || *p == '+') buf[i++] = *p++;
    while (*p && ((*p >= '0' && *p <= '9') || *p == '.') && i < 30)
        buf[i++] = *p++;
    buf[i] = '\0';
    return atof(buf);
}

char SVGParser::nextCmd(const char*& p) {
    while (*p && !isCmd(*p)) p++;
    if (!*p) return 0;
    return *p++;
}

void SVGParser::doMove(float x, float y) {
    _cx = x; _cy = y;
    if (_cb.onPenUp) _cb.onPenUp();
    float sx = _ox + x * _scale;
    float sy = _oy + y * _scale;
    if (_cb.onMoveTo) _cb.onMoveTo(sx, sy);
}

void SVGParser::doLine(float x, float y) {
    _cx = x; _cy = y;
    float sx = _ox + x * _scale;
    float sy = _oy + y * _scale;
    if (_cb.onLineTo) _cb.onLineTo(sx, sy);
}

void SVGParser::doCubic(float x1, float y1, float x2, float y2, float x3, float y3) {
    float x0 = _cx, y0 = _cy;
    int steps = SVG_CURVE_STEPS;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float u = 1 - t;
        float x = u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3;
        float y = u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3;
        doLine(x, y);
    }
}

void SVGParser::doQuad(float x1, float y1, float x2, float y2) {
    float x0 = _cx, y0 = _cy;
    int steps = SVG_CURVE_STEPS;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float u = 1 - t;
        float x = u*u*x0 + 2*u*t*x1 + t*t*x2;
        float y = u*u*y0 + 2*u*t*y1 + t*t*y2;
        doLine(x, y);
    }
}

void SVGParser::parsePath(const char* d) {
    _cx = _cy = 0;
    float sx = 0, sy = 0;
    char cmd = 0, prevCmd = 0;

    while (*d) {
        cmd = nextCmd(d);
        if (!cmd) break;

        switch (cmd) {
            case 'M': case 'm': {
                float x = nextNum(d), y = nextNum(d);
                if (cmd == 'm') { x += _cx; y += _cy; }
                doMove(x, y);
                sx = _cx; sy = _cy;
                prevCmd = 'M';
                break;
            }
            case 'L': case 'l': {
                float x = nextNum(d), y = nextNum(d);
                if (cmd == 'l') { x += _cx; y += _cy; }
                doLine(x, y);
                prevCmd = 'L';
                break;
            }
            case 'H': case 'h': {
                float x = nextNum(d);
                if (cmd == 'h') x += _cx;
                doLine(x, _cy);
                prevCmd = 'H';
                break;
            }
            case 'V': case 'v': {
                float y = nextNum(d);
                if (cmd == 'v') y += _cy;
                doLine(_cx, y);
                prevCmd = 'V';
                break;
            }
            case 'C': case 'c': {
                float x1 = nextNum(d), y1 = nextNum(d);
                float x2 = nextNum(d), y2 = nextNum(d);
                float x3 = nextNum(d), y3 = nextNum(d);
                if (cmd == 'c') { x1 += _cx; y1 += _cy; x2 += _cx; y2 += _cy; x3 += _cx; y3 += _cy; }
                doCubic(x1, y1, x2, y2, x3, y3);
                prevCmd = 'C';
                break;
            }
            case 'S': case 's': {
                float x2 = nextNum(d), y2 = nextNum(d);
                float x3 = nextNum(d), y3 = nextNum(d);
                float x1 = _cx, y1 = _cy;
                if (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's') {
                    x1 = 2 * _cx - _cx;
                    y1 = 2 * _cy - _cy;
                }
                if (cmd == 's') { x2 += _cx; y2 += _cy; x3 += _cx; y3 += _cy; }
                doCubic(x1, y1, x2, y2, x3, y3);
                prevCmd = 'S';
                break;
            }
            case 'Q': case 'q': {
                float x1 = nextNum(d), y1 = nextNum(d);
                float x2 = nextNum(d), y2 = nextNum(d);
                if (cmd == 'q') { x1 += _cx; y1 += _cy; x2 += _cx; y2 += _cy; }
                doQuad(x1, y1, x2, y2);
                prevCmd = 'Q';
                break;
            }
            case 'T': case 't': {
                float x2 = nextNum(d), y2 = nextNum(d);
                float x1 = _cx, y1 = _cy;
                if (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't') {
                    x1 = 2 * _cx - _cx;
                    y1 = 2 * _cy - _cy;
                }
                if (cmd == 't') { x2 += _cx; y2 += _cy; }
                doQuad(x1, y1, x2, y2);
                prevCmd = 'T';
                break;
            }
            case 'Z': case 'z':
                doLine(sx, sy);
                prevCmd = 'Z';
                break;
        }
    }
}

void SVGParser::parseViewBox(const char* attr) {
    if (!attr || !*attr) return;
    _info.viewX = nextNum(attr);
    _info.viewY = nextNum(attr);
    _info.viewW = nextNum(attr);
    _info.viewH = nextNum(attr);
}

static const char* findAttr(const char* svg, size_t len, const char* name) {
    const char* p = svg;
    const char* end = svg + len;
    size_t nlen = strlen(name);
    while (p < end) {
        const char* found = strstr(p, name);
        if (!found || found >= end) return nullptr;
        const char* val = found + nlen;
        while (val < end && (*val == ' ' || *val == '=' || *val == '"' || *val == '\'')) val++;
        if (val >= end) return nullptr;
        // find end of value
        return val;
    }
    return nullptr;
}

static void readValue(const char* start, const char* end, char* buf, int maxLen) {
    int i = 0;
    while (start < end && *start != '"' && *start != '\'' && *start != ' ' && *start != '>' && i < maxLen - 1)
        buf[i++] = *start++;
    buf[i] = '\0';
}

bool SVGParser::parse(const char* svg, size_t len, const Callbacks& cb) {
    _cb = cb;
    _info = SVGInfo();
    _info.pathCount = 0;

    // Find viewBox
    const char* vbStart = findAttr(svg, len, "viewBox");
    if (vbStart) {
        char vb[128];
        readValue(vbStart, svg + len, vb, sizeof(vb));
        parseViewBox(vb);
    }

    // Width/height fallback
    const char* wStart = findAttr(svg, len, "width");
    if (wStart) {
        char buf[32];
        readValue(wStart, svg + len, buf, sizeof(buf));
        _info.width = atof(buf);
    }
    const char* hStart = findAttr(svg, len, "height");
    if (hStart) {
        char buf[32];
        readValue(hStart, svg + len, buf, sizeof(buf));
        _info.height = atof(buf);
    }

    if (_info.viewW == 0) _info.viewW = _info.width;
    if (_info.viewH == 0) _info.viewH = _info.height;
    if (_info.viewW == 0) _info.viewW = 100;
    if (_info.viewH == 0) _info.viewH = 100;

    // Compute scale to fit plotter area, preserve aspect, then apply zoom
    float scaleX = X_MAX_MM / _info.viewW;
    float scaleY = Y_MAX_MM / _info.viewH;
    _scale = fmin(scaleX, scaleY) * _zoom;
    _ox = (X_MAX_MM - _info.viewW * _scale) / 2.0f;
    _oy = (Y_MAX_MM - _info.viewH * _scale) / 2.0f;

    // Parse all <path> elements
    const char* p = svg;
    while (p < svg + len) {
        const char* pathStart = strstr(p, "<path");
        if (!pathStart || pathStart >= svg + len) break;
        p = pathStart + 5;

        const char* dStart = findAttr(pathStart, svg + len - pathStart, "d");
        if (dStart && dStart < svg + len) {
            char pathData[SVG_PATH_MAX];
            readValue(dStart, svg + len, pathData, sizeof(pathData));
            parsePath(pathData);
            _info.pathCount++;
        }
    }

    return _info.pathCount > 0;
}
