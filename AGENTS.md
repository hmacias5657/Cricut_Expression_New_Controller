# Session Context — ESP32 GCode Plotter

Last updated: 2026-06-05 (Session 16)

## Goal
Build an ESP32-S3-driven G-code/HPGL/SVG pen plotter with Plotter keyboard, OLED display, dual-core operation, SVG parsing, USB flash drive storage, and PSRAM buffering.

## Completed Features

### Core
- [x] PlatformIO project (espressif32, Arduino framework, ESP32-S3, 8MB flash + 8MB PSRAM)
- [x] Stepper XY control with AccelStepper (A4988/DRV8825, configurable steps/mm)
- [x] Single endstop homing (homes X, backoff, tracks position)
- [x] G-code parser — G0/G1, G4, G28, G90/G91, M3/M4/M5 (with S pressure value), M92 (set position), M114
- [x] PWM solenoid pen lifter (5 kHz, 8-bit, LEDC, core-independent)
- [x] Speed potentiometer (GPIO 5, EMA smoothing, 5 detent levels → feed rate 500–3000 mm/min)
- [x] Pressure potentiometer (EMA smoothing, 5 detent levels → 0–100% solenoid duty)
- [x] Potentiometer calibration wizard (Settings → Calibrate, NVS persisted, ±5% detent snapping)
- [x] Magnification defaults to 1.0x on startup, adjustable via quadrature encoder (GPIO 18/23)
- [x] Dual-core: motion on Core 0 (`AccelStepper.run()` loop), UI on Core 1

### Communication
- [x] Serial at 115200 baud
- [x] WiFi AP mode with AsyncWebServer + WebSocket control page
- [x] File upload via WiFi `$upload` (stored in PSRAM buffer)
- [x] USB flash drive file listing and streaming playback (pause/resume/stop)

### Display & Menu
- [x] OLED SSD1322 (NHD-2.7-12864, 128x64, SPI) via U8G2
- [x] Status view: state, X/Y position, pressure %, zoom factor
- [x] Plotter-style menu system (Browse USB, Settings, About)
- [x] Plotter keyboard driver (24×5 shift-register matrix scan)
- [x] Menu navigation via physical buttons, Plotter keyboard, or serial `$menu`

### File Formats
- [x] SVG parser — M/L/H/V/C/S/Q/T/Z commands, bezier approximation, viewBox scaling, 2 MB max
- [x] HPGL parser — IN, PU, PD, PA, PR, SP, LT; auto-detected; pen-to-pressure mapping
- [x] SVG → G-code on-device conversion with zoom from mag pot

### Plotter UI Emulation (added Session 4)
- [x] Full Plotter UI spec encoded in `include/plotter_ui.h`
- [x] 7 cutting modes: Landscape, Portrait, Mix 'n Match, Quantity, Fit to Page, Fit to Length, Auto Fill
- [x] 5 functions: Multi Cut (2/3/4 passes), Center Point, Line Return, Flip, Paper Saver
- [x] 7 settings: Language (EN/FR/ES/DE), Units (4 types), Multi Cut defaults, Mat Size (12x12/12x24), Char Images toggle, WiFi SSID/Password editor, Calibrate (Speed/Pressure wizard)
- [x] Calibration wizard: step through 5 detents per pot, read ADC on OK press, save to NVS with ±5% snap radius, "Reset Cal" option
- [x] Plotter-style 5-line OLED status view: mode bar, position/size, speed/pressure bars, function toggles, status
- [x] Comprehensive key handler for all 120+ matrix keys (blade nav, shift, text entry, modes, functions)
- [x] Multi-cut auto-replay (reopens file for each pass)
- [x] Size dial (mag pot repurposed), speed/pressure bar display
- [x] Buzzer/beep with sound on/off toggle
- [x] **WiFi SSID/Password editor in Settings menu** (Session 7) — text entry via Plotter keyboard, saved to NVS, applied live

### Hardware Support
- [x] Plotter keyboard (shift-register scanning, 10+ pin ribbon cable)
- [x] Quadrature encoder on GPIO 18/23 for zoom adjustment, defaults to 1.0x on startup
- [x] USB flash drive via ESP32-S3 native USB OTG (GPIO 19/20, Full Speed 12 Mbps)
- [x] PSRAM file buffer (4 MB pool, file overflow error)
- [x] I2C IO expander (MCP23017) for extra GPIO when needed (config.h toggle)

### Storage (added Session 5)
- [x] USB flash drive replaces SD card entirely
- [x] `usb_drive.h/cpp` — FAT32 reader via ESP-IDF USB host library + MSC Bulk-Only Transport
- [x] `psram_buffer.h/cpp` — PSRAM file buffer (ps_malloc, 4 MB pool, write/read/clear/error)
- [x] Files larger than PSRAM buffer show OLED error + beep pattern
- [x] GPIO 19 (DN) and GPIO 20 (DP) fixed for USB OTG (no configurable pins)

### Firmware Update (added Session 14)
- [x] Streaming file read API (`openFile/readFile/closeFile`) for reading .bin without loading to PSRAM
- [x] OTA firmware update from USB pendrive using Arduino `Update` class
- [x] `.bin` file detection in file browser → confirmation dialog
- [x] OLED progress bar + percentage during flashing
- [x] Validation via `Update.end()` before committing; abort on failure
- [x] Automatic reboot on success

### SVG G-code Caching (added Session 6)
- [x] SVG → G-code conversion outputs to PSRAM buffer (no longer just Serial)
- [x] Multi-cut SVG plots save G-code to USB `/GCODE/` directory for replay
- [x] Filename includes magnification factor (e.g. `IMAGE_M10.GCO`) — different zoom = different file
- [x] Only saves if file doesn't already exist (saves USB erase cycles)
- [x] `usb_drive.cpp` FAT32 write support: SCSI WRITE10, cluster allocation, directory creation, file write
- [x] Subdirectory path resolution for `loadFile()`, `exists()`, `writeFile()`, `makeDir()`

## Key Architectural Decisions

### Dual-Core Split
- **Core 0**: Dedicated `motionTask` runs `AccelStepper.run()` continuously with `vTaskDelay(1)` for jitter-free step pulses
- **Core 1**: Arduino `loop()` handles serial, WiFi, display, menu, SVG parsing, pot reading, USB host
- **Bridge**: `moveComplete` flag set by Core 0, read and cleared by Core 1

### Solenoid PWM
- LEDC channel (hardware PWM) on GPIO 32, independent of core scheduling
- Pressure priority: `gcode S-value > potentiometer` (pot takes over when no S-value set)

### HPGL Auto-Detection
- `processLine()` checks for `^[A-Z]{2}` prefix to route HPGL vs G-code
- `$hpgl` and `$gcode` force explicit mode

### Native USB Host (no MAX3421E)
- ESP32-S3 USB OTG controller on GPIO 19/20 (fixed), accessed via `#include <usb/usb_host.h>`
- MSC Bulk-Only Transport protocol implemented from scratch: CBW/CSW, SCSI READ10/READ_CAPACITY
- TinyUSB MSC host header (`msc_host.h`) available but not used; ESP-IDF `usb_host` for full control
- Transfer buffer (512 bytes) allocated once, reused for CBW→Data→CSW phases
- Synchronous wrapper on async event-driven API: `submitAndWait()` polls `usb_host_client_handle_events()`

### PSRAM File Buffer
- Files loaded entirely into PSRAM before parsing (no streaming File objects)
- `ps_malloc()` for allocation on ESP32-S3 with `BOARD_HAS_PSRAM`
- 4 MB pool reserved; `buf.error()` flag set if file exceeds pool

### Pin Sharing
Several GPIOs are shared between features to fit the ESP32's limited pins. See config.h comments.

## Current Pin Assignment

| GPIO | Function | Notes |
|------|----------|-------|
| 12 | X_STEP | |
| 13 | X_DIR | |
| 14 | Y_STEP | |
| 27 | Y_DIR | |
| 26 | ENABLE | Active low, both drivers |
| 34 | ENDSTOP | |
| 32 | SOLENOID | LEDC PWM output |
| 35 | POT | Pressure ADC |
| 5  | SPEED_PIN | Speed potentiometer ADC |
| 19 | USB_DN | USB OTG D- (fixed, no config) |
| 20 | USB_DP | USB OTG D+ (fixed, no config) |
| 15 | OLED_CS | SPI CS |
| 2 | OLED_DC | Data/Command |
| 4 | OLED_RST / KBD_ROW4 | Saved/restored per scan |
| 16 | OLED_MOSI | SPI MOSI |
| 17 | OLED_SCK | SPI SCK |
| 33 | KBD_CLK / BTN_UP | Output when KBD enabled |
| 25 | KBD_DATA / BTN_DOWN | Output when KBD enabled |
| 22 | KBD_ROW2 / BTN_SELECT | Input when KBD enabled |
| 21 | KBD_ROW3 / BTN_BACK | Input when KBD enabled |
| 36 | KBD_ROW0 | ADC1 input-only |
| 39 | KBD_ROW1 | ADC1 input-only |
| 18 | ENC_A | Quadrature encoder A |
| 23 | ENC_B | Quadrature encoder B |

## Known Issues / Gotchas

1. **GPIO 34** is dedicated to ENDSTOP only (magnification pot removed, no sharing).
2. **GPIO 4 (OLED_RST / KBD_ROW4)**: The keyboard scanning function saves the pin state, configures it as input, reads, then restores it as output. If this causes OLED glitches, set `KBD_ROW4 -1` in config.h.
3. **U8G2 constructor**: Must use `U8G2_SSD1322_NHD_128X64_F_4W_SW_SPI` — the `_2X` variant and `_BW` variants don't exist for this display.
4. **WIFI_AP macro renamed** to `WIFI_AP_MODE` to avoid conflict with WiFi library.
5. **USB OTG Full Speed only**: ESP32-S3 USB OTG supports only Full Speed (12 Mbps) and Low Speed. High Speed (480 Mbps) flash drives may not work. Use older/smaller drives.
6. **PSRAM on N8R8**: The `esp32-s3-devkitc-1` PlatformIO board defaults to "No PSRAM"; enable with `board_build.psram = enable` + `-DBOARD_HAS_PSRAM` in build_flags.
7. **USB host transfers**: The MSC BOT driver blocks the calling task during sector reads. FAT32 cluster walking may take 10s–100s of ms for large directories over Full Speed USB.

## Verification
- Build with: `~/.platformio/penv/bin/pio run` from the project root
- Build passes with RAM ~16% (51 KB SRAM), Flash ~26% (878 KB)
- PSRAM (8 MB) available via `ps_malloc()` for file buffers
- PlatformIO CLI: `espressif32@7.0.1`, `framework-arduinoespressif32@3.20017.241212`
- Build output: `firmware_v{VERSION}_b{BUILD}.bin` in `.pio/build/esp32s3dev/`

## File Map

| File | Purpose |
|------|---------|
| `platformio.ini` | Board (esp32-s3-devkitc-1), deps (AccelStepper, ESPAsyncWebServer, U8g2), PSRAM |
| `src/config.h` | All pin definitions, motion params, feature toggles, I2C expander |
| `src/main.cpp` | Entry point, state machine, dual-core, all integrations |
| `src/stepper.h/cpp` | AccelStepper XY + homing |
| `src/gcode_parser.h/cpp` | G-code interpreter |
| `src/hpgl_parser.h/cpp` | HPGL interpreter (Inkscape compatible) |
| `src/usb_drive.h/cpp` | Native USB host MSC driver + FAT32 reader (replaces sd_card) |
| `src/psram_buffer.h/cpp` | PSRAM file buffer (4 MB pool, overflow error) |
| `src/wifi_server.h/cpp` | Async web server + WebSocket UI |
| `src/display.h/cpp` | SSD1322 OLED (U8G2), Plotter status + error views |
| `src/menu.h/cpp` | Plotter-style menu (Browse USB, Settings, About) |
| `src/svg_parser.h/cpp` | SVG path → G-code converter with zoom |
| `src/keyboard.h/cpp` | Plotter shift-register matrix scanning |
| `include/plotter_ui.h` | Plotter UI spec constants, enums, PlotterState struct |
| `scripts/versioning.py` | PlatformIO extra script: auto build number + versioned bin output |
| `version.txt` | SemVer and build counter (auto-managed) |
| `CHANGELOG.md` | Release history per Keep a Changelog |
| `AGENTS.md` | Session context, architecture notes, open issues |
| `README.md` | Full documentation |

## Common Next Steps (not yet implemented)

### Hardware
- **Test with actual hardware** — verify keyboard scanning, endstop homing, solenoid PWM, buzzer
- **Tune `STEP_PER_MM`** — depends on belt pitch, pulley teeth, microstepping (TMC2209 with 0.9° motors)
- **Wire STOP button** — set KBD_STOP to a dedicated GPIO or identify STOP in the matrix
- **Test HPGL with Inkscape** — verify coordinate scaling and pen pressure mapping
- **Test USB flash drive** — verify MSC enumeration, FAT32 reads on real hardware

### Plotter Mode Transforms
- **Paper Saver** — apply material-saving layout optimization (piggyback cuts)

### Polish
- **WiFi fallback to station mode** — currently AP-only
- **G-code planner** — the `PLANNER_BUFFER` is defined but only one move is buffered (AccelStepper has its own internal buffer)

## Session Log

### Session 16 — Automatic Build Versioning
- Added `#define FIRMWARE_VERSION "1.0.0"` to `src/config.h` — SemVer version string
- Added `#define FIRMWARE_BUILD __BUILD_NUMBER__` to `src/config.h` — auto-incremented build number injected via build flag
- Created `scripts/versioning.py` — PlatformIO extra script that:
  - Pre-build: reads `version.txt` (contains `semver buildnum`), increments build number, writes it back, injects `-D__BUILD_NUMBER__=<n>` into build flags
  - Post-build: copies `firmware.bin` → `firmware_v{semver}_b{build}.bin` in the build directory
- Created `version.txt` — initial state: `1.0.0 1`
- Updated `platformio.ini` — added `extra_scripts = scripts/versioning.py`
- Updated About screen in `display.cpp` — shows `v1.0.0 (build 1)`
- Updated serial welcome in `main.cpp` — prints version and build number
- Created `CHANGELOG.md` — extracted from all session logs in AGENTS.md; follows Keep a Changelog + SemVer. Includes planned v1.1.0 items for future.
- Added `CHANGELOG.md` to file map below
- Build verified: RAM 16.0%, Flash 26.3%

### Session 14 — USB Pendrive Firmware Update
- Added streaming file read API to `usb_drive.h/cpp` (`openFile()`, `readFile()`, `closeFile()`, `currentFileSize()`) — walks the FAT32 cluster chain incrementally instead of loading the entire file into PSRAM
- Added `MENU_FW_CONFIRM` and `MENU_FW_PROGRESS` pages to menu system
- When a `.bin` file is selected in the USB file browser, the menu shows a confirmation prompt ("Update firmware from this file?")
- On confirm, calls `performFirmwareUpdate()` which:
  - Stops motion and sets state to IDLE
  - Opens the file via the streaming USB API (no PSRAM used)
  - Calls `Update.begin(fwSize)` on the inactive OTA partition
  - Streams 512-byte chunks from USB → `Update.write()` with OLED progress bar and percentage
  - Calls `pollUSB()` between chunks to keep USB host alive
  - On success: calls `Update.end()` for CRC validation → displays "Success! Rebooting..." → `ESP.restart()`
  - On failure: aborts the update (`Update.abort()`) → shows error on OLED → returns to menu
- Uses the existing `default_8MB.csv` partition table which provides two OTA app partitions (`app0` at 0x10000, `app1` at 0x340000, each 3.2 MB)
- Display: `showFwUpdate(msg, percent)` draws progress bar with percentage; `isFwUpdateMode()` takes priority in the display loop
- No extra `lib_deps` needed — `Update.h` is part of the Arduino ESP32 core
- Build verified: RAM 15.9%, Flash 25.9%

### Session 13 — S-Curve (Jerk-Limited) Motion Profile
- Replaced AccelStepper library with a custom S-curve motion planner and step generator
- Removed `accelstepper@^1.64` from `platformio.ini` dependencies
- Added `JERK` (8000 mm/s³), `MOTION_TICK_HZ` (1000), and `STEP_PULSE_US` (2) to `config.h`
- Rewrote `stepper.h` — removed `AccelStepper` dependency, added 7-phase S-curve profile parameters (`_t1-_t7`, `_s1-_s7`, `_v1-_v7`), `planProfile()`, `evalProfile()`, `pulseStepX/Y()`
- Rewrote `stepper.cpp` — full 7-phase jerk-limited S-curve planner:
  - Phase 1: jerk +J, accel 0→A (ramp up)
  - Phase 2: jerk 0, accel = A (constant accel)
  - Phase 3: jerk −J, accel A→0 (ramp down)
  - Phase 4: jerk 0, accel = 0 (cruise)
  - Phase 5: jerk −J, accel 0→−A (decel ramp)
  - Phase 6: jerk 0, accel = −A (constant decel)
  - Phase 7: jerk +J, accel −A→0 (decel taper)
- Profile planner handles:
  - Full 7-phase profile (long moves with cruise)
  - Triangular with constant accel (medium moves, no cruise)
  - Pure triangular (short moves, only jerk phases 1/3/5/7)
  - Binary search for achievable V when distance is limited
  - Fallback to trapezoidal if jerk is extremely high
- Step generation uses rate-limited pulse spacing based on S-curve instantaneous velocity, capped at `MOTION_TICK_HZ` budget per call
- Homing uses same direct GPIO control (no AccelStepper)
- Build verified: RAM 15.8%, Flash 25.7%

### Session 15 — Knife Compensation, Arc Support, Non-Blocking Ops, SVG/HPGL/FS Enhancements
- **Knife compensation**: Created `knife_comp.h/cpp` — pending-move async pivot/lower/cut sequence with angle delta threshold (`KNIFE_ANGLE_THRESHOLD_DEG=15`) and configurable offset (`KNIFE_OFFSET_MM=0.75`). Uses `KNIFE_PIVOT` state and `moveComplete` atomic for lift → pivot → lower → cut flow.
- **Atomic state**: Changed `state` and `moveComplete` from `volatile bool`/`State` to `std::atomic<State>` and `std::atomic<bool>` with `memory_order_seq_cst`; replaced `portMUX_TYPE` spinlock. Added `state.load()` cast in `onReport()` printf.
- **Non-blocking beep**: Replaced `delay()` beeps with timestamp-based state machine (`beepOffAt`/`beepNextAt`, run from `updateBeep()` in loop).
- **Non-blocking buttons**: `handleButtons()` uses timestamp debounce (`BTN_DEBOUNCE_MS`) saved per button.
- **Non-blocking post-cut**: Replaced `while(state==RUNNING)` busy-waits with `PostCutAction` enum continuation pattern.
- **Homing timeout**: Added `HOMING_MAX_STEPS` (50000) and `HOMING_MIN_STEP_US` (50) in `stepper.cpp` to prevent runaway on missing endstop.
- **gpioSet/gpioReadRaw bank-aware**: Handles GPIOs <32 vs ≥32 separately (GPIO.out1_w1ts for 32-39).
- **evalVelocity()**: O(1) analytical S-curve velocity function replacing `(evalProfile(elapsed+dt)-evalProfile(elapsed))/dt`.
- **WebSocket data length**: `wifi_server.cpp` passes `len` to `_cmdCb`. `main.cpp` `handleUpload()` and `onWiFiCmd()` use `size_t dataLen`.
- **G2/G3 arcs**: Added to `gcode_parser.cpp` — `generateArc()` with center(I,J) and radius(R) methods, segment count via `GCODE_ARC_SEGMENTS` (64), emits series of `onMove` G1 moves.
- **SVG bezier reflection fix**: `S/s`/`T/t` commands now properly reflect the previous control point (`_prevCpX/_prevCpY`) instead of wrongly reflecting the current point.
- **SVG primitive elements**: Added `<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>` parsing in `svg_parser.cpp`.
- **SVG `<g transform>`**: Basic `translate()` parser added.
- **HPGL SC/IP commands**: Added user-unit scaling (`SC x1,y1,x2,y2`) and input P1/P2 (`IP x1,y1,x2,y2`) to `hpgl_parser.cpp`.
- **USB disk state cleanup**: `pollUSB()` now periodically calls `mscTestUnitReady()` and resets `d.diskReady/fat.valid/mscClaimed/devHdl` on failure (P2-1).
- **FSInfo free-cluster hint**: Reads FSInfo sector in `initFAT()` for `freeClusHint`; `findFreeCluster()` uses hint as scan start then wraps around.
- **Directory navigation**: `menu.cpp` `enterDir()`/`leaveDir()` for navigating subdirectories in USB file browser; `enumerate()` accepts dir path.
- **Settings persistence**: `saveSettings()`/`loadSettings()` persist language, units, mat size, char images to NVS via Preferences; `setPlotterState()` auto-loads.
- **`$status` command**: Added serial command for comprehensive state dump.
- **`adcToLevel` ceiling fix**: `(raw*5+4095)/4096` instead of floor truncation.
- **`motionTask` stack**: Increased from 4096 to 8192.
- Build: RAM 16.0%, Flash 26.3%

### Session 14 — USB Pendrive Firmware Update
- Updated `X_MAX_MM` from 200.0 to **304.8** (12″ gantry width)
- Updated `Y_MAX_MM` from 200.0 to **609.6** (24″ max travel for 12×24″ mat)
- Updated USER_MANUAL.md tables and example to match

### Session 11 — Key Response Corrections from Cricut Expression Manual
- Fixed Y-axis blade navigation direction: ▲ now **decreases** Y (moves toward top Y=0),
  ▼ now **increases** Y (moves toward bottom). (0,0) = upper-left of mat.
- Fixed homing: endstop is on the **right** side at X_MAX_MM (304.8 mm). `homeX()` now
  moves **right** (positive direction) to hit endstop, sets position to `X_MAX_MM`,
  backs off left.
- Updated `onHome()` display position to `(X_MAX_MM, 0)` instead of `(0, 0)`
- Updated `plotSVG()` display position after homing to match
- Updated USER_MANUAL.md blade navigation table with correct Y direction annotations

### Session 12 — Mode/Function Coordinate Transforms (per manual)
- Implemented `applyMoveTransform()` called from `onMove()` before constraint — applies
  all active mode and function transforms to each G-code coordinate in real time.
- **Portrait**: 90° CW rotation (swap X↔Y, mirror new Y to X_MAX_MM)
- **Flip**: mirror X (`X' = X_MAX_MM - X`)
- **Fit to Page**: pre-scans G-code bounding box, scales uniformly to fill mat
- **Fit to Length**: pre-scans bounding box, scales to fit the longer mat dimension
- **Center Point**: pre-scans bounding box, offsets so blade position becomes design
  centre (Landscape-only, per manual)
- **Quantity**: replays file N times stacked vertically with 2 mm gap
- **Auto Fill**: tiles copies in a grid to fill mat (max 300, per manual)
- **Line Return**: after file completes, returns X to 0 (line start)
- Added `scanBoundingBox()` — pre-scans PSRAM G-code buffer for min/max X,Y
- Added `setupModeTransforms()` — called from `startCut()` to compute all scales/offsets
- Added `modeXform` struct to hold transform state (bbox, scale, offset, replay count)
- Mode key handlers enforce mutual exclusion of size modes (Fit to Page, Fit to Length,
  Auto Fill) as specified in the manual
- Center Point restricted to Landscape mode only
- Added `memset(&modeXform, 0)` in setup() and KEY_RESETALL
- Updated USER_MANUAL.md mode/function tables with transform descriptions

### Session 1 — Initial Project Creation
- Created project structure and all source files
- Wrote platformio.ini, config.h, all module .h/.cpp files
- Integrated everything in main.cpp with dual-core setup
- Build verified (RAM 15%, Flash 27.2% — no actual compile test, manual check)

### Session 2 — Keyboard + Magnification
- Added KBD_ENABLE/KBD_* and MAG_* to config.h
- Created keyboard.h/cpp with Plotter shift-register scanning
- Added setZoom() to SVG parser
- Added zoom display to OLED status view
- Integrated keyboard and mag pot into main loop
- Wired pin sharing (keyboard reuses button GPIOs)

### Session 3 — HPGL Parser
- Created hpgl_parser.h/cpp with IN, PU, PD, PA, PR, SP, LT commands
- Added HPGL_UNITS_PER_MM, HPGL_PEN_PRESSURE to config.h
- Added auto-detection in processLine() (^[A-Z]{2} prefix check)
- Added $hpgl/$gcode toggle commands
- Documented HPGL + Inkscape setup in README.md
- Created AGENTS.md for session handoff

### Session 4 — Plotter UI Emulation
- Extracted full UI/behavior specs from ManualsLib (pages 8–21)
- Created `include/plotter_ui.h` with all Plotter constants, enums, PlotterState struct
- Modified `config.h` — added BUZZER_PIN, size dial macros, mat dimensions
- Rewrote `display.cpp` — Plotter-style 5-line status view on 128x64 OLED
- Rewrote `menu.h/cpp` — 5 settings sub-pages with selection
- Rewrote `main.cpp` — comprehensive key handler for all 120+ keys, mode/function toggles, startCut(), multi-cut, beep

### Session 5 — USB Flash Drive + PSRAM (native USB OTG)
- Changed platform from `esp32dev` to `esp32-s3-devkitc-1` with PSRAM
- Removed `USB_Host_Shield_2.0` dependency (MAX3421E not used)
- Removed `sd_card.h/cpp`; created `psram_buffer.h/cpp` (4 MB PSRAM pool)
- Rewrote `usb_drive.h/cpp` — ESP-IDF `usb_host.h` native USB OTG driver + MSC BOT protocol + FAT32 reader
- Updated `config.h` — removed MAX3421E pins, added I2C expander toggle
- Updated menu to use USBDrive enumeration instead of SD
- Build verified successfully: RAM 15.7% SRAM, Flash 25.3%

### Session 6 — SVG → G-code Caching + FAT32 Write
- Added FAT32 write support to `usb_drive.cpp` (SCSI WRITE10, cluster allocation, directory creation, file write)
- Added `makeDir()` and `writeFile()` public methods to `usb_drive.h`
- Added `resolvePath()` for subdirectory navigation in `loadFile()`, `exists()`, `writeFile()`, `makeDir()`
- Rewrote `plotSVG()` — SVG source copied to PSRAM heap, callbacks write G-code to PSRAM buffer
- Multi-cut SVG saves to `/GCODE/BASENAME_M##.GCO` (zoom-encoded filename); single-copy plays from PSRAM
- Only writes if file doesn't already exist (saves USB erase cycles)
- Build verified: RAM 15.8%, Flash 25.4%

### Session 7 — WiFi SSID/Password Menu Editor
- Added `MENU_SETTINGS_WIFI` and `MENU_SETTINGS_WIFI_EDIT` menu pages
- Added `wifiSSID[33]`, `wifiPass[65]`, `wifiChanged` flag to `PlotterState`
- Added text editing mode to menu: `feedChar()`, `feedBackspace()`, `feedConfirm()`, `feedCancel()`
- Plotter keyboard ASCII keys (A-Z, 0-9, space, minus, period) route to editor when menu is in editing mode
- WiFi credentials saved to NVS (`Preferences`) via `saveWiFi()`, loaded at startup
- `wifiChanged` flag triggers `wifiServer.begin()` re-init in `loop()` with new credentials
- OLED display shows SSID/Password values and text editor with confirm/cancel hints
- Build verified: RAM 15.8%, Flash 25.5%

### Session 8 — Quadrature Encoder for Zoom Control
- Removed `MAG_ENABLE`, `MAG_PIN`, `MAG_LOOKBACK`, `MAG_MIN`, `MAG_MAX` from config.h (pot was not used)
- Removed `updateMagnification()` analog read function from main.cpp
- Added `ENC_A_PIN` (GPIO 18) and `ENC_B_PIN` (GPIO 23) for quadrature rotary encoder
- Implemented `updateEncoder()` with state-transition lookup table for direction decoding
- Each encoder detent changes zoom by ±0.1x, clamped to 0.1x–4.0x range
- Zoom defaults to 1.0x on startup; GPIO 34 is now endstop-only
- Updated pin tables, known issues, and docs in AGENTS.md / README.md
- Build verified: RAM 15.8%, Flash 25.5%

### Session 9 — Potentiometer Calibration (5-detent snapping + menu wizard)
- Added `SPEED_PIN` (GPIO 5) for speed potentiometer with `updateSpeed()` ADC read
- Speed level (1-5) now controls `currentFeed` (500-3000 mm/min, interpolated)
- Added calibration data arrays to `PlotterState` (`calSpeed[5]`, `calPressure[5]`, validity flags)
- NVS storage: `spd_cal`, `prs_cal` blobs, `cal_flags` bitmask; load at startup, save after wizard
- `updatePressure()` rewritten: snaps pot to 5 detent bands, updates `pressureLevel`, maps 1-5 to 0-100% duty
- `updateSpeed()` rewritten: snaps pot to 5 detent bands, updates `speedLevel` and `currentFeed`
- Fallback: `adcToLevel()` divides ADC range 0-4095 into 5 equal bands when no calibration exists
- Calibration wizard in Settings menu: "Speed Cal" and "Pressure Cal" each step through 5 detents, read potentiometer on OK press, save to NVS
- "Reset Cal" option clears both calibrations
- OLED draws wizard state: type, step number, instructions
- Build verified: RAM 15.8%, Flash 25.6%

## Reset All behavior
- Clears on-screen char buffer, resets mode to Landscape, zeroes all functions, resets quantity to 1, size to 1.0", shift state

## Sound On/Off
- Persisted in plotter.soundOn (bool); checked by beep() before generating tone
- Startup double-beep plays regardless (hardware power-on indicator)

## Multi-Cut Implementation
- MultiCut cycles: Off → 2 passes → 3 passes → 4 passes
- When file finishes playing and cutsRemaining > 0, the file is reopened from currentFilePath and replayed
- cutsRemaining is decremented after each full playthrough
- If the file cannot be reopened, cutsRemaining is reset to 0

## Open Issues / Gotchas
1. **STOP button** — KBD_STOP = -1 in config.h by default. The STOP key on the original machine is a dedicated button not in the matrix. Must wire to a GPIO and set KBD_STOP.
2. **Size dial** — Currently repurposes the magnification concept. The Size Dial value is displayed but not yet applied to the output (scaling of characters). Needs quadrature encoder input.
3. **Character buffer & text entry** — Keys A-Z and 0-9 go into the on-screen character buffer, but this is purely for display emulation. The actual character generation from cartridge/font data is not implemented (replaced by USB SVG/GCode file input).
4. **Paper Saver** — Toggle state tracked but layout optimisation not implemented.
5. **Mix 'n Match** — Toggle state tracked but alternation between two designs not implemented (requires two loaded files).
6. **USB OTG speed limit** — ESP32-S3 only supports Full Speed (12 Mbps) USB. High Speed flash drives may not enumerate.
7. **PSRAM board detection** — PlatformIO board `esp32-s3-devkitc-1` reports "No PSRAM" by default; must set `board_build.psram = enable` in platformio.ini.
8. **G-code file must exist before multi-cut replay** — SVG multi-cut saves G-code to USB on first pass; subsequent passes reload from `/GCODE/` file. If USB write fails (e.g. no `/GCODE/` dir, USB ejected), multi-cut will abort after first pass.
9. **HPGL bounding box** — The `scanBoundingBox()` scanner only parses G0/G1 commands; HPGL files use PA/PR/PD/PU commands. Fit to Page / Center Point with HPGL files will not compute correct bounds.
10. **Firmware update blocks Core 1** — `performFirmwareUpdate()` blocks the UI loop while flashing. Core 0 (motion) is stopped by setting `state = IDLE`. This is acceptable because firmware updates are infrequent and the OLED progress is updated directly within the blocking function.
