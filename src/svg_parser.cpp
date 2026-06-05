#include "svg_parser.h"
#include "config.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool isCmd(char c) {
    return c == 'M' || c == 'L' || c == 'C' || c == 'Q' || c == 'Z' ||
           c == 'm' || c == 'l' || c == 'c' || c == 'q' || c == 'z' ||
           c == 'H' || c == 'h' || c == 'V' || c == 'v';
}

// Parse a comma- or space-separated number list
static int parseFloats(const char* p, float* out, int max) {
    int count = 0;
    while (*p && count < max) {
        while (*p && *p != '-' && *p != '+' && !(*p >= '0' && *p <= '9') && *p != '.') p++;
        if (!*p) break;
        char buf[32]; int i = 0;
        if (*p == '-' || *p == '+') buf[i++] = *p++;
        while (*p && ((*p >= '0' && *p <= '9') || *p == '.') && i < 30)
            buf[i++] = *p++;
        buf[i] = '\0';
        out[count++] = atof(buf);
    }
    return count;
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
                _prevCpX = x2; _prevCpY = y2;
                prevCmd = 'C';
                break;
            }
            case 'S': case 's': {
                float x2 = nextNum(d), y2 = nextNum(d);
                float x3 = nextNum(d), y3 = nextNum(d);
                float x1 = _cx, y1 = _cy;
                if (prevCmd == 'C' || prevCmd == 'c' || prevCmd == 'S' || prevCmd == 's') {
                    x1 = 2 * _cx - _prevCpX;
                    y1 = 2 * _cy - _prevCpY;
                }
                if (cmd == 's') { x2 += _cx; y2 += _cy; x3 += _cx; y3 += _cy; }
                doCubic(x1, y1, x2, y2, x3, y3);
                _prevCpX = x2; _prevCpY = y2;
                prevCmd = 'S';
                break;
            }
            case 'Q': case 'q': {
                float x1 = nextNum(d), y1 = nextNum(d);
                float x2 = nextNum(d), y2 = nextNum(d);
                if (cmd == 'q') { x1 += _cx; y1 += _cy; x2 += _cx; y2 += _cy; }
                doQuad(x1, y1, x2, y2);
                _prevCpX = x1; _prevCpY = y1;
                prevCmd = 'Q';
                break;
            }
            case 'T': case 't': {
                float x2 = nextNum(d), y2 = nextNum(d);
                float x1 = _cx, y1 = _cy;
                if (prevCmd == 'Q' || prevCmd == 'q' || prevCmd == 'T' || prevCmd == 't') {
                    x1 = 2 * _cx - _prevCpX;
                    y1 = 2 * _cy - _prevCpY;
                }
                if (cmd == 't') { x2 += _cx; y2 += _cy; }
                doQuad(x1, y1, x2, y2);
                _prevCpX = x1; _prevCpY = y1;
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

    // Parse all SVG elements
    const char* p = svg;
    while (p < svg + len) {
        // <path d="...">
        const char* elemStart = strstr(p, "<path");
        bool isPath = elemStart != nullptr && elemStart < svg + len;
        
        // Check for primitive elements if no path found yet or continue scanning
        if (!isPath || elemStart > p) {
            const char* const primitives[] = {"<rect", "<circle", "<ellipse", "<line", "<polyline", "<polygon", nullptr};
            const char* best = nullptr;
            for (int i = 0; primitives[i]; i++) {
                const char* found = strstr(p, primitives[i]);
                if (found && found < svg + len && (!best || found < best))
                    best = found;
            }
            if (best && (!isPath || best < elemStart)) {
                elemStart = best;
                isPath = false;
            }
        }
        
        if (!elemStart || elemStart >= svg + len) break;

        if (isPath) {
            p = elemStart + 5;
            const char* dStart = findAttr(elemStart, svg + len - elemStart, "d");
            if (dStart && dStart < svg + len) {
                char pathData[SVG_PATH_MAX];
                readValue(dStart, svg + len, pathData, sizeof(pathData));
                parsePath(pathData);
                _info.pathCount++;
            }
        } else {
            // Determine element type and parse accordingly
            const char* tagEnd = strchr(elemStart, '>');
            if (!tagEnd || tagEnd >= svg + len) { p = elemStart + 1; continue; }
            char elemBuf[256];
            size_t eLen = min((size_t)(tagEnd - elemStart + 1), sizeof(elemBuf) - 1);
            strncpy(elemBuf, elemStart, eLen);
            elemBuf[eLen] = '\0';
            
            if (elemBuf[1] == 'r' && elemBuf[2] == 'e' && elemBuf[3] == 'c' && elemBuf[4] == 't') {
                // <rect x="..." y="..." width="..." height="..." rx="..." ry="...">
                float x=0, y=0, w=0, h=0, rx=0, ry=0;
                for (const char* a = elemBuf; *a; a++) {
                    if (strncmp(a, "x=\"", 3) == 0) x = strtof(a+3, (char**)&a);
                    else if (strncmp(a, "y=\"", 3) == 0) y = strtof(a+3, (char**)&a);
                    else if (strncmp(a, "width=\"", 7) == 0) w = strtof(a+7, (char**)&a);
                    else if (strncmp(a, "height=\"", 8) == 0) h = strtof(a+8, (char**)&a);
                }
                if (w > 0 && h > 0) {
                    doMove(x, y);
                    doLine(x + w, y);
                    doLine(x + w, y + h);
                    doLine(x, y + h);
                    doLine(x, y);
                    _info.pathCount++;
                }
            } else if (elemBuf[1] == 'c' && elemBuf[2] == 'i' && elemBuf[3] == 'r' && elemBuf[4] == 'c' && elemBuf[5] == 'l' && elemBuf[6] == 'e') {
                // <circle cx="..." cy="..." r="...">
                float cx=0, cy=0, r=0;
                for (const char* a = elemBuf; *a; a++) {
                    if (strncmp(a, "cx=\"", 4) == 0) cx = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "cy=\"", 4) == 0) cy = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "r=\"", 3) == 0) r = strtof(a+3, (char**)&a);
                }
                if (r > 0) {
                    // Approximate circle with 4 cubic bezier arcs
                    float k = 0.5522847498f * r;
                    float x0 = cx - r, y0 = cy;
                    doMove(cx + r, cy);
                    doCubic(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
                    doCubic(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
                    doCubic(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
                    doCubic(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
                    _info.pathCount++;
                }
            } else if (elemBuf[1] == 'e' && elemBuf[2] == 'l' && elemBuf[3] == 'l' && elemBuf[4] == 'i' && elemBuf[5] == 'p' && elemBuf[6] == 's' && elemBuf[7] == 'e') {
                // <ellipse cx="..." cy="..." rx="..." ry="...">
                float cx=0, cy=0, rx=0, ry=0;
                for (const char* a = elemBuf; *a; a++) {
                    if (strncmp(a, "cx=\"", 4) == 0) cx = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "cy=\"", 4) == 0) cy = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "rx=\"", 4) == 0) rx = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "ry=\"", 4) == 0) ry = strtof(a+4, (char**)&a);
                }
                if (rx > 0 && ry > 0) {
                    float kx = 0.5522847498f * rx;
                    float ky = 0.5522847498f * ry;
                    doMove(cx + rx, cy);
                    doCubic(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
                    doCubic(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
                    doCubic(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
                    doCubic(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
                    _info.pathCount++;
                }
            } else if (elemBuf[1] == 'l' && elemBuf[2] == 'i' && elemBuf[3] == 'n' && elemBuf[4] == 'e') {
                // <line x1="..." y1="..." x2="..." y2="...">
                float x1=0, y1=0, x2=0, y2=0;
                for (const char* a = elemBuf; *a; a++) {
                    if (strncmp(a, "x1=\"", 4) == 0) x1 = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "y1=\"", 4) == 0) y1 = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "x2=\"", 4) == 0) x2 = strtof(a+4, (char**)&a);
                    else if (strncmp(a, "y2=\"", 4) == 0) y2 = strtof(a+4, (char**)&a);
                }
                doMove(x1, y1);
                doLine(x2, y2);
                _info.pathCount++;
            } else if (strncmp(elemBuf+1, "polyline", 8) == 0 || strncmp(elemBuf+1, "polygon", 7) == 0) {
                // <polyline points="x1,y1 x2,y2 ..."/>  or <polygon>
                bool isPolygon = (elemBuf[1] == 'p' && elemBuf[2] == 'o' && elemBuf[3] == 'l' && elemBuf[4] == 'y' && elemBuf[5] == 'g');
                const char* pts = strstr(elemBuf, "points=\"");
                if (pts) {
                    pts += 8;
                    float nums[256];
                    int n = parseFloats(pts, nums, 256);
                    if (n >= 4) {
                        doMove(nums[0], nums[1]);
                        for (int i = 2; i + 1 < n; i += 2)
                            doLine(nums[i], nums[i+1]);
                        if (isPolygon) doLine(nums[0], nums[1]);
                        _info.pathCount++;
                    }
                }
            }

            p = tagEnd + 1;
        }
    }

    return _info.pathCount > 0;
}
