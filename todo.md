# Session 17 — Priority Todo

> Generated: 2026-06-05  
> Codebase: Sessions 1–16 (firmware v1.0.0, build 2)

---

## 🔴 P1 — Must Have

### 1.0 · Implement stub/placeholder key actions

Four key handlers are stubs that only print to serial. Implement real
sequences for each:

**`KEY_LOADMAT`** (Load Mat — line 1090):
- Home X axis (`stepper.homeX()`)
- Move carriage to `(0, 0)` (upper-left)
- Set `plotter.bladeX = 0, plotter.bladeY = 0`
- Display "Mat loaded" on OLED
- Play confirmation beep

**`KEY_UNLOADMAT`** (Unload Mat — line 1093):
- If `state != IDLE`, ignore or stop
- Move carriage to `(X_MAX_MM/2, 0)` (center top — clear of cutting area)
- Display "Remove mat" on OLED
- Play confirmation beep

**`KEY_SETCUTAREA`** (Set Cut Area — line 1100):
- Record current virtual blade position `(plotter.bladeX, plotter.bladeY)`
- Store as `plotter.cutAreaX, plotter.cutAreaY` (or a similar
  `plotter.cutAreaOrigin` field)
- Display "Cut area set" on OLED
- Play short beep

**`KEY_LOADLAST`** (Load Last — line 1103):
- If `currentFilePath[0]` is non-empty, reload and play the file
  (same logic as `KEY_REPEATLAST` but reloads from USB/PSRAM instead
  of just replaying the buffer)
- If no file path is stored, show "No last file" on OLED

**Files:** `src/main.cpp`, `include/plotter_ui.h`

### 1.1 · Wire STOP button to GPIO
`KBD_STOP` is `-1` in config.h. The STOP key is a dedicated button outside the
matrix. Assign a GPIO, wire it, and handle in `loop()` as an immediate abort.
Currently the stop check at line 1585 reads `KBD_STOP` but the pin is -1 and
nothing is wired. **Test with a physical button.**

**Files:** `src/config.h`, `src/main.cpp`

### 1.2 · Fix HPGL bounding box for Fit to Page / Center Point
`scanBoundingBox()` only parses G0/G1 commands. HPGL files use PA/PR/PD/PU.
Add an HPGL-aware bounding box scan (walk PSRAM buffer, parse HPGL coordinates).

**Files:** `src/main.cpp`, `src/hpgl_parser.cpp`

### 1.3 · G-code M0/M1 pause — wire to state machine
Parser acknowledges M0/M1 but doesn't pause execution. Add a `PAUSED` state
trigger from M0 and resume on serial `$resume` or keyboard CUT.

**Files:** `src/gcode_parser.cpp`, `src/main.cpp`

---

## 🟠 P2 — Should Have

### 2.1 · WiFi station mode fallback
Currently AP-only (`WIFI_AP_MODE 1`). Add option to connect to an existing
WiFi network (station mode). Save mode selection in NVS. Fallback to AP if
station connection fails.

**Files:** `src/config.h`, `src/wifi_server.cpp`, `src/menu.cpp`

### 2.2 · Size dial applied to output
Size dial value is displayed but never used. Should scale character output
(just like zoom scales SVG). Wire into `applyMoveTransform()` or add a
size-based scaling factor to G-code playback.

**Files:** `src/main.cpp`, `src/config.h`

### 2.3 · SVG `<g transform>` — support scale/rotate/skew
Currently only `translate()`. Real-world SVGs use `scale()`, `rotate()`,
and `matrix()`. Parse these and apply to all child elements.

**Files:** `src/svg_parser.cpp`, `src/svg_parser.h`

### 2.4 · Split `main.cpp` (~1728 lines) into focused modules
Extract into separate `.h/.cpp` files:

| New File | Content |
|----------|---------|
| `src/plotter_ctrl.cpp/h` | `startCut()`, `setupModeTransforms()`, `applyMoveTransform()`, `scanBoundingBox()`, `buildGcodePath()`, `plotSVG()`, `onMenuPlot()` |
| `src/calibration.cpp/h` | `loadCalibration()`, `saveCalSpeed()`, `saveCalPressure()`, `resetCalibration()`, `snapToDetent()`, `adcToLevel()` |
| `src/pot_reader.cpp/h` | `updatePressure()`, `updateSpeed()`, `updateEncoder()` |
| `src/serial_cmd.cpp/h` | `handleSerial()`, `processLine()`, `handleUpload()`, `onWiFiCmd()`, `handleMenuCmd()` |
| `src/ota_update.cpp/h` | `performFirmwareUpdate()`, `onFwUpdate()` |

---

## 🟡 P3 — Nice to Have

### 3.1 · G-code motion planner buffering
`PLANNER_BUFFER` (16) is defined but unused. AccelStepper was replaced with
custom S-curve code that also only buffers one move. Implement a look-ahead
planner to queue moves and optimise corner speed.

**Files:** `src/stepper.cpp`, `src/main.cpp`

### 3.2 · Paper Saver layout optimisation
Toggle is tracked but no optimisations applied. Piggyback adjacent shapes
to minimise material waste.

**Files:** `src/main.cpp`

### 3.3 · Mix 'n Match file alternation
Toggle is tracked but alternation between two loaded files not implemented.
Require two files loaded in PSRAM, alternate per cut.

**Files:** `src/main.cpp`, `src/menu.cpp`

### 3.4 · Encrypt WiFi password in NVS
Plaintext in NVS. Use `nvs_flash` encryption or at minimum obfuscate.

**File:** `src/main.cpp`

---

## 🔵 P4 — Polish / Hardening

### 4.1 · Hardware bring-up checklist
- [ ] Verify keyboard matrix scanning (all 120+ keys)
- [ ] Test endstop homing with timeout
- [ ] Tune `STEP_PER_MM` for belt/pulley/microstepping
- [ ] Verify solenoid PWM (5 kHz, 8-bit, MOSFET)
- [ ] Test buzzer beep patterns
- [ ] Test USB flash drive enumeration + FAT32 reads
- [ ] Test HPGL with Inkscape output
- [ ] Test SVG with smooth curves (`S`/`T` commands)
- [ ] Test G2/G3 arcs
- [ ] Test knife compensation (square → clean corners)
- [ ] Test quad encoder zoom adjustment (0.1x–4.0x)
- [ ] Test pot calibration wizard
- [ ] Test OTA firmware update from USB

### 4.2 · Non-blocking firmware update
`performFirmwareUpdate()` blocks Core 1. Stream in the background and update
OLED from the loop.

**File:** `src/main.cpp`

---

## Build Verification

After each change, run:
```bash
~/.platformio/penv/bin/pio run
```

Target: RAM ≤ 60 KB, Flash ≤ 1 MB.
