#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"

class GCodeParser {
public:
    using MoveCmd = std::function<void(float x, float y, float f)>;
    using SpeedCmd = std::function<void(float f)>;
    using HomeCmd = std::function<void()>;
    using SolenoidCmd = std::function<void(bool on, float pressure)>;
    using ReportCmd = std::function<void()>;
    using DwellCmd = std::function<void(unsigned long ms)>;
    using FileCmd = std::function<void(const char* filename)>;
    using ErrorCb = std::function<void(const char* msg)>;

    struct Callbacks {
        MoveCmd onMove;
        SpeedCmd onSetSpeed;
        HomeCmd onHome;
        SolenoidCmd onSolenoid;
        ReportCmd onReport;
        DwellCmd onDwell;
        FileCmd onFile;
        ErrorCb onError;
    };

    void begin(const Callbacks& cb);
    bool parseLine(const char* line);
    bool isAbsolute() { return _absolute; }

private:
    Callbacks _cb;
    bool _absolute{true};
    float _x{0}, _y{0}, _f{DEFAULT_FEED};
};
