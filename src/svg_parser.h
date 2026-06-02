#pragma once
#include <Arduino.h>
#include <functional>

class SVGParser {
public:
    using MoveTo = std::function<void(float x, float y)>;
    using LineTo = std::function<void(float x, float y)>;
    using PenUp = std::function<void()>;

    struct Callbacks {
        MoveTo onMoveTo;
        LineTo onLineTo;
        PenUp onPenUp;
    };

    struct SVGInfo {
        float width{100};
        float height{100};
        float viewW{100};
        float viewH{100};
        float viewX{0};
        float viewY{0};
        int pathCount{0};
    };

    bool parse(const char* svg, size_t len, const Callbacks& cb);
    void setZoom(float zoom) { _zoom = zoom; }
    float zoom() const { return _zoom; }
    const SVGInfo& info() { return _info; }

private:
    SVGInfo _info;
    float _zoom{1.0f};
    float _cx{0}, _cy{0};
    float _scale{1}, _ox{0}, _oy{0};
    Callbacks _cb;

    void parseViewBox(const char* attr);
    void parsePath(const char* d);
    float nextNum(const char*& p);
    char nextCmd(const char*& p);
    void doMove(float x, float y);
    void doLine(float x, float y);
    void doCubic(float x1, float y1, float x2, float y2, float x3, float y3);
    void doQuad(float x1, float y1, float x2, float y2);
};
