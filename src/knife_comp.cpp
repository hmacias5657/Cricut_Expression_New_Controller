#include "knife_comp.h"
#include "config.h"
#include "stepper.h"
#include <math.h>
#include <Arduino.h>

extern StepperControl stepper;
extern void onSolenoid(bool on, float pressure);

static float _lastX = 0, _lastY = 0;
static float _bladeAngle = 0;
static bool  _bladeDown = false;
static bool  _initialized = false;

// Pending move state for async pivot sequence
static bool  _pendingValid = false;
static float _pendingX = 0, _pendingY = 0, _pendingFeed = 0;
static bool  _pendingPenDown = false;

void knifeCompReset() {
    _lastX = stepper.currentX();
    _lastY = stepper.currentY();
    _bladeAngle   = 0;
    _bladeDown    = false;
    _initialized  = false;
    _pendingValid = false;
}

bool knifeHasPendingMove() {
    return _pendingValid;
}

void knifeExecutePending() {
    if (!_pendingValid) return;
    _pendingValid = false;

    // Lower blade for the actual cut
    onSolenoid(true, -1);
    _bladeDown = true;

    // Issue the original cut move
    stepper.setTarget(_pendingX, _pendingY, _pendingFeed);
}

void knifeMove(float x, float y, float feed, bool penDown) {
    float dx = x - _lastX;
    float dy = y - _lastY;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 0.01f) {
        if (penDown != _bladeDown) {
            onSolenoid(penDown, -1);
            _bladeDown = penDown;
        }
        return;
    }

    float newAngle = atan2f(dy, dx);

#if KNIFE_COMPENSATION_ENABLE
    if (_initialized && _bladeDown) {
        float delta = newAngle - _bladeAngle;
        while (delta >  M_PI) delta -= 2.0f * M_PI;
        while (delta < -M_PI) delta += 2.0f * M_PI;

        float threshRad = KNIFE_ANGLE_THRESHOLD_DEG * M_PI / 180.0f;
        if (fabsf(delta) > threshRad) {
            // Lift blade
            onSolenoid(false, -1);
            _bladeDown = false;

            // Pivot: move forward in the new direction
            float pivotX = _lastX + cosf(newAngle) * KNIFE_OFFSET_MM;
            float pivotY = _lastY + sinf(newAngle) * KNIFE_OFFSET_MM;
            if (pivotX < 0) pivotX = 0;
            if (pivotX > X_MAX_MM) pivotX = X_MAX_MM;
            if (pivotY < 0) pivotY = 0;
            if (pivotY > Y_MAX_MM) pivotY = Y_MAX_MM;
            stepper.setTarget(pivotX, pivotY, feed * 0.5f);

            // Stash the real move for later (after pivot completes)
            _pendingX = x;
            _pendingY = y;
            _pendingFeed = feed;
            _pendingPenDown = penDown;
            _pendingValid = true;

            _lastX = x;
            _lastY = y;
            _bladeAngle = newAngle;
            return;
        }
    }
#endif

    if (penDown != _bladeDown) {
        onSolenoid(penDown, -1);
        _bladeDown = penDown;
    }
    stepper.setTarget(x, y, feed);

    _lastX = x;
    _lastY = y;
    _bladeAngle  = newAngle;
    _initialized = true;
}
