# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version `MAJOR.MINOR.PATCH` is defined in `src/config.h` (`FIRMWARE_VERSION`).
Build number is managed automatically via `scripts/versioning.py`.

---

## [1.0.0] — 2026-06-05 (Build 1)

### Added
- Initial project creation (Session 1):
  - PlatformIO project (espressif32, Arduino framework, ESP32-S3, 8MB flash + 8MB PSRAM)
  - Stepper XY control (A4988/DRV8825, configurable steps/mm)
  - Single endstop homing
  - G-code parser — G0/G1, G4, G28, G90/G91, M3/M4/M5 (with S pressure value), M92, M114
  - PWM solenoid pen lifter (5 kHz, 8-bit, LEDC)
  - Speed potentiometer (EMA smoothing, 5 detent levels)
  - Pressure potentiometer (EMA smoothing, 5 detent levels)
  - Dual-core architecture (motion on Core 0, UI on Core 1)
- Keyboard + magnification control (Session 2):
  - Plotter shift-register keyboard scanning
  - SVG zoom adjustment via magnifier pot
- HPGL parser (Session 3):
  - IN, PU, PD, PA, PR, SP, LT commands
  - Auto-detection of HPGL vs G-code
- Plotter UI emulation (Session 4):
  - 7 cutting modes: Landscape, Portrait, Mix 'n Match, Quantity, Fit to Page, Fit to Length, Auto Fill
  - 5 functions: Multi Cut, Center Point, Line Return, Flip, Paper Saver
  - 7 settings menus (Language, Units, Multi Cut, Mat Size, Char Images, WiFi, Calibrate)
  - Plotter-style 5-line OLED status view
  - Comprehensive key handler for 120+ matrix keys
  - Multi-cut auto-replay
  - Buzzer/beep with sound on/off toggle
- USB flash drive + PSRAM native USB OTG (Session 5):
  - Native ESP32-S3 USB OTG MSC driver + FAT32 reader
  - 4 MB PSRAM file buffer
- SVG → G-code caching + FAT32 write (Session 6):
  - FAT32 write support (SCSI WRITE10, cluster allocation)
  - SVG conversion outputs to PSRAM buffer
  - Multi-cut SVG saves to USB `/GCODE/` directory
- WiFi SSID/Password menu editor (Session 7):
  - Text entry via Plotter keyboard
  - NVS persistence
- Quadrature encoder for zoom control (Session 8)
- Potentiometer calibration wizard (Session 9):
  - 5-detent snapping ±5%
  - NVS-persisted calibration
- S-curve (jerk-limited) motion profile (Session 13):
  - Custom 7-phase S-curve motion planner and step generator
  - Removed AccelStepper dependency
  - Analytical `evalVelocity()` for O(1) per-tick computation
- Knife compensation, arc support, non-blocking ops, SVG/HPGL/FS enhancements (Session 15):
  - Drag knife compensation (lift/pivot/lower corner sequence)
  - Atomic state (`std::atomic`) replacing spinlocks
  - Non-blocking beep, buttons, post-cut continuations
  - Homing timeout safety
  - G2/G3 arc support (I,J,R methods)
  - SVG primitive elements (`<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`)
  - SVG `<g transform>` (translate)
  - HPGL SC/IP commands (user-unit scaling, input P1/P2)
  - USB disk state cleanup and FSInfo free-cluster hint
  - Directory navigation in USB file browser
  - Settings persistence (NVS Preferences)
  - `$status` serial command
- USB pendrive firmware update (Session 14):
  - Streaming file read API (no PSRAM buffering)
  - OTA firmware update from USB pendrive
  - OLED progress bar + percentage during flashing
- **Automatic build versioning** (Session 16):
  - `FIRMWARE_VERSION` define in `config.h`
  - `scripts/versioning.py` PlatformIO extra script for auto build number and bin renaming
  - Versioned firmware binary (`firmware_v{VER}_b{BUILD}.bin`)
  - `CHANGELOG.md` tracking

### Changed
- **GPIO re-assignment** (Session 5): Changed from ESP32 dev board to ESP32-S3-DevKitC-1
- **X_MAX_MM** (Session 14): 200.0 → 304.8 (12″ gantry)
- **Y_MAX_MM** (Session 14): 200.0 → 609.6 (24″ travel)
- **Y-axis navigation** (Session 11): ▲ decreases Y, ▼ increases Y (0,0 = upper-left)
- **Homing direction** (Session 11): Endstop on right → move right to hit, set X_MAX_MM
- **Mode/function coordinate transforms** (Session 12):
  - Portrait: 90° CW rotation
  - Flip: mirror X
  - Fit to Page/Length: pre-scan bounding box, uniform scaling
  - Center Point: offset to blade position
  - Quantity: stacked replay with gap
  - Auto Fill: tiled grid
  - Line Return: X → 0 after file
- **`adcToLevel` ceiling fix**: `(raw*5+4095)/4096` instead of floor truncation
- **`motionTask` stack**: 4096 → 8192

### Fixed
- SVG bezier reflection (`S`/`T`): Previous control point now correctly mirrored
- WebSocket data length: `len` passed to callback in `wifi_server.cpp`
- `gpioSet`/`gpioReadRaw`: Bank-aware handling of GPIOs 32-39

---

## [1.1.0] — TBD

### Planned
- Hardware testing (keyboard scanning, endstop, solenoid, buzzer)
- Tune `STEP_PER_MM` for actual belt/pulley/microstepping
- STOP button wiring
- HPGL bounding box for Fit to Page / Center Point
- Paper Saver layout optimisation
- Mix 'n Match file alternation
- Character cartridge emulation
- Wi-Fi station mode fallback
- G-code motion planner buffering
