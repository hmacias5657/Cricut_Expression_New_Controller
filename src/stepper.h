#pragma once
#include <cstdint>
#include "config.h"

class StepperControl {
public:
    void init();
    void enable(bool en);
    void setTarget(float x, float y, float feedrate);
    void moveRelative(float dx, float dy, float feedrate);
    void homeX();
    bool isHomed() { return _homed; }
    bool isRunning() { return _moveActive; }
    void run();
    void stop();
    float currentX() { return (float)_stepX / STEP_PER_MM; }
    float currentY() { return (float)_stepY / STEP_PER_MM; }
    float targetX();
    float targetY();
    void setCurrentPosition(float x, float y);
    void setMaxFeedrateOverride(float mm_min) { _maxFeedOverride = mm_min; }

private:
    bool _homed{false};

    // Step counts (actual position)
    int32_t _stepX{0};
    int32_t _stepY{0};

    // Move plan
    int32_t _targetStepX{0};
    int32_t _targetStepY{0};
    int32_t _startStepX{0};
    int32_t _startStepY{0};
    bool _moveActive{false};
    bool _dirX{true};  // true = positive direction
    bool _dirY{true};

    // Runtime max speed cap from potentiometer (mm/min, 0 = disabled)
    float _maxFeedOverride{0};

    // S-curve profile (normalised distance 0.._totalDist)
    float _totalDist{0};        // mm (always positive)
    float _feedrate{0};         // mm/s
    uint64_t _moveStartUs{0};   // start time in microseconds
    uint64_t _lastStepUs{0};    // time of last step pulse

    // Profile parameters (times in seconds, distances in mm)
    // 7-phase S-curve:
    //   1: jerk +J, accel 0→A
    //   2: jerk 0,  accel = A
    //   3: jerk -J, accel A→0
    //   4: jerk 0,  cruise
    //   5: jerk -J, accel 0→-A
    //   6: jerk 0,  accel = -A
    //   7: jerk +J, accel -A→0
    float _j, _aMax, _vMax;
    float _t1, _t2, _t3, _t4, _t5, _t6, _t7;  // phase durations
    float _s1, _s2, _s3, _s4, _s5, _s6, _s7;  // cumulative distance at end of each phase
    float _v1, _v2, _v3, _v4, _v5, _v6, _v7;  // velocity at end of each phase
    float _totalTime;

    void planProfile();
    float evalProfile(float t);
    float evalVelocity(float t);
    void pulseStepX();
    void pulseStepY();
};
