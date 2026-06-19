#pragma once
#include <Arduino.h>
#include <functional>

// 2D Affine transformation matrix (3x3)
struct TransformMatrix {
    float a, b, c, d, e, f;  // [a c e; b d f; 0 0 1]
    
    TransformMatrix() : a(1), b(0), c(0), d(1), e(0), f(0) {}
    TransformMatrix(float a_, float b_, float c_, float d_, float e_, float f_) 
        : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_) {}
    
    void identity() { a=1; b=0; c=0; d=1; e=0; f=0; }
    void translate(float tx, float ty);
    void scale(float sx, float sy);
    void rotate(float angle_deg, float cx = 0, float cy = 0);
    void skewX(float angle_deg);
    void skewY(float angle_deg);
    void concat(const TransformMatrix& other);
    void apply(float &x, float &y) const;
};

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

    void begin(float zoom = 1.0f);
    bool parse(const char* svg, size_t len, const Callbacks& cb);
    void parsePath(const char* pathData);
    void parseViewBox(const char* attr);
    void setZoom(float zoom) { _zoom = zoom; }
    float zoom() const { return _zoom; }
    const SVGInfo& info() { return _info; }

private:
    SVGInfo _info;
    float _zoom{1.0f};
    float _cx{0}, _cy{0};
    float _prevCpX{0}, _prevCpY{0};
    float _scale{1}, _ox{0}, _oy{0};
    Callbacks _cb;
    
    // Transform stack for <g> elements
    static const int MAX_TRANSFORM_STACK = 8;
    TransformMatrix _transformStack[MAX_TRANSFORM_STACK];
    int _transformDepth{0};

    void parseViewBox(const char* attr);
    void parsePath(const char* d);
    float nextNum(const char*& p);
    char nextCmd(const char*& p);
    void doMove(float x, float y);
    void doLine(float x, float y);
    void doCubic(float x1, float y1, float x2, float y2, float x3, float y3);
    void doQuad(float x1, float y1, float x2, float y2);
    void parseElement(const char* elemStart, size_t elemLen, const Callbacks& cb);
    void parseTransform(const char* transformAttr);
    void pushTransform();
    void popTransform();
};
