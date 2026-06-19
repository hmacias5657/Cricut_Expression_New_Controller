1: # ESP32 GCode Plotter — Improvement Plan
2: 
3: > Generated: 2026-06-05  
4: > Codebase: Sessions 1–16 as documented in AGENTS.md
5: 
6: ---
7: 
8: ## Intake
9: 
10: - **User request**: "ESP32 GCode Plotter improvement plan"
11: - **Clear**: yes
12: - **Affected projects**: esp32-gcode-plotter
13: - **Size**: Medium
14: - **Dependency graph**: Serial (phases depend on each other)
15: 
16: ---
17: 
18: ## Execute
19: 
20: ## Overview
21: 
22: The project is architecturally sound. Improvements are grouped into six priority tiers:
23: 
24: | Tier | Label | Description |
25: |------|-------|-------------|
26: | ⚫ P0 | Critical Missing Feature | Core cutter functionality not yet implemented — directly affects cut quality |
27: | 🔴 P1 | Correctness Bugs | Wrong output or potential data corruption |
28: | 🟠 P2 | Safety & Robustness | Crashes, hangs, or silent failures under realistic conditions |
29: | 🟡 P3 | Performance & Quality | Inefficiencies and code quality issues |
30: | 🟢 P4 | Architecture | Maintainability and long-term structure |
31: | 🔵 P5 | Missing Features | Quick-win additions from the open issues list |
32: 
33: ---
34: 
35: ## ⚫ P0 — Critical Missing Feature: Drag Knife (Swivel Blade) Compensation  ✅ Done
36: 
37: > **This is the most impactful gap for a cutter.** Without it, every sharp direction change tears the media.
38: 
39: ### Background — How a Drag Knife Works
40: 
41: The Cricut Expression blade is a **drag knife** (also called a swivel knife). The blade tip is offset from the pivot/carriage center by a small distance (typically 0.5–1.0 mm). The blade self-orients by trailing behind the pivot as the carriage moves:
42: 
43: ```
44:   carriage pivot (stepper moves this)
45:         │
46:         │  ← swivel arm (offset ~0.75 mm)
47:         ▼
48:      ╱ blade tip  (this is what cuts)
49: ```
50: 
51: At **gentle curves** (small angle changes per step), the blade rotates naturally as it trails — no problem.
52: 
53: At **sharp corners** (angle change > ~15°), the blade cannot rotate fast enough. The carriage pivots but the blade tip drags sideways through the material, **tearing it**.
54: 
55: ### Solution — Lift / Pivot / Lower at Corners
56: 
57: Before every G-code move that changes direction by more than a threshold angle, the firmware must automatically insert:
58: 
59: 1. **Lift blade** — raise solenoid (`M5`)
60: 2. **Pivot the carriage** — move the pivot point forward past the corner by `KNIFE_OFFSET_MM` in the **new** direction, which swings the blade tip into alignment
61: 3. **Lower blade** — lower solenoid (`M3`)
62: 4. **Continue** on the planned path
63: 
64: This sequence is transparent to the G-code file; it is inserted at runtime by a compensation layer.
65: 
66: ### Implementation Plan
67: 
68: #### Step 1 — Add config parameters (`src/config.h`)
69: 
70: ```cpp
71: // Drag knife offset: distance from pivot to blade tip (mm)
72: // Measure from your specific blade holder; 0.5–1.0 mm is typical for Cricut blades
73: #define KNIFE_OFFSET_MM         0.75f
74: 
75: // Minimum angle change (degrees) that triggers a lift-pivot-lower sequence
76: // Below this threshold the blade self-corrects during normal motion
77: #define KNIFE_ANGLE_THRESHOLD_DEG  15.0f
78: 
79: // Enable/disable drag knife compensation (set 0 to use as a plain pen plotter)
80: #define KNIFE_COMPENSATION_ENABLE  1
81: ```
82: 
83: #### Step 2 — Create `src/knife_comp.h` / `src/knife_comp.cpp`
84: 
85: ```cpp
86: // knife_comp.h
87: #pragma once
88: #include <stdint.h>
89: 
90: // Call once at the start of each file playback
91: void knifeCompReset();
92: 
93: // Call this instead of directly calling stepper.setTarget() + solenoid.
94: // Inspects the direction change between the last move and this one.
95: // If the angle exceeds KNIFE_ANGLE_THRESHOLD_DEG and the blade is down,
96: // automatically inserts a lift/pivot/lower before the actual move.
97: //
98: // Parameters:
99: //   x, y      — destination in mm (already transformed by applyMoveTransform)
100: //   feed      — feedrate mm/min
101: //   penDown   — true if blade should be down for this move (G1 = true, G0 = false)
102: void knifeMove(float x, float y, float feed, bool penDown);
103: ```
104: 
105: ```cpp
106: // knife_comp.cpp
107: #include "knife_comp.h"
108: #include "config.h"
109: #include "stepper.h"
110: #include <math.h>
111: 
112: extern StepperControl stepper;
113: extern void onSolenoid(bool on, float pressure);
114: 
115: static float _lastX = 0, _lastY = 0;   // last pivot position
116: static float _bladeAngle = 0;          // current blade heading (radians)
117: static bool  _bladeDown  = false;      // current solenoid state
118: static bool  _initialized = false;
119: 
120: void knifeCompReset() {
121:     _lastX = stepper.currentX();
122:     _lastY = stepper.currentY();
123:     _bladeAngle   = 0;
124:     _bladeDown    = false;
125:     _initialized  = false;
126: }
127: 
128: void knifeMove(float x, float y, float feed, bool penDown) {
129:     float dx = x - _lastX;
130:     float dy = y - _lastY;
131:     float dist = sqrtf(dx * dx + dy * dy);
132: 
133:     if (dist < 0.01f) {
134:         // Negligible move — just update pen state
135:         if (penDown != _bladeDown) {
136:             onSolenoid(penDown, -1);
137:             _bladeDown = penDown;
138:         }
139:         return;
140:     }
141: 
142:     float newAngle = atan2f(dy, dx);  // heading toward destination
143: 
144: #if KNIFE_COMPENSATION_ENABLE
145:     if (_initialized && _bladeDown) {
146:         // Angle change between last heading and new heading
147:         float delta = newAngle - _bladeAngle;
148:         // Normalise to -π .. +π
149:         while (delta >  M_PI) delta -= 2.0f * M_PI;
150:         while (delta < -M_PI) delta += 2.0f * M_PI;
151: 
152:         float threshRad = KNIFE_ANGLE_THRESHOLD_DEG * M_PI / 180.0f;
153:         if (fabsf(delta) > threshRad) {
154:             // ── Lift ──
155:             onSolenoid(false, -1);
156:             _bladeDown = false;
157: 
158:             // ── Pivot: move pivot forward in NEW direction by KNIFE_OFFSET_MM
159:             //    This swings the blade tip into the new heading.
160:             float pivotX = _lastX + cosf(newAngle) * KNIFE_OFFSET_MM;
161:             float pivotY = _lastY + sinf(newAngle) * KNIFE_OFFSET_MM;
162:             pivotX = constrain(pivotX, 0, X_MAX_MM);
163:             pivotY = constrain(pivotY, 0, Y_MAX_MM);
164:             stepper.setTarget(pivotX, pivotY, feed * 0.5f);  // slow pivot
165:             // Wait for pivot move (caller's responsibility via state machine)
166:             // See integration note below.
167: 
168:             // ── Lower ──
169:             onSolenoid(true, -1);
170:             _bladeDown = true;
171:         }
172:     }
173: #endif
174: 
175:     // ── Execute the actual cut move ──
176:     if (penDown != _bladeDown) {
177:         onSolenoid(penDown, -1);
178:         _bladeDown = penDown;
179:     }
180:     stepper.setTarget(x, y, feed);
181: 
182:     _lastX = x;
183:     _lastY = y;
184:     _bladeAngle  = newAngle;
185:     _initialized = true;
186: }
187: ```
188: 
189: > **Integration note on the pivot wait:** Because `setTarget()` is asynchronous (motion runs on Core 0), the pivot move completes before the next move only if the caller returns to the `loop()` state machine between them. The cleanest approach is to split `knifeMove()` into a state machine with states `KNIFE_MOVE_CUTTING`, `KNIFE_PIVOT_WAIT`, `KNIFE_LOWER_WAIT` — or add a `KNIFE_PIVOT` state to the existing `State` enum and handle it in `loop()`.
190: 
191: #### Step 3 — Wire into G-code playback (`src/main.cpp`)
192: 
193: Replace the `onMove()` callback body:
194: ```cpp
195: void onMove(float x, float y, float f) {
196:     if (!stepper.isHomed()) { Serial.println("error: not homed"); return; }
197:     applyMoveTransform(x, y);
198:     x = constrain(x, 0, X_MAX_MM);
199:     y = constrain(y, 0, Y_MAX_MM);
200: 
201:     // Determine if this is a cut move (G1) or rapid (G0)
202:     // G1 sets pen down; G0 lifts pen first
203:     bool penDown = solenoidOn;  // solenoid state set by preceding M3/M5
204:     knifeMove(x, y, f, penDown);
205: 
206:     state = RUNNING;
207:     display.setPosition(x, y);
208: }
209: ```
210: 
211: Call `knifeCompReset()` at the start of each file playback (in `startCut()` and `onFile()`).
212: 
213: #### Step 4 — Menu setting for knife offset
214: 
215: Add `KNIFE_OFFSET_MM` as a user-adjustable setting in the Settings menu (alongside Calibrate), stored in NVS. This allows different blade holders to be calibrated without recompiling.
216: 
217: ### Testing Protocol
218: 
219: 1. Cut a square (90° corners) in paper — corners should be crisp, not rounded or torn
220: 2. Cut a star (5 × 144° turns) — all inner corners should be clean
221: 3. Cut a circle (no corners > threshold) — no lift events should occur
222: 4. Disable compensation (`KNIFE_COMPENSATION_ENABLE 0`) and cut the same square — verify tearing is visible, confirming the feature works
223: 5. Try `KNIFE_OFFSET_MM` values 0.5, 0.75, 1.0 and compare corner quality
224: 
225: Pipeline: Medium (full pipeline)
226: 
227: ---
228: 
229: ## 🔴 P1 — Correctness Bugs  ✅ All Done
230: 
231: ### P1-1 · Smooth bezier control-point reflection is a no-op *(geometry rendering bug — unrelated to knife rotation)*
232: **Files:** `src/svg_parser.cpp` lines 122–123, 141–142  ✅ Done
233: 
234: > ⚠️ **Note:** This is a **mathematical path geometry bug** — the SVG curve shape is computed incorrectly. It is entirely separate from the drag knife compensation in P0. P0 is about *how the blade moves*; P1-1 is about *which path the SVG describes*.
235: 
236: SVG `S`/`s` (smooth cubic) and `T`/`t` (smooth quadratic) commands must reflect the **previous control point** across the current pen position to produce a smooth tangent join between segments. The current code subtracts the pen position from itself, which is a no-op:
237: 
238: ```cpp
239: // BUG: 2*_cx - _cx == _cx — the reflection produces the same point, no smoothing
240: x1 = 2 * _cx - _cx;   // always equals _cx
241: y1 = 2 * _cy - _cy;   // always equals _cy
242: ```
243: 
244: The result: `S`/`T` segments behave identically to `C`/`Q` with the first control point at the pen position — shapes like rounded rectangles, smooth lettering, and organic curves will have visible kinks at every smooth-join vertex.
245: 
246: **Fix:** Track the previous control point in new `SVGParser` members `_prevCpX` / `_prevCpY`. Update them after every `C`/`S`/`Q`/`T` command:
247: 
248: ```cpp
249: // In SVGParser class (svg_parser.h), add:
250: float _prevCpX{0}, _prevCpY{0};
251: 
252: // S/s command — reflect the last C/S control point:
253: if (prevCmd == 'C' || prevCmd == 'S') {
254:     x1 = 2 * _cx - _prevCpX;   // true reflection
255:     y1 = 2 * _cy - _prevCpY;
256: }
257: // After doCubic(): _prevCpX = x2; _prevCpY = y2;  (x2,y2 = last control point)
258: 
259: // T/t command — reflect the last Q/T control point:
260: if (prevCmd == 'Q' || prevCmd == 'T') {
261:     x1 = 2 * _cx - _prevCpX;
262:     y1 = 2 * _cy - _prevCpY;
263: }
264: // After doQuad(): _prevCpX = x1; _prevCpY = y1;
265: ```
266: 
267: **Impact:** Any SVG exported from Inkscape, Illustrator, or Figma that uses smooth curve joins (`S`/`T` path commands) will have kinks at every smooth-join vertex — common in text outlines, logos, and organic shapes.
268: 
269: Pipeline: Trivial (code → build → commit)
270: 
271: ### P1-2 · Dual-core `moveComplete` flag is not atomically protected  ✅ Done (std::atomic instead)
272: **Files:** `src/main.cpp`
273: 
274: `moveComplete` and `state` were changed from `volatile` to `std::atomic<State>` / `std::atomic<bool>` with `memory_order_seq_cst`. This avoids the `portMUX_TYPE` spinlock while providing the same memory ordering guarantees. `state.load()` is cast in `onReport()` printf.
275: 
276: Pipeline: Trivial (code → build → commit)
277: 
278: ### P1-3 · Busy-wait in Core 1 blocks USB polling and display during Line Return / Auto-fill  ✅ Done
279: **Files:** `src/main.cpp`
280: 
281: All `while(state == RUNNING)` busy-waits replaced with a `PostCutAction` enum continuation pattern. Actions like replay, line return, and multi-cut are scheduled as post-cut actions and handled in the next `loop()` tick instead of blocking.
282: 
283: Pipeline: Trivial (code → build → commit)
284: 
285: ### P1-4 · `adcToLevel()` fallback never reaches level 5  ✅ Done
286: **Files:** `src/main.cpp`
287: 
288: Changed to ceiling division: `constrain((raw * 5 + 4095) / 4096, 1, 5)`
289: 
290: Pipeline: Trivial (code → build → commit)
291: 
292: ---
293: 
294: ## 🟠 P2 — Safety & Robustness  ✅ All Done
295: 
296: ### P2-1 · USB drive disconnection during playback causes infinite loop  ✅ Done
297: **Files:** `src/usb_drive.cpp` (MSC layer), `src/main.cpp` `playFileFromBuffer()`
298: 
299: If the drive is removed mid-cut, `mscReadSectors()` times out (5 s) and returns `ESP_FAIL`. The caller returns `false` but `state` remains `PLAYING_SD`. The PLAYING_SD case calls `playFileFromBuffer()` again next tick, which reads from `psramBuf` (already loaded) — actually safe. But if the failure happens during a multi-cut reload (`usbDrive.loadFile()`), the reload fails silently and the loop continues with stale data.
300: 
301: **Fix:**
302: 1. In `mscCommand()`, on timeout set `d.diskReady = false`.
303: 2. In `USBDrive::isReady()`, return `d.diskReady`.
304: 3. In the multi-cut reload block in `loop()`, check `usbDrive.isReady()` before `loadFile()` and abort with `usbError()` if not ready.
305: 
306: Pipeline: Trivial (code → build → commit)
307: 
308: ### P2-2 · `motionTask` stack size is too small  ✅ Done
309: **Files:** `src/main.cpp`
310: 
311: Changed `xTaskCreatePinnedToCore("motion", ..., 8192, ...)` — increased from 4096 to 8192.
312: 
313: Pipeline: Trivial (code → build → commit)
314: 
315: ### P2-3 · `handleUpload()` calls `strlen()` on non-null-terminated WebSocket data  ✅ Done
316: **Files:** `src/main.cpp`, `src/wifi_server.cpp`
317: 
318: `handleUpload()` now takes `size_t dataLen` and uses `memchr(data, '\n', dataLen)` instead of `strlen()`. The WebSocket callback passes the frame's `len` to `_cmdCb`.
319: 
320: Pipeline: Trivial (code → build → commit)
321: 
322: ### P2-4 · `beep()` blocks Core 1 for up to 400 ms  ✅ Done
323: **Files:** `src/main.cpp`
324: 
325: Replaced `delay()` beeps with a timestamp-based state machine. `beepOffAt`/`beepNextAt` track timing; `updateBeep()` is called from `loop()` to turn the buzzer off and advance through error patterns.
326: 
327: Pipeline: Trivial (code → build → commit)
328: 
329: ### P2-5 · `gpioSet()` in stepper silently fails for GPIO ≥ 32  ✅ Done
330: **Files:** `src/stepper.cpp`
331: 
332: `gpioSet()` and `gpioReadRaw()` now check `pin < 32` to use `GPIO.out_w1ts` vs `GPIO.out1_w1ts.val` for the correct register bank. Currently no pins ≥ 32 are used, but this future-proofs against re-assignment.
333: 
334: Pipeline: Trivial (code → build → commit)
335: 
336: ### P2-6 · NVS stores WiFi password in plaintext
337: **Status:** Not implemented. Add NVS encryption in `platformio.ini` or document in README.
338: 
339: Pipeline: Small (explore → code → build → test → commit)
340: 
341: ---
342: 
343: ## 🟡 P3 — Performance & Quality  ✅ All Done
344: 
345: ### P3-1 · `evalProfile()` called twice per `run()` tick (finite-difference velocity)  ✅ Done
346: **Files:** `src/stepper.cpp`
347: 
348: Added `evalVelocity(float t)` — O(1) closed-form analytical velocity using per-phase math, replacing `(evalProfile(elapsed + dtEval) - evalProfile(elapsed)) / dtEval`. Halves `run()` compute cost.
349: 
350: Pipeline: Trivial (code → build → commit)
351: 
352: ### P3-2 · FAT32 `findFreeCluster()` does a full linear scan  ✅ Done
353: **Files:** `src/usb_drive.cpp`
354: 
355: `initFAT()` now reads the FSInfo sector (LBA = `bpb->fsInfo`) to extract `freeClusHint` (offset 488, 4 bytes). `findFreeCluster()` uses the hint as the scan start and wraps around to cluster 2 if no free cluster is found after the hint. Updates `freeClusHint` after each allocation.
356: 
357: Pipeline: Trivial (code → build → commit)
358: 
359: ### P3-3 · `scanBoundingBox()` blocks Core 1 for the full PSRAM buffer  ✅ Done
360: **Files:** `src/main.cpp`, `src/svg_parser.cpp`
361: 
362: SVG bounding box accumulated during conversion: `svgMoveTo()` and `svgLineTo()` callbacks update `minX/maxX/minY/maxY` in real time, zero extra cost. G-code bounding box is still scanned from buffer when needed.
363: 
364: Pipeline: Trivial (code → build → commit)
365: 
366: ### P3-4 · Settings (Language, Units, Mat Size, Char Images) are not saved to NVS  ✅ Done
367: **Files:** `src/menu.cpp`
368: 
369: Added `saveSettings()` and `loadSettings()` in `menu.cpp` using the `"plotter"` Preferences namespace. Called automatically when user exits a Settings sub-menu. `loadSettings()` is called from `setPlotterState()` at startup.
370: 
371: Pipeline: Trivial (code → build → commit)
372: 
373: ---
374: 
375: ## 🟢 P4 — Architecture & Maintainability
376: 
377: ### P4-1 · Split `main.cpp` (1604 lines) into focused modules  
378: **Status:** Not implemented
379: 
380: Suggested new files:
381: 
382: | New File | Content to Extract |
383: |---|---|
384: | `src/plotter_ctrl.cpp/h` | `startCut()`, `setupModeTransforms()`, `applyMoveTransform()`, `scanBoundingBox()`, `buildGcodePath()`, `plotSVG()`, `onMenuPlot()` |
385: | `src/calibration.cpp/h` | `loadCalibration()`, `saveCalSpeed()`, `saveCalPressure()`, `resetCalibration()`, `snapToDetent()`, `adcToLevel()` |
386: | `src/pot_reader.cpp/h` | `updatePressure()`, `updateSpeed()`, `updateEncoder()` |
387: | `src/serial_cmd.cpp/h` | `handleSerial()`, `processLine()`, `handleUpload()`, `onWiFiCmd()`, `handleMenuCmd()` |
388: | `src/ota_update.cpp/h` | `performFirmwareUpdate()`, `onFwUpdate()` |
389: 
390: `main.cpp` would then contain only `setup()`, `loop()`, `motionTask()`, and G-code callbacks (`onMove`, `onHome`, etc.).
391: 
392: Pipeline: Large (full pipeline with envelope)
393: 
394: ### P4-2 · Centralise magic numbers  ✅ Done
395: 
396: All added to `config.h`:
397: - `SCURVE_VEL_DT` (0.002f), `SCURVE_VEL_DT_MIN` (0.0005f)
398: - `USB_TIMEOUT_MS` (5000)
399: - `HOMING_MIN_STEP_US` (50)
400: - `COPY_GAP_MM` (2.0f)
401: - `HPGL_DEFAULT_SCALE`, `HPGL_DEFAULT_IP_W`, `HPGL_DEFAULT_IP_H`
402: 
403: `BTN_DEBOUNCE_MS` (50) was already defined and is used consistently.
404: 
405: Pipeline: Trivial (code → build → commit)
406: 
407: ### P4-3 · Use `esp_timer_get_time()` consistently (not mixed with `millis()`)
408: **Status:** Not implemented. Intentional split: `esp_timer_get_time()` for motion, `millis()` for UI/debounce.
409: 
410: `stepper.cpp` uses `esp_timer_get_time()` (µs, 64-bit, monotonic). `main.cpp` uses `millis()`. These are derived from the same hardware timer but mixing them makes timing relationships harder to reason about. For the motion system, keep `esp_timer_get_time()`. For UI/debounce, keep `millis()`. Add a comment to clarify the intentional split.
411: 
412: Pipeline: Small (explore → code → build → test → commit)
413: 
414: ---
415: 
416: ## 🔵 P5 — Missing Features (Quick Wins)  ✅ All Done
417: 
418: ### P5-1 · USB directory navigation (back to parent)  ✅ Done
419: **Files:** `src/menu.cpp`, `src/menu.h`, `src/usb_drive.cpp`
420: 
421: Added `_currentDir` path tracking to `PlotterMenu`, `enterDir()`/`leaveDir()` methods, and `enumerate()` now accepts an optional directory path parameter. The USB drive resolves subdirectory clusters and enumerates their contents. Back key in file list mode navigates to parent directory.
422: 
423: Pipeline: Trivial (code → build → commit)
424: 
425: ### P5-2 · Settings persistence (Language, Units, Mat Size, Char Images)  ✅ Done
426: See P3-4.
427: 
428: Pipeline: Trivial (code → build → commit)
429: 
430: ### P5-3 · SVG primitive element support (`<rect>`, `<circle>`, `<ellipse>`, `<line>`)  ✅ Done
431: **Files:** `src/svg_parser.cpp`
432: 
433: Added `<rect>`, `<circle>` (4 cubic bezier arcs), `<ellipse>`, `<line>`, `<polyline>`, and `<polygon>` parsers. Each converts to the equivalent `doMove`/`doLine`/`doCubic` path calls.
434: 
435: Pipeline: Trivial (code → build → commit)
436: 
437: ### P5-4 · HPGL `SC` (scale) and `IP` (input point) commands  ✅ Done
438: **Files:** `src/hpgl_parser.cpp`, `src/hpgl_parser.h`
439: 
440: Added `_scale`, `_ipW`, `_ipH`, `_scX1/_scY1/_scX2/_scY2`, and `_scSet` members. `SC x1,y1,x2,y2` sets user-unit scaling; `IP x1,y1,x2,y2` sets input P1/P2 extents. `hpglToMM()` applies the transform when `_scSet` is true.
441: 
442: Pipeline: Trivial (code → build → commit)
443: 
444: ### P5-5 · Non-blocking error beep (prerequisite: P2-4)  ✅ Done
445: Non-blocking beep state machine implemented in `main.cpp` as part of P2-4.
446: 
447: Pipeline: Trivial (code → build → commit)
448: 
449: ### P5-6 · Serial `$status` JSON dump  ✅ Done
450: **Files:** `src/main.cpp`
451: 
452: Added `/^\$status/` handler that prints `PlotterState` as JSON: mode, speed level, pressure level, position, state, zoom, and solenoid state.
453: 
454: Pipeline: Trivial (code → build → commit)
455: 
456: ---
457: 
458: ## Recommended Implementation Order
459: 
460: ```
461: Phase 0 — Drag Knife Compensation  ✅ Done (Session 15)
462:   P0    Add KNIFE_OFFSET_MM + KNIFE_ANGLE_THRESHOLD_DEG to config.h
463:   P0    Create src/knife_comp.h + src/knife_comp.cpp
464:   P0    Wire knifeMove() into onMove() callback in main.cpp
465:   P0    Call knifeCompReset() in startCut() / onFile()
466:   (Knife offset menu setting: deferred — NVS addition would be straightforward)
467: 
468: Phase A — Bug fixes  ✅ Done (Session 15)
469:   P1-1  SVG smooth bezier reflection (fix _prevCpX/_prevCpY tracking)
470:   P1-4  adcToLevel ceiling division
471:   P2-2  motionTask stack → 8192
472:   P2-5  gpioSet bank-aware macro
473:   P1-2  moveComplete → std::atomic
474:   P1-3  Non-blocking post-cut (replaced while(state==RUNNING) with PostCutAction enum)
475: 
476: Phase B — Robustness  ✅ Done (Session 15)
477:   P2-1  USB disconnect recovery (pollUSB periodic test, reset state on failure)
478:   P2-3  WebSocket data length passed to callback
479:   P2-4  Non-blocking beep (timestamp state machine)
480: 
481: Phase C — Quick wins  ✅ Done (Session 15)
482:   P3-4  Settings NVS persistence
483:   P5-6  $status command
484:   P5-4  HPGL SC/IP commands
485:   P4-2  Magic number constants
486: 
487: Phase D — Performance  ✅ Done (Session 15)
488:   P3-1  evalVelocity() closed-form
489:   P3-2  FSInfo free-cluster hint
490:   P3-3  Bounding box during SVG conversion
491: 
492: Phase E — Architecture (2 sessions)  ❌ Not started
493:   P4-1  Split main.cpp
494: 
495: Phase F — Features  ✅ Done (Session 15)
496:   P5-1  USB directory navigation
497:   P5-3  SVG primitives
498: ```
499: 
500: ---
501: 
502: ## Verification Plan
503: 
504: After each phase, run:
505: ```bash
506: cd /Users/mariacabrera/Documents/TestVScode/C_Cut_E-plotter
507: ~/.platformio/penv/bin/pio run
508: ```
509: 
510: Target build budget: **RAM ≤ 60 KB** (currently 52 KB / 16.0%), **Flash ≤ 1 MB** (currently 878 KB / 26.3%).
511: 
512: Hardware tests after Phase 0 (drag knife):
513: - Cut a 50×50 mm square — all 4 corners should be clean right angles, not torn
514: - Cut a 5-pointed star — inner 144° corners should be crisp
515: - Cut a circle — no lift events, smooth continuous arc
516: - Compare with `KNIFE_COMPENSATION_ENABLE 0` — tearing should be visible, confirming the feature works
517: - Tune `KNIFE_OFFSET_MM` (try 0.5, 0.75, 1.0) until corners are sharpest
518: 
519: Hardware tests after Phase A–B:
520: - Home X, jog blade with arrow keys, verify position display
521: - Load a test SVG with smooth curves (e.g. a circle with `S` path commands) — verify no kinks at smooth-join vertices
522: - Hot-unplug USB during playback — verify graceful stop with error message
523: - Run error beep — verify USB remains connected throughout

(End of file - total 523 lines)