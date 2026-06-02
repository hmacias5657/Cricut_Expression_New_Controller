#include <Arduino.h>
#include <cmath>
#include "config.h"
#include "stepper.h"

// ── Fast GPIO helpers (IRAM for speed) ──────────────────────

static IRAM_ATTR void gpioSet(int pin, bool level) {
    if (level) GPIO.out_w1ts = (1 << pin);
    else       GPIO.out_w1tc = (1 << pin);
}

static IRAM_ATTR int gpioReadRaw(int pin) {
    return (GPIO.in >> pin) & 1;
}

// ── Init ─────────────────────────────────────────────────────

void StepperControl::init() {
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, HIGH); // disabled (active low)

    pinMode(X_STEP_PIN, OUTPUT);
    pinMode(X_DIR_PIN, OUTPUT);
    pinMode(Y_STEP_PIN, OUTPUT);
    pinMode(Y_DIR_PIN, OUTPUT);

    digitalWrite(X_STEP_PIN, LOW);
    digitalWrite(Y_STEP_PIN, LOW);
    digitalWrite(X_DIR_PIN, LOW);
    digitalWrite(Y_DIR_PIN, LOW);

    enable(false);
}

void StepperControl::enable(bool en) {
    digitalWrite(ENABLE_PIN, en ? LOW : HIGH);
}

// ── Step pulse helpers ───────────────────────────────────────

void StepperControl::pulseStepX() {
    gpioSet(X_STEP_PIN, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    gpioSet(X_STEP_PIN, LOW);
}

void StepperControl::pulseStepY() {
    gpioSet(Y_STEP_PIN, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    gpioSet(Y_STEP_PIN, LOW);
}

// ── S-curve profile planner ──────────────────────────────────
//
// Produces a 7-phase S-curve (jerk-limited trapezoid):
//   Phase 1: jerk +J,  accel 0→A      (ramp up)
//   Phase 2: jerk  0,  accel = A       (constant accel)
//   Phase 3: jerk −J,  accel A→0       (ramp down)
//   Phase 4: jerk  0,  accel = 0       (cruise)
//   Phase 5: jerk −J,  accel 0→−A      (ramp down decel)
//   Phase 6: jerk  0,  accel = −A      (constant decel)
//   Phase 7: jerk +J,  accel −A→0      (ramp up decel)
//
// For short moves, phases 2/6 and/or 4 are collapsed.

void StepperControl::planProfile() {
    float D = _totalDist;   // mm  (always ≥ 0)
    float V = _feedrate;    // mm/s
    float A = ACCELERATION; // mm/s²
    float J = JERK;         // mm/s³

    _j = J;
    _aMax = A;

    if (D < 0.001f || V < 0.1f) {
        _moveActive = false;
        return;
    }

    // Clamp feedrate to hardware maximum
    float Vmax = MAX_FEEDRATE / 60.0f;
    if (V > Vmax) V = Vmax;
    // Apply runtime speed-pot override (set by setMaxFeedrateOverride)
    if (_maxFeedOverride > 0) {
        float overrideMs = _maxFeedOverride / 60.0f;
        if (V > overrideMs) V = overrideMs;
    }

    // Jerk time — how long to ramp accel from 0 to A (or A to 0)
    float Ta = (J > 0.001f) ? (A / J) : 0.0f;
    if (Ta < 0.0001f) {
        // Jerk so high it's essentially trapezoidal
        // Fall back to trapezoidal: phases 2 and 6 only, no 1/3/5/7
        _vMax = V;
        _aMax = A;
        _t1 = 0; _t2 = V / A; _t3 = 0; _t4 = 0; _t5 = 0; _t6 = V / A; _t7 = 0;
        _v1 = 0; _v2 = V/2; _v3 = V; _v4 = V; _v5 = V; _v6 = V/2; _v7 = 0;
        _s1 = 0; _s2 = 0.5f * A * _t2 * _t2;
        _s3 = _s2; _s4 = _s3; _s5 = _s4;
        _s6 = _s5 + _v5 * _t6 - 0.5f * A * _t6 * _t6;
        _s7 = D;
        float dAccelDecel = _s2 + (D - _s6);
        if (D > dAccelDecel) {
            _t4 = (D - dAccelDecel) / V;
            _s4 = _s3 + V * _t4;
        } else {
            // Triangular trapezoidal: reduce V
            float Vt = sqrtf(A * D);
            _t2 = Vt / A; _t6 = Vt / A;
            _v2 = Vt/2; _v3 = Vt; _v5 = Vt; _v6 = Vt/2;
            _s2 = 0.5f * A * _t2 * _t2;
            _s3 = _s2; _s4 = _s3; _s5 = _s4;
            _s6 = _s5 + _v5 * _t6 - 0.5f * A * _t6 * _t6;
            _s7 = D;
            _vMax = Vt;
        }
        _totalTime = _t1 + _t2 + _t3 + _t4 + _t5 + _t6 + _t7;
        _moveActive = true;
        return;
    }

    // Velocity gained during one ramp phase: vRamp = 0.5 * J * Ta² = 0.5 * A * Ta
    float vRamp = 0.5f * A * Ta;
    // Distance during one ramp phase: sRamp = (1/6) * J * Ta³ = (1/6) * A * Ta²
    float sRamp = (1.0f/6.0f) * J * Ta * Ta * Ta;

    // ── Phase 1: jerk +J ──
    float s1 = sRamp;
    float v1 = vRamp;

    // ── Phase 2: constant accel A, duration t2 ──
    // We need v1 + A*t2 + vRamp = V  →  t2 = (V - v1 - vRamp) / A = V/A - Ta
    float t2_ideal = (V / A) - Ta;
    float s2 = 0, v2 = v1;
    if (t2_ideal > 0) {
        s2 = v1 * t2_ideal + 0.5f * A * t2_ideal * t2_ideal;
        v2 = v1 + A * t2_ideal;
    } else {
        t2_ideal = 0;
    }

    // ── Phase 3: jerk −J ──
    // v3 = v2 + vRamp = V
    float v3 = v2 + vRamp;
    // s3 = s2 + v2*Ta + 0.5*A*Ta² − sRamp
    float s3 = (s1 + s2) + (v2 * Ta + 0.5f * A * Ta * Ta - sRamp);
    float sAccel = s1 + s2 + (s3 - s1 - s2);  // = s3 - s1 - s2 + s1 + s2 = s3... no
    sAccel = s3;  // s3 is the cumulative distance at end of phase 3

    // ── Check: do we have room to accelerate to V and decelerate? ──
    if (D >= 2.0f * sAccel) {
        // Yes → full 7-phase profile with cruise
        _vMax = V;
        _t1 = Ta; _t2 = t2_ideal; _t3 = Ta;
        _t4 = (D - 2.0f * sAccel) / V;
        _t5 = Ta; _t6 = t2_ideal; _t7 = Ta;
        _s1 = s1;
        _s2 = s1 + s2;
        _s3 = s1 + s2 + (s3 - s1 - s2);  // = s3
        _v1 = v1; _v2 = v2; _v3 = v3;
        _s4 = _s3 + V * _t4;
        _v4 = V;
        _s5 = _s4 + (V * Ta - sRamp);
        _v5 = V - vRamp;
        _s6 = _s5 + (_v5 * t2_ideal - 0.5f * A * t2_ideal * t2_ideal);
        _v6 = (_t6 > 0) ? (_v5 - A * t2_ideal) : _v5;
        _s7 = D;
        _v7 = 0;
        _totalTime = _t1 + _t2 + _t3 + _t4 + _t5 + _t6 + _t7;
        _moveActive = true;
        return;
    }

    // ── Not enough room for cruise → find achievable V ──
    // Binary search on V such that accel+decel distance = D
    float vLo = 0.1f, vHi = V;
    for (int iter = 0; iter < 20; iter++) {
        float vTest = (vLo + vHi) * 0.5f;
        float t2t = (vTest / A) - Ta;
        if (t2t < 0) t2t = 0;
        float s1t = sRamp;
        float v1t = vRamp;
        float s2t = (t2t > 0) ? (v1t * t2t + 0.5f * A * t2t * t2t) : 0;
        float v2t = v1t + A * t2t;
        float s3t = s1t + s2t + (v2t * Ta + 0.5f * A * Ta * Ta - sRamp);
        if (2.0f * s3t <= D) vLo = vTest;
        else vHi = vTest;
    }
    float Vactual = vLo;

    // Recompute with the found V
    float t2a = (Vactual / A) - Ta;
    if (t2a < 0) t2a = 0;
    float s1a = sRamp;
    float v1a = vRamp;
    float s2a = (t2a > 0) ? (v1a * t2a + 0.5f * A * t2a * t2a) : 0;
    float v2a = v1a + A * t2a;
    float s3a = s1a + s2a + (v2a * Ta + 0.5f * A * Ta * Ta - sRamp);
    float v3a = v2a + vRamp;

    if (2.0f * s3a <= D + 0.001f) {
        // Triangular with phases 2 and 6 present
        _vMax = Vactual;
        _aMax = A;
        _t1 = Ta; _t2 = t2a; _t3 = Ta; _t4 = 0;
        _t5 = Ta; _t6 = t2a; _t7 = Ta;
        _s1 = s1a;
        _s2 = s1a + s2a;
        _s3 = s3a;
        _v1 = v1a; _v2 = v2a; _v3 = v3a;
        _s4 = _s3; _v4 = v3a;
        _s5 = _s4 + (v3a * Ta - sRamp);
        _v5 = v3a - vRamp;
        _s6 = _s5 + (_v5 * t2a - 0.5f * A * t2a * t2a);
        _v6 = (t2a > 0) ? (_v5 - A * t2a) : _v5;
        _s7 = D;
        _v7 = 0;
        _totalTime = _t1 + _t2 + _t3 + _t4 + _t5 + _t6 + _t7;
        _moveActive = true;
        return;
    }

    // ── Very short move: pure triangular (phases 1,3,5,7 only) ──
    // Total distance = 2 * J * T³  →  T = cbrt(D / (2*J))
    float T = powf(D / (2.0f * J), 1.0f / 3.0f);
    if (T > Ta) T = Ta;  // cap at full A
    float J_T = J * T;
    float vPeak = J_T * T;  // J*T²
    _vMax = vPeak;
    _aMax = J_T;
    _t1 = T; _t2 = 0; _t3 = T; _t4 = 0;
    _t5 = T; _t6 = 0; _t7 = T;
    _v1 = 0.5f * J_T * T;
    _s1 = (1.0f/6.0f) * J * T * T * T;
    _v2 = _v1; _s2 = _s1;
    _v3 = vPeak;
    _s3 = J * T * T * T;  // = D/2
    _s4 = _s3; _v4 = vPeak;
    float v5s = vPeak;
    _s5 = _s4 + v5s * T - (1.0f/6.0f) * J * T * T * T;
    _v5 = v5s - 0.5f * J_T * T;
    _s6 = _s5; _v6 = _v5;
    _s7 = D;
    _v7 = 0;
    _totalTime = _t1 + _t2 + _t3 + _t4 + _t5 + _t6 + _t7;
    _moveActive = true;
}

// ── S-curve position evaluator (distance travelled at time t) ──

float StepperControl::evalProfile(float t) {
    if (t <= 0) return 0;
    if (t >= _totalTime) return _totalDist;

    float remaining = t;

    // Phase 1: a(t) = J*t
    if (remaining <= _t1) {
        return (1.0f/6.0f) * _j * remaining * remaining * remaining;
    }
    remaining -= _t1;

    // Phase 2: a(t) = A
    if (remaining <= _t2) {
        return _s1 + _v1 * remaining + 0.5f * _aMax * remaining * remaining;
    }
    remaining -= _t2;

    // Phase 3: a(t) = A - J*t
    if (remaining <= _t3) {
        float r = remaining;
        return _s2 + _v2 * r + 0.5f * _aMax * r * r - (1.0f/6.0f) * _j * r * r * r;
    }
    remaining -= _t3;

    // Phase 4: a(t) = 0 (cruise)
    if (remaining <= _t4) {
        return _s3 + _v3 * remaining;
    }
    remaining -= _t4;

    // Phase 5: a(t) = -J*t
    if (remaining <= _t5) {
        float r = remaining;
        return _s4 + _v4 * r - (1.0f/6.0f) * _j * r * r * r;
    }
    remaining -= _t5;

    // Phase 6: a(t) = -A
    if (remaining <= _t6) {
        return _s5 + _v5 * remaining - 0.5f * _aMax * remaining * remaining;
    }
    remaining -= _t6;

    // Phase 7: a(t) = -A + J*t
    {
        float r = remaining;
        return _s6 + _v6 * r - 0.5f * _aMax * r * r + (1.0f/6.0f) * _j * r * r * r;
    }
}

// ── Public API ──────────────────────────────────────────────

void StepperControl::setTarget(float x, float y, float feedrate) {
    float f = constrain(feedrate, 10.0f, MAX_FEEDRATE) / 60.0f; // mm/s
    float cx = currentX();
    float cy = currentY();
    float dx = x - cx;
    float dy = y - cy;
    float dist = sqrtf(dx * dx + dy * dy);

    _startStepX = _stepX;
    _startStepY = _stepY;
    _targetStepX = round(x * STEP_PER_MM);
    _targetStepY = round(y * STEP_PER_MM);
    _dirX = (_targetStepX >= _stepX);
    _dirY = (_targetStepY >= _stepY);
    digitalWrite(X_DIR_PIN, _dirX ? HIGH : LOW);
    digitalWrite(Y_DIR_PIN, _dirY ? HIGH : LOW);

    _totalDist = dist;
    _feedrate = f;
    _moveStartUs = esp_timer_get_time();
    _lastStepUs = _moveStartUs;

    enable(true);
    planProfile();
}

void StepperControl::moveRelative(float dx, float dy, float feedrate) {
    float nx = currentX() + dx;
    float ny = currentY() + dy;
    nx = constrain(nx, 0, X_MAX_MM);
    ny = constrain(ny, 0, Y_MAX_MM);
    setTarget(nx, ny, feedrate);
}

void StepperControl::stop() {
    _moveActive = false;
    enable(false);
}

float StepperControl::targetX() {
    return (float)_targetStepX / STEP_PER_MM;
}

float StepperControl::targetY() {
    return (float)_targetStepY / STEP_PER_MM;
}

void StepperControl::setCurrentPosition(float x, float y) {
    _stepX = round(x * STEP_PER_MM);
    _stepY = round(y * STEP_PER_MM);
}

// ── Runtime: evaluate S-curve and generate rate-limited steps ──

void StepperControl::run() {
    if (!_moveActive) return;

    uint64_t now = esp_timer_get_time();
    float elapsed = (float)(now - _moveStartUs) / 1e6f;

    if (elapsed >= _totalTime) {
        _stepX = _targetStepX;
        _stepY = _targetStepY;
        _moveActive = false;
        enable(false);
        return;
    }

    // Evaluate S-curve position at current time
    float dist = evalProfile(elapsed);
    float frac = (dist / _totalDist);
    if (frac > 1.0f) frac = 1.0f;
    if (frac < 0.0f) frac = 0.0f;

    int32_t tx = _startStepX + (int32_t)roundf((_targetStepX - _startStepX) * frac);
    int32_t ty = _startStepY + (int32_t)roundf((_targetStepY - _startStepY) * frac);

    // Estimate instantaneous velocity from S-curve (mm/s) for step timing
    float dtEval = 0.002f; // 2ms derivative window
    if (elapsed + dtEval > _totalTime) dtEval = _totalTime - elapsed;
    if (dtEval < 0.0005f) dtEval = 0.0005f;
    float dNext = evalProfile(elapsed + dtEval);
    float vel = (dNext - dist) / dtEval;
    if (vel < 0.1f) vel = 0.1f;

    // Step period at current velocity
    float stepPeriodUs = 1e6f / (vel * STEP_PER_MM);
    if (stepPeriodUs < 10.0f) stepPeriodUs = 10.0f;
    uint32_t period = (uint32_t)stepPeriodUs;

    // Time budget for this run() call (one tick period)
    uint32_t maxRunUs = (uint32_t)(1e6f / MOTION_TICK_HZ);
    uint32_t startRunUs = (uint32_t)(now / 1);  // micros approximation
    uint32_t elapsedRun = 0;

    // Generate steps with proper spacing
    while ((_stepX != tx || _stepY != ty) && elapsedRun < maxRunUs) {
        // Generate one step per axis if needed
        if (_stepX < tx) { _stepX++; pulseStepX(); }
        else if (_stepX > tx) { _stepX--; pulseStepX(); }

        if (_stepY < ty) { _stepY++; pulseStepY(); }
        else if (_stepY > ty) { _stepY--; pulseStepY(); }

        // Wait for the correct step interval
        if (period > 2 * STEP_PULSE_US + 1) {
            uint32_t wait = period - 2 * STEP_PULSE_US;
            if (wait > 50) wait = 50; // cap per-step wait to prevent lag
            delayMicroseconds(wait);
            elapsedRun += period;
        } else {
            elapsedRun += period;
        }

        // Re-check position in case we crossed a phase boundary
        now = esp_timer_get_time();
        if (elapsedRun >= maxRunUs) break;

        // Refresh target (S-curve may have advanced)
        elapsed = (float)(now - _moveStartUs) / 1e6f;
        if (elapsed >= _totalTime) {
            _stepX = _targetStepX;
            _stepY = _targetStepY;
            _moveActive = false;
            enable(false);
            return;
        }
        dist = evalProfile(elapsed);
        frac = dist / _totalDist;
        if (frac > 1.0f) frac = 1.0f;
        tx = _startStepX + (int32_t)roundf((_targetStepX - _startStepX) * frac);
        ty = _startStepY + (int32_t)roundf((_targetStepY - _startStepY) * frac);
    }
}

// ── Homing ──────────────────────────────────────────────────
//
// Endstop is on the right side at X_MAX_MM. Home = rightmost,
// then back off to release the switch.

void StepperControl::homeX() {
    _homed = false;
    _moveActive = false;
    enable(true);

    // Move right (positive X) until endstop triggers
    digitalWrite(X_DIR_PIN, HIGH);
    float stepDelayUs = 60.0f / (HOMING_FEED * STEP_PER_MM) * 1e6f;
    if (stepDelayUs < 50) stepDelayUs = 50;

    while (true) {
        if (gpioReadRaw(ENDSTOP_PIN) == LOW) break;
        pulseStepX();
        _stepX++;
        delayMicroseconds((uint32_t)stepDelayUs);
    }

    // Home = X_MAX_MM
    _stepX = round(X_MAX_MM * STEP_PER_MM);

    // Back off left
    digitalWrite(X_DIR_PIN, LOW);
    int32_t backoff = round(HOMING_BACKOFF * STEP_PER_MM);
    for (int32_t i = 0; i < backoff; i++) {
        pulseStepX();
        _stepX--;
        delayMicroseconds((uint32_t)stepDelayUs);
    }

    _homed = true;
    enable(false);
}
