# ESP32 GCode Plotter — Improvement Plan

> Generated: 2026-06-05  
> Codebase: Sessions 1–15 as documented in AGENTS.md

---

## Overview

The project is architecturally sound. Improvements are grouped into six priority tiers:

| Tier | Label | Description |
|------|-------|-------------|
| ⚫ P0 | Critical Missing Feature | Core cutter functionality not yet implemented — directly affects cut quality |
| 🔴 P1 | Correctness Bugs | Wrong output or potential data corruption |
| 🟠 P2 | Safety & Robustness | Crashes, hangs, or silent failures under realistic conditions |
| 🟡 P3 | Performance & Quality | Inefficiencies and code quality issues |
| 🟢 P4 | Architecture | Maintainability and long-term structure |
| 🔵 P5 | Missing Features | Quick-win additions from the open issues list |

---

## ⚫ P0 — Critical Missing Feature: Drag Knife (Swivel Blade) Compensation  ✅ Done

> **This is the most impactful gap for a cutter.** Without it, every sharp direction change tears the media.

### Background — How a Drag Knife Works

The Cricut Expression blade is a **drag knife** (also called a swivel knife). The blade tip is offset from the pivot/carriage center by a small distance (typically 0.5–1.0 mm). The blade self-orients by trailing behind the pivot as the carriage moves:

```
  carriage pivot (stepper moves this)
        │
        │  ← swivel arm (offset ~0.75 mm)
        ▼
     ╱ blade tip  (this is what cuts)
```

At **gentle curves** (small angle changes per step), the blade rotates naturally as it trails — no problem.

At **sharp corners** (angle change > ~15°), the blade cannot rotate fast enough. The carriage pivots but the blade tip drags sideways through the material, **tearing it**.

### Solution — Lift / Pivot / Lower at Corners

Before every G-code move that changes direction by more than a threshold angle, the firmware must automatically insert:

1. **Lift blade** — raise solenoid (`M5`)
2. **Pivot the carriage** — move the pivot point forward past the corner by `KNIFE_OFFSET_MM` in the **new** direction, which swings the blade tip into alignment
3. **Lower blade** — lower solenoid (`M3`)
4. **Continue** on the planned path

This sequence is transparent to the G-code file; it is inserted at runtime by a compensation layer.

### Implementation Plan

#### Step 1 — Add config parameters (`src/config.h`)

```cpp
// Drag knife offset: distance from pivot to blade tip (mm)
// Measure from your specific blade holder; 0.5–1.0 mm is typical for Cricut blades
#define KNIFE_OFFSET_MM         0.75f

// Minimum angle change (degrees) that triggers a lift-pivot-lower sequence
// Below this threshold the blade self-corrects during normal motion
#define KNIFE_ANGLE_THRESHOLD_DEG  15.0f

// Enable/disable drag knife compensation (set 0 to use as a plain pen plotter)
#define KNIFE_COMPENSATION_ENABLE  1
```

#### Step 2 — Create `src/knife_comp.h` / `src/knife_comp.cpp`

```cpp
// knife_comp.h
#pragma once
#include <stdint.h>

// Call once at the start of each file playback
void knifeCompReset();

// Call this instead of directly calling stepper.setTarget() + solenoid.
// Inspects the direction change between the last move and this one.
// If the angle exceeds KNIFE_ANGLE_THRESHOLD_DEG and the blade is down,
// automatically inserts a lift/pivot/lower before the actual move.
//
// Parameters:
//   x, y      — destination in mm (already transformed by applyMoveTransform)
//   feed      — feedrate mm/min
//   penDown   — true if blade should be down for this move (G1 = true, G0 = false)
void knifeMove(float x, float y, float feed, bool penDown);
```

```cpp
// knife_comp.cpp
#include "knife_comp.h"
#include "config.h"
#include "stepper.h"
#include <math.h>

extern StepperControl stepper;
extern void onSolenoid(bool on, float pressure);

static float _lastX = 0, _lastY = 0;   // last pivot position
static float _bladeAngle = 0;          // current blade heading (radians)
static bool  _bladeDown  = false;      // current solenoid state
static bool  _initialized = false;

void knifeCompReset() {
    _lastX = stepper.currentX();
    _lastY = stepper.currentY();
    _bladeAngle   = 0;
    _bladeDown    = false;
    _initialized  = false;
}

void knifeMove(float x, float y, float feed, bool penDown) {
    float dx = x - _lastX;
    float dy = y - _lastY;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < 0.01f) {
        // Negligible move — just update pen state
        if (penDown != _bladeDown) {
            onSolenoid(penDown, -1);
            _bladeDown = penDown;
        }
        return;
    }

    float newAngle = atan2f(dy, dx);  // heading toward destination

#if KNIFE_COMPENSATION_ENABLE
    if (_initialized && _bladeDown) {
        // Angle change between last heading and new heading
        float delta = newAngle - _bladeAngle;
        // Normalise to -π .. +π
        while (delta >  M_PI) delta -= 2.0f * M_PI;
        while (delta < -M_PI) delta += 2.0f * M_PI;

        float threshRad = KNIFE_ANGLE_THRESHOLD_DEG * M_PI / 180.0f;
        if (fabsf(delta) > threshRad) {
            // ── Lift ──
            onSolenoid(false, -1);
            _bladeDown = false;

            // ── Pivot: move pivot forward in NEW direction by KNIFE_OFFSET_MM
            //    This swings the blade tip into the new heading.
            float pivotX = _lastX + cosf(newAngle) * KNIFE_OFFSET_MM;
            float pivotY = _lastY + sinf(newAngle) * KNIFE_OFFSET_MM;
            pivotX = constrain(pivotX, 0, X_MAX_MM);
            pivotY = constrain(pivotY, 0, Y_MAX_MM);
            stepper.setTarget(pivotX, pivotY, feed * 0.5f);  // slow pivot
            // Wait for pivot move (caller's responsibility via state machine)
            // See integration note below.

            // ── Lower ──
            onSolenoid(true, -1);
            _bladeDown = true;
        }
    }
#endif

    // ── Execute the actual cut move ──
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
```

> **Integration note on the pivot wait:** Because `setTarget()` is asynchronous (motion runs on Core 0), the pivot move completes before the next move only if the caller returns to the `loop()` state machine between them. The cleanest approach is to split `knifeMove()` into a state machine with states `KNIFE_MOVE_CUTTING`, `KNIFE_PIVOT_WAIT`, `KNIFE_LOWER_WAIT` — or add a `KNIFE_PIVOT` state to the existing `State` enum and handle it in `loop()`.

#### Step 3 — Wire into G-code playback (`src/main.cpp`)

Replace the `onMove()` callback body:
```cpp
void onMove(float x, float y, float f) {
    if (!stepper.isHomed()) { Serial.println("error: not homed"); return; }
    applyMoveTransform(x, y);
    x = constrain(x, 0, X_MAX_MM);
    y = constrain(y, 0, Y_MAX_MM);

    // Determine if this is a cut move (G1) or rapid (G0)
    // G1 sets pen down; G0 lifts pen first
    bool penDown = solenoidOn;  // solenoid state set by preceding M3/M5
    knifeMove(x, y, f, penDown);

    state = RUNNING;
    display.setPosition(x, y);
}
```

Call `knifeCompReset()` at the start of each file playback (in `startCut()` and `onFile()`).

#### Step 4 — Menu setting for knife offset

Add `KNIFE_OFFSET_MM` as a user-adjustable setting in the Settings menu (alongside Calibrate), stored in NVS. This allows different blade holders to be calibrated without recompiling.

### Testing Protocol

1. Cut a square (90° corners) in paper — corners should be crisp, not rounded or torn
2. Cut a star (5 × 144° turns) — all inner corners should be clean
3. Cut a circle (no corners > threshold) — no lift events should occur
4. Disable compensation (`KNIFE_COMPENSATION_ENABLE 0`) and cut the same square — verify tearing is visible, confirming the feature works
5. Try `KNIFE_OFFSET_MM` values 0.5, 0.75, 1.0 and compare corner quality

---

## 🔴 P1 — Correctness Bugs  ✅ All Done

### P1-1 · Smooth bezier control-point reflection is a no-op *(geometry rendering bug — unrelated to knife rotation)*
**Files:** `src/svg_parser.cpp` lines 122–123, 141–142  ✅ Done

> ⚠️ **Note:** This is a **mathematical path geometry bug** — the SVG curve shape is computed incorrectly. It is entirely separate from the drag knife compensation in P0. P0 is about *how the blade moves*; P1-1 is about *which path the SVG describes*.

SVG `S`/`s` (smooth cubic) and `T`/`t` (smooth quadratic) commands must reflect the **previous control point** across the current pen position to produce a smooth tangent join between segments. The current code subtracts the pen position from itself, which is a no-op:

```cpp
// BUG: 2*_cx - _cx == _cx — the reflection produces the same point, no smoothing
x1 = 2 * _cx - _cx;   // always equals _cx
y1 = 2 * _cy - _cy;   // always equals _cy
```

The result: `S`/`T` segments behave identically to `C`/`Q` with the first control point at the pen position — shapes like rounded rectangles, smooth lettering, and organic curves will have visible kinks at every smooth-join vertex.

**Fix:** Track the previous control point in new `SVGParser` members `_prevCpX` / `_prevCpY`. Update them after every `C`/`S`/`Q`/`T` command:

```cpp
// In SVGParser class (svg_parser.h), add:
float _prevCpX{0}, _prevCpY{0};

// S/s command — reflect the last C/S control point:
if (prevCmd == 'C' || prevCmd == 'S') {
    x1 = 2 * _cx - _prevCpX;   // true reflection
    y1 = 2 * _cy - _prevCpY;
}
// After doCubic(): _prevCpX = x2; _prevCpY = y2;  (x2,y2 = last control point)

// T/t command — reflect the last Q/T control point:
if (prevCmd == 'Q' || prevCmd == 'T') {
    x1 = 2 * _cx - _prevCpX;
    y1 = 2 * _cy - _prevCpY;
}
// After doQuad(): _prevCpX = x1; _prevCpY = y1;
```

**Impact:** Any SVG exported from Inkscape, Illustrator, or Figma that uses smooth curve joins (`S`/`T` path commands) will have kinks at every smooth-join vertex — common in text outlines, logos, and organic shapes.

---

### P1-2 · Dual-core `moveComplete` flag is not atomically protected  ✅ Done (std::atomic instead)
**Files:** `src/main.cpp`

`moveComplete` and `state` were changed from `volatile` to `std::atomic<State>` / `std::atomic<bool>` with `memory_order_seq_cst`. This avoids the `portMUX_TYPE` spinlock while providing the same memory ordering guarantees. `state.load()` is cast in `onReport()` printf.

---

### P1-3 · Busy-wait in Core 1 blocks USB polling and display during Line Return / Auto-fill  ✅ Done
**Files:** `src/main.cpp`

All `while(state == RUNNING)` busy-waits replaced with a `PostCutAction` enum continuation pattern. Actions like replay, line return, and multi-cut are scheduled as post-cut actions and handled in the next `loop()` tick instead of blocking.

---

### P1-4 · `adcToLevel()` fallback never reaches level 5  ✅ Done
**Files:** `src/main.cpp`

Changed to ceiling division: `constrain((raw * 5 + 4095) / 4096, 1, 5)`

---

## 🟠 P2 — Safety & Robustness  ✅ All Done

### P2-1 · USB drive disconnection during playback causes infinite loop  ✅ Done
**Files:** `src/usb_drive.cpp` (MSC layer), `src/main.cpp` `playFileFromBuffer()`

If the drive is removed mid-cut, `mscReadSectors()` times out (5 s) and returns `ESP_FAIL`. The caller returns `false` but `state` remains `PLAYING_SD`. The PLAYING_SD case calls `playFileFromBuffer()` again next tick, which reads from `psramBuf` (already loaded) — actually safe. But if the failure happens during a multi-cut reload (`usbDrive.loadFile()`), the reload fails silently and the loop continues with stale data.

**Fix:**
1. In `mscCommand()`, on timeout set `d.diskReady = false`.
2. In `USBDrive::isReady()`, return `d.diskReady`.
3. In the multi-cut reload block in `loop()`, check `usbDrive.isReady()` before `loadFile()` and abort with `usbError()` if not ready.

---

### P2-2 · `motionTask` stack size is too small  ✅ Done
**Files:** `src/main.cpp`

Changed `xTaskCreatePinnedToCore("motion", ..., 8192, ...)` — increased from 4096 to 8192.

---

### P2-3 · `handleUpload()` calls `strlen()` on non-null-terminated WebSocket data  ✅ Done
**Files:** `src/main.cpp`, `src/wifi_server.cpp`

`handleUpload()` now takes `size_t dataLen` and uses `memchr(data, '\n', dataLen)` instead of `strlen()`. The WebSocket callback passes the frame's `len` to `_cmdCb`.

---

### P2-4 · `beep()` blocks Core 1 for up to 400 ms  ✅ Done
**Files:** `src/main.cpp`

Replaced `delay()` beeps with a timestamp-based state machine. `beepOffAt`/`beepNextAt` track timing; `updateBeep()` is called from `loop()` to turn the buzzer off and advance through error patterns.

---

### P2-5 · `gpioSet()` in stepper silently fails for GPIO ≥ 32  ✅ Done
**Files:** `src/stepper.cpp`

`gpioSet()` and `gpioReadRaw()` now check `pin < 32` to use `GPIO.out_w1ts` vs `GPIO.out1_w1ts.val` for the correct register bank. Currently no pins ≥ 32 are used, but this future-proofs against re-assignment.

---

### P2-6 · NVS stores WiFi password in plaintext
**Status:** Not implemented. Add NVS encryption in `platformio.ini` or document in README.

---

## 🟡 P3 — Performance & Quality  ✅ All Done

### P3-1 · `evalProfile()` called twice per `run()` tick (finite-difference velocity)  ✅ Done
**Files:** `src/stepper.cpp`

Added `evalVelocity(float t)` — O(1) closed-form analytical velocity using per-phase math, replacing `(evalProfile(elapsed + dtEval) - evalProfile(elapsed)) / dtEval`. Halves `run()` compute cost.

---

### P3-2 · FAT32 `findFreeCluster()` does a full linear scan  ✅ Done
**Files:** `src/usb_drive.cpp`

`initFAT()` now reads the FSInfo sector (LBA = `bpb->fsInfo`) to extract `freeClusHint` (offset 488, 4 bytes). `findFreeCluster()` uses the hint as the scan start and wraps around to cluster 2 if no free cluster is found after the hint. Updates `freeClusHint` after each allocation.

---

### P3-3 · `scanBoundingBox()` blocks Core 1 for the full PSRAM buffer  ✅ Done
**Files:** `src/main.cpp`, `src/svg_parser.cpp`

SVG bounding box accumulated during conversion: `svgMoveTo()` and `svgLineTo()` callbacks update `minX/maxX/minY/maxY` in real time, zero extra cost. G-code bounding box is still scanned from buffer when needed.

---

### P3-4 · Settings (Language, Units, Mat Size, Char Images) are not saved to NVS  ✅ Done
**Files:** `src/menu.cpp`

Added `saveSettings()` and `loadSettings()` in `menu.cpp` using the `"plotter"` Preferences namespace. Called automatically when user exits a Settings sub-menu. `loadSettings()` is called from `setPlotterState()` at startup.

---

## 🟢 P4 — Architecture & Maintainability

### P4-1 · Split `main.cpp` (1604 lines) into focused modules  
**Status:** Not implemented

Suggested new files:

| New File | Content to Extract |
|---|---|
| `src/plotter_ctrl.cpp/h` | `startCut()`, `setupModeTransforms()`, `applyMoveTransform()`, `scanBoundingBox()`, `buildGcodePath()`, `plotSVG()`, `onMenuPlot()` |
| `src/calibration.cpp/h` | `loadCalibration()`, `saveCalSpeed()`, `saveCalPressure()`, `resetCalibration()`, `snapToDetent()`, `adcToLevel()` |
| `src/pot_reader.cpp/h` | `updatePressure()`, `updateSpeed()`, `updateEncoder()` |
| `src/serial_cmd.cpp/h` | `handleSerial()`, `processLine()`, `handleUpload()`, `onWiFiCmd()`, `handleMenuCmd()` |
| `src/ota_update.cpp/h` | `performFirmwareUpdate()`, `onFwUpdate()` |

`main.cpp` would then contain only `setup()`, `loop()`, `motionTask()`, and G-code callbacks (`onMove`, `onHome`, etc.).

---

### P4-2 · Centralise magic numbers  ✅ Done

All added to `config.h`:
- `SCURVE_VEL_DT` (0.002f), `SCURVE_VEL_DT_MIN` (0.0005f)
- `USB_TIMEOUT_MS` (5000)
- `HOMING_MIN_STEP_US` (50)
- `COPY_GAP_MM` (2.0f)
- `HPGL_DEFAULT_SCALE`, `HPGL_DEFAULT_IP_W`, `HPGL_DEFAULT_IP_H`

`BTN_DEBOUNCE_MS` (50) was already defined and is used consistently.

---

### P4-3 · Use `esp_timer_get_time()` consistently (not mixed with `millis()`)
**Status:** Not implemented. Intentional split: `esp_timer_get_time()` for motion, `millis()` for UI/debounce.

`stepper.cpp` uses `esp_timer_get_time()` (µs, 64-bit, monotonic). `main.cpp` uses `millis()`. These are derived from the same hardware timer but mixing them makes timing relationships harder to reason about. For the motion system, keep `esp_timer_get_time()`. For UI/debounce, keep `millis()`. Add a comment to clarify the intentional split.

---

## 🔵 P5 — Missing Features (Quick Wins)  ✅ All Done

### P5-1 · USB directory navigation (back to parent)  ✅ Done
**Files:** `src/menu.cpp`, `src/menu.h`, `src/usb_drive.cpp`

Added `_currentDir` path tracking to `PlotterMenu`, `enterDir()`/`leaveDir()` methods, and `enumerate()` now accepts an optional directory path parameter. The USB drive resolves subdirectory clusters and enumerates their contents. Back key in file list mode navigates to parent directory.

---

### P5-2 · Settings persistence (Language, Units, Mat Size, Char Images)  ✅ Done
See P3-4.

---

### P5-3 · SVG primitive element support (`<rect>`, `<circle>`, `<ellipse>`, `<line>`)  ✅ Done
**Files:** `src/svg_parser.cpp`

Added `<rect>`, `<circle>` (4 cubic bezier arcs), `<ellipse>`, `<line>`, `<polyline>`, and `<polygon>` parsers. Each converts to the equivalent `doMove`/`doLine`/`doCubic` path calls.

---

### P5-4 · HPGL `SC` (scale) and `IP` (input point) commands  ✅ Done
**Files:** `src/hpgl_parser.cpp`, `src/hpgl_parser.h`

Added `_scale`, `_ipW`, `_ipH`, `_scX1/_scY1/_scX2/_scY2`, and `_scSet` members. `SC x1,y1,x2,y2` sets user-unit scaling; `IP x1,y1,x2,y2` sets input P1/P2 extents. `hpglToMM()` applies the transform when `_scSet` is true.

---

### P5-5 · Non-blocking error beep (prerequisite: P2-4)  ✅ Done
Non-blocking beep state machine implemented in `main.cpp` as part of P2-4.

---

### P5-6 · Serial `$status` JSON dump  ✅ Done
**Files:** `src/main.cpp`

Added `/^\$status/` handler that prints `PlotterState` as JSON: mode, speed level, pressure level, position, state, zoom, and solenoid state.

---

## Recommended Implementation Order

```
Phase 0 — Drag Knife Compensation  ✅ Done (Session 15)
  P0    Add KNIFE_OFFSET_MM + KNIFE_ANGLE_THRESHOLD_DEG to config.h
  P0    Create src/knife_comp.h + src/knife_comp.cpp
  P0    Wire knifeMove() into onMove() callback in main.cpp
  P0    Call knifeCompReset() in startCut() / onFile()
  (Knife offset menu setting: deferred — NVS addition would be straightforward)

Phase A — Bug fixes  ✅ Done (Session 15)
  P1-1  SVG smooth bezier reflection (fix _prevCpX/_prevCpY tracking)
  P1-4  adcToLevel ceiling division
  P2-2  motionTask stack → 8192
  P2-5  gpioSet bank-aware macro
  P1-2  moveComplete → std::atomic
  P1-3  Non-blocking post-cut (replaced while(state==RUNNING) with PostCutAction enum)

Phase B — Robustness  ✅ Done (Session 15)
  P2-1  USB disconnect recovery (pollUSB periodic test, reset state on failure)
  P2-3  WebSocket data length passed to callback
  P2-4  Non-blocking beep (timestamp state machine)

Phase C — Quick wins  ✅ Done (Session 15)
  P3-4  Settings NVS persistence
  P5-6  $status command
  P5-4  HPGL SC/IP commands
  P4-2  Magic number constants

Phase D — Performance  ✅ Done (Session 15)
  P3-1  evalVelocity() closed-form
  P3-2  FSInfo free-cluster hint
  P3-3  Bounding box during SVG conversion

Phase E — Architecture (2 sessions)  ❌ Not started
  P4-1  Split main.cpp

Phase F — Features  ✅ Done (Session 15)
  P5-1  USB directory navigation
  P5-3  SVG primitives
```

---

## Verification Plan

After each phase, run:
```bash
cd /Users/mariacabrera/Documents/TestVScode/C_Cut_E-plotter
~/.platformio/penv/bin/pio run
```

Target build budget: **RAM ≤ 60 KB** (currently 51 KB / 15.9%), **Flash ≤ 1 MB** (currently 827 KB / 25.9%).

Hardware tests after Phase 0 (drag knife):
- Cut a 50×50 mm square — all 4 corners should be clean right angles, not torn
- Cut a 5-pointed star — inner 144° corners should be crisp
- Cut a circle — no lift events, smooth continuous arc
- Compare with `KNIFE_COMPENSATION_ENABLE 0` — tearing should be visible, confirming the feature works
- Tune `KNIFE_OFFSET_MM` (try 0.5, 0.75, 1.0) until corners are sharpest

Hardware tests after Phase A–B:
- Home X, jog blade with arrow keys, verify position display
- Load a test SVG with smooth curves (e.g. a circle with `S` path commands) — verify no kinks at smooth-join vertices
- Hot-unplug USB during playback — verify graceful stop with error message
- Run error beep — verify USB remains connected throughout
