# ESP32 GCode Plotter — Improvement Roadmap

## Phase 1 — Quick Wins (parallel, ~1 session)

These are independent changes in different sections of `main.cpp` and `config.h`. No cross-dependencies.

### Work Unit 1.1: Wire STOP Button to GPIO

| Field | Value |
|-------|-------|
| **Size** | Trivial |
| **Dependencies** | None |
| **Parallel with** | 1.3, 2.2 |
| **Roles** | coder → builder → git |
| **Risk** | Physical button must be wired; can only verify on hardware |

**Changes:**
1. `src/config.h` — Change `#define KBD_STOP -1` to a real GPIO (e.g., `GPIO 34` is endstop-only, so pick an unused pin — confirm with user which GPIO is available)
2. `src/main.cpp` — The existing STOP check at ~line 1585 already calls `kbd.stopPressed()` which reads `KBD_STOP`. Once the pin is assigned, this code works as-is (keyboard.h reads the dedicated pin separately from the matrix)
3. `src/keyboard.h/cpp` — Verify `stopPressed()` correctly reads the GPIO as a simple digital input

**Acceptance criteria:**
- Build passes
- `KBD_STOP` is a valid GPIO number
- STOP button immediately aborts RUNNING/PLAYING_SD/PAUSED states

### Work Unit 1.3: G-code M0/M1 Pause → State Machine

| Field | Value |
|-------|-------|
| **Size** | Small |
| **Dependencies** | None |
| **Parallel with** | 1.1, 2.2 |
| **Roles** | coder → builder → git |
| **Risk** | Low — isolated to parser + state machine |

**Changes:**
1. `src/gcode_parser.cpp` — M0/M1 currently sets `_x = _y = 0` and returns true. Add a `pauseRequested` flag or call a callback:
   ```cpp
   case 0: // program pause
   case 1: // optional stop
       if (onPause) onPause();  // new callback
       return true;
   ```
2. `src/gcode_parser.h` — Add `void (*onPause)() = nullptr;` callback member
3. `src/main.cpp` — Set `gcodeParser.onPause = [](){ state.store(PAUSED); beep(...); };` during init. In the PLAYING_SD state handler, when `state == PAUSED`, stop advancing `filePlayOffset` and wait for resume
4. Resume trigger: serial `$resume` command or CUT key on keyboard

**Acceptance criteria:**
- M0 in a G-code file pauses playback (OLED shows PAUSED, blade lifts)
- `$resume` or CUT key resumes from the next line
- M2 (program end) still works as before

### Work Unit 2.2: Size Dial Applied to Output

| Field | Value |
|-------|-------|
| **Size** | Small |
| **Dependencies** | None |
| **Parallel with** | 1.1, 1.3 |
| **Roles** | coder → builder → git |
| **Risk** | Low — single transform addition |

**Changes:**
1. `src/main.cpp` in `applyMoveTransform()` — The size dial value is stored in `plotter.sizeDial` (or equivalent — need to verify the exact field name). Add a scaling step:
   ```cpp
   // After existing transforms, apply size dial scaling
   float sizeScale = plotter.sizeDial / BASE_SIZE;  // e.g., 1.0" = 1.0x
   x *= sizeScale;
   y *= sizeScale;
   ```
2. Verify where `plotter.sizeDial` is set (likely from the quadrature encoder or a dedicated pot). If it's currently mapped to zoom, decide if size dial and zoom are independent or the same thing.

**Note:** The AGENTS.md says "Size dial (mag pot repurposed)" and "The Size Dial value is displayed but not yet applied to the output (scaling of characters)." Need to verify the exact data flow.

**Acceptance criteria:**
- Changing the size dial value visibly scales the cut output
- Size dial and zoom (if separate) don't interfere

### Work Unit 1.0: Implement Stub Key Actions

| Field | Value |
|-------|-------|
| **Size** | Small |
| **Dependencies** | 1.1 (STOP button for context on GPIO assignments) |
| **Parallel with** | 1.2, 2.3, 2.1 (after Phase 1.1 completes) |
| **Roles** | coder → builder → git |
| **Risk** | Low — 4 discrete key handlers |

**Changes in `src/main.cpp` (lines 1085–1110):**

**KEY_LOADMAT:**
```cpp
case KEY_LOADMAT:
    if (state == IDLE) {
        stepper.homeX();
        stepper.setTarget(0, 0, plotter.currentFeed);
        plotter.bladeX = 0;
        plotter.bladeY = 0;
        beep(BEEP_FREQ, BEEP_SHORT_MS);
        // Display "Mat loaded" briefly
    }
    break;
```

**KEY_UNLOADMAT:**
```cpp
case KEY_UNLOADMAT:
    if (state == IDLE) {
        stepper.setTarget(X_MAX_MM / 2, 0, plotter.currentFeed);
        beep(BEEP_FREQ, BEEP_SHORT_MS);
        // Display "Remove mat"
    }
    break;
```

**KEY_SETCUTAREA:**
```cpp
case KEY_SETCUTAREA:
    plotter.cutAreaX = plotter.bladeX;
    plotter.cutAreaY = plotter.bladeY;
    beep(BEEP_FREQ, BEEP_SHORT_MS);
    // Display "Cut area set"
    break;
```

**KEY_LOADLAST:**
```cpp
case KEY_LOADLAST:
    if (currentFilePath[0] && (usbDrive.isReady() || psramBuf.isReady())) {
        if (usbDrive.isReady()) {
            usbDrive.loadFile(currentFilePath, psramBuf);
        }
        startCut();
    } else {
        // Display "No last file"
        beep(BEEP_FREQ_ERR, BEEP_SHORT_MS);
    }
    break;
```

**Prerequisites in `plotter_ui.h`:**
- Add `cutAreaX`, `cutAreaY` fields to `PlotterState` if not present

**Acceptance criteria:**
- Each key performs its described action
- No-op when preconditions aren't met (e.g., LOADMAT when already running)

## Phase 2 — Medium Tasks (parallel across files, ~1 session)

These touch different source files and can run concurrently.

### Work Unit 1.2: Fix HPGL Bounding Box

| Field | Value |
|-------|-------|
| **Size** | Medium |
| **Dependencies** | None |
| **Parallel with** | 2.3, 2.1 |
| **Roles** | explore → coder → builder → verifier → git |
| **Risk** | Medium — HPGL coordinate parsing is tricky |

**Problem:** `scanBoundingBox()` (main.cpp:468-503) only parses `G0`/`G1` lines. HPGL files use `PA`/`PR`/`PD`/`PU` commands with different coordinate syntax.

**Changes:**
1. `src/hpgl_parser.cpp` — Add a `scanBoundingBox()` function or expose coordinate state:
   - Walk the PSRAM buffer line by line
   - Parse `PA x,y;` commands (absolute) — extract x,y coordinates
   - Parse `PR dx,dy;` commands (relative) — accumulate deltas
   - Track min/max X,Y throughout
   - Handle `IN;` (initialize → reset position to 0,0)
   - Handle `SC`/`IP` scaling if present

2. `src/hpgl_parser.h` — Declare:
   ```cpp
   struct HPGLBBox { float minX, minY, maxX, maxY; bool valid; };
   HPGLBBox hpglScanBBox(const uint8_t* buf, size_t len);
   ```

3. `src/main.cpp` in `scanBoundingBox()` — After the G-code scan, check if the buffer starts with an HPGL command (detect via `^[A-Z]{2}` prefix on first non-empty line):
   ```cpp
   // After existing G-code scan...
   if (!modeXform.bbValid) {
       // Try HPGL scan
       HPGLBBox hb = hpglScanBBox(psramBuf.data(), psramBuf.size());
       if (hb.valid) {
           modeXform.bbMinX = hb.minX; modeXform.bbMaxX = hb.maxX;
           modeXform.bbMinY = hb.minY; modeXform.bbMaxY = hb.maxY;
           modeXform.bbValid = true;
       }
   }
   ```

**Acceptance criteria:**
- Fit to Page / Center Point work correctly with HPGL files
- G-code files still work (no regression)

### Work Unit 2.3: SVG `<g transform>` — Scale/Rotate/Skew

| Field | Value |
|-------|-------|
| **Size** | Medium |
| **Dependencies** | None |
| **Parallel with** | 1.2, 2.1 |
| **Roles** | explore → coder → builder → git |
| **Risk** | Medium — SVG transform matrix math |

**Current state:** Only `translate()` is parsed in `<g transform="translate(x,y)">`.

**Changes in `src/svg_parser.cpp`:**
1. Extend the transform parser to handle:
   - `scale(sx, sy)` or `scale(s)`
   - `rotate(angle)` or `rotate(angle, cx, cy)`
   - `skewX(angle)`, `skewY(angle)`
   - `matrix(a, b, c, d, e, f)`
   - Multiple transforms: `translate(10,20) rotate(45)`

2. Implement a 2D affine transform matrix (3×3, stored as 6 floats):
   ```cpp
   struct Transform {
       float a, b, c, d, e, f;  // [a c e; b d f; 0 0 1]
       void translate(float tx, float ty);
       void scale(float sx, float sy);
       void rotate(float angleDeg, float cx, float cy);
       void skewX(float angleDeg);
       void skewY(float angleDeg);
       void apply(float &x, float &y) const;
   };
   ```

3. Maintain a transform stack (vector of `Transform`). On `<g transform="...">`, push the new combined transform. On `</g>`, pop.

4. Apply the current top-of-stack transform to all child element coordinates before passing to `doMove`/`doLine`/`doCubic`.

**Acceptance criteria:**
- SVGs with `scale()`, `rotate()`, `matrix()` transforms render correctly
- Nested `<g>` elements compose transforms correctly
- Existing translate-only SVGs still work

### Work Unit 2.1: WiFi Station Mode Fallback

| Field | Value |
|-------|-------|
| **Size** | Medium |
| **Dependencies** | None |
| **Parallel with** | 1.2, 2.3 |
| **Roles** | explore → coder → builder → git |
| **Risk** | Medium — network state management, NVS persistence |

**Changes:**

1. `src/config.h` — Add:
   ```cpp
   #define WIFI_MODE_AP       0
   #define WIFI_MODE_STA      1
   #define WIFI_DEFAULT_MODE  WIFI_MODE_AP
   #define WIFI_STA_SSID      ""  // default empty
   #define WIFI_STA_PASS      ""
   #define WIFI_CONNECT_TIMEOUT_MS  10000
   ```

2. `src/wifi_server.h` — Add mode tracking:
   ```cpp
   enum WiFiMode { WIFI_AP, WIFI_STA };
   void begin(WIFIMode mode, const char* staSSID = "", const char* staPass = "");
   WiFiMode currentMode() const;
   ```

3. `src/wifi_server.cpp` — Implement dual-mode:
   ```cpp
   void WiFiServer::begin(WiFiMode mode, ...) {
       if (mode == WIFI_STA) {
           WiFi.mode(WIFI_STA);
           WiFi.begin(staSSID, staPass);
           unsigned long start = millis();
           while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
               delay(100);
           }
           if (WiFi.status() != WL_CONNECTED) {
               // Fallback to AP
               mode = WIFI_AP;
           }
       }
       if (mode == WIFI_AP) {
           WiFi.mode(WIFI_AP);
           WiFi.softAP(wifiSSID, wifiPass);
       }
       // Start AsyncWebServer...
   }
   ```

4. `src/menu.cpp` — Add WiFi Mode selection in Settings (AP / Station). When Station selected, show SSID/Password editor (reuse existing WiFi editor).

5. NVS persistence — `saveWiFi()` already exists. Extend to save WiFi mode. `loadWiFi()` loads mode + credentials.

**Acceptance criteria:**
- AP mode works as before (default)
- Station mode connects to specified network
- Falls back to AP if station connection fails
- Mode + credentials persist across reboots

## Phase 3 — Major Refactor: Split main.cpp (1–2 sessions)

**This is the highest-risk change.** All other work must complete first because every task touches `main.cpp`.

### Work Unit 2.4: Split main.cpp into Focused Modules

| Field | Value |
|-------|-------|
| **Size** | Large |
| **Dependencies** | 1.0, 1.1, 1.2, 1.3, 2.1, 2.2, 2.3 — ALL prior work |
| **Parallel with** | Nothing (serial, one coder) |
| **Roles** | explore → coder → builder → verifier → git-agent → docs |
| **Risk** | **High** — 1,593-line file split across 5 new modules; cross-module dependencies; dual-core globals |

**Target structure:**

| New File | Functions to Extract | ~Lines |
|----------|---------------------|--------|
| `src/plotter_ctrl.h/cpp` | `startCut()`, `setupModeTransforms()`, `applyMoveTransform()`, `scanBoundingBox()`, `buildGcodePath()`, `plotSVG()`, `onMenuPlot()`, `modeXform` struct | ~250 |
| `src/calibration.h/cpp` | `loadCalibration()`, `saveCalSpeed()`, `saveCalPressure()`, `resetCalibration()`, `snapToDetent()`, `adcToLevel()`, calibration arrays | ~150 |
| `src/pot_reader.h/cpp` | `updatePressure()`, `updateSpeed()`, `updateEncoder()`, pot state variables | ~120 |
| `src/serial_cmd.h/cpp` | `handleSerial()`, `processLine()`, `handleUpload()`, `onWiFiCmd()`, `handleMenuCmd()`, `$status` handler | ~200 |
| `src/ota_update.h/cpp` | `performFirmwareUpdate()`, `onFwUpdate()`, `.bin` detection logic | ~120 |
| **`src/main.cpp` (remainder)** | `setup()`, `loop()`, `motionTask()`, `onMove()`, `onHome()`, `onSolenoid()`, state machine, keyboard/button handling | ~750 |

**Execution plan:**

1. **Explore phase** — Map every function's dependencies (which globals it reads/writes, which other functions it calls). Build a dependency graph.

2. **Extract one module at a time** (serial order to avoid conflicts):
   - Step A: Extract `calibration.h/cpp` (fewest external dependencies)
   - Step B: Extract `pot_reader.h/cpp` (depends on calibration)
   - Step C: Extract `serial_cmd.h/cpp` (depends on many things but is a leaf caller)
   - Step D: Extract `ota_update.h/cpp` (self-contained)
   - Step E: Extract `plotter_ctrl.h/cpp` (depends on everything, do last)

3. **After each extraction:**
   - Build → must compile clean
   - Verify no behavioral change (same build size ±1%)

4. **Global state management:**
   - Shared state (`state`, `plotter`, `solenoidOn`, etc.) stays in `main.cpp` as `extern` declarations
   - Each new module declares `extern` for what it needs
   - No new global variables — restructure to pass context where possible

**Acceptance criteria:**
- `main.cpp` is ≤ 800 lines
- Each new module is ≤ 250 lines
- Build passes with identical RAM/Flash usage (±1%)
- All existing functionality works unchanged
- No new globals introduced

## Phase 4 — Future / Deferred

These are lower priority and can be planned separately.

### 3.1: G-code Motion Planner Buffering (Large)
- Implement look-ahead planner with 16-move buffer
- Optimize corner speeds between consecutive moves
- High complexity, needs careful testing with real hardware

### 3.2: Paper Saver Layout (Medium)
- Analyze bounding boxes of shapes in PSRAM
- Arrange shapes to minimize material waste
- Requires shape extraction and re-ordering logic

### 3.3: Mix 'n Match (Medium)
- Load two files, alternate cuts between them
- Requires dual-file PSRAM management or streaming

### 3.4: NVS WiFi Encryption (Small)
- Use ESP-IDF `nvs_flash` encryption or `Preferences` encryption
- Low priority but good security practice

### 4.2: Non-blocking Firmware Update (Medium)
- Move `performFirmwareUpdate()` to a FreeRTOS task
- Update OLED from the main loop instead of blocking

## Dependency Graph (All Phases)

```
Phase 1 (parallel)                Phase 2 (parallel)           Phase 3
┌─────────────────┐              ┌──────────────────┐         ┌──────────────┐
│ 1.1 STOP button │              │ 1.2 HPGL bbox    │         │              │
│ 1.3 M0/M1 pause │──────────────│ 2.3 SVG xforms   │────────▶│ 2.4 Split    │
│ 2.2 Size dial   │              │ 2.1 WiFi STA     │         │    main.cpp  │
└────────┬────────┘              └────────┬─────────┘         └──────────────┘
         │                                │
         ▼                                │
┌─────────────────┐                       │
│ 1.0 Stub keys   │───────────────────────┘
└─────────────────┘
```

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| STOP button GPIO conflicts with existing pin | Medium | Verify against pin table before assigning |
| M0/M1 pause blocks motion on Core 0 | Low | State change is atomic; motion task sees IDLE/PAUSED and stops |
| Size dial scaling conflicts with zoom | Medium | Clarify if they're independent or the same control |
| HPGL coordinate parsing edge cases | Medium | Test with real Inkscape HPGL output |
| SVG transform matrix numerical precision | Low | Float is sufficient for SVG viewBox scales |
| WiFi STA fallback timeout blocks boot | Medium | Use non-blocking connect with `WiFi.status()` polling |
| main.cpp split breaks dual-core globals | **High** | Careful `extern` management; build after each extraction |
| main.cpp split changes build size | Low | Compare RAM/Flash before/after each step |

## Estimated Effort

| Phase | Sessions | Risk |
|-------|----------|------|
| Phase 1 (1.1, 1.3, 2.2) | 0.5 | Low |
| Phase 2 (1.0, 1.2, 2.3, 2.1) | 1.0 | Medium |
| Phase 3 (2.4 split) | 1.5 | High |
| **Total** | **~3 sessions** | |