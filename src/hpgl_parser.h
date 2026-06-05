#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

class HPGLParser {
public:
    using MoveCmd = std::function<void(float x, float y, float f)>;
    using SolenoidCmd = std::function<void(bool on, float pressure)>;
    using ReportCmd = std::function<void()>;
    using ErrorCb = std::function<void(const char* msg)>;

    struct Callbacks {
        MoveCmd onMove;
        SolenoidCmd onSolenoid;
        ReportCmd onReport;
        ErrorCb onError;
    };

    void begin(const Callbacks& cb);
    bool parseLine(const char* line);

    float currentX() const { return _x; }
    float currentY() const { return _y; }
    bool  isPenDown() const { return _penDown; }

private:
    Callbacks _cb;
    float _x{0}, _y{0};
    bool  _penDown{false};
    int   _pen{1};
    float _speed{1000.0f};

    // User-unit scaling (SC and IP commands)
    float _scale{HPGL_DEFAULT_SCALE};
    float _ipW{HPGL_DEFAULT_IP_W};
    float _ipH{HPGL_DEFAULT_IP_H};
    float _scX1{0}, _scY1{0}, _scX2{1}, _scY2{1};
    bool  _scSet{false};

    float hpglToMM(float hpgl) const;
    void  executeMove(float x, float y);
};
