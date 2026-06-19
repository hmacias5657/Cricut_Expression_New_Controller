# LESSONS — Portfolio-Specific Anti-Patterns

## esp32-gcode-plotter

### L1: USB OTG Full Speed Only
- ESP32-S3 USB OTG supports Full Speed (12 Mbps) only
- High Speed (480 Mbps) flash drives may not enumerate
- Use older/smaller FAT32-formatted drives

### L2: PSRAM Board Detection
- PlatformIO board `esp32-s3-devkitc-1` defaults to "No PSRAM"
- Must set `board_build.psram = enable` in platformio.ini
- Must add `-DBOARD_HAS_PSRAM` to build_flags

### L3: GPIO Bank Registers
- GPIOs 0-31 use `GPIO.out_w1ts` / `GPIO.out_w1tc`
- GPIOs 32-39 use `GPIO.out1_w1ts.val` / `GPIO.out1_w1tc.val`
- Always check `pin < 32` before register access

### L4: AccelStepper → S-curve Migration
- Replaced AccelStepper library with custom 7-phase S-curve planner
- Removed `accelstepper@^1.64` from lib_deps
- Step generation now uses rate-limited pulse spacing from analytical velocity
- Lesson: Library removal changed the entire motion stack — test thoroughly

### L5: OLED U8G2 Constructor
- Must use `U8G2_SSD1322_NHD_128X64_F_4W_SW_SPI`
- `_2X` and `_BW` variants don't exist for this display

### L6: WiFi Macro Naming
- `WIFI_AP` conflicts with WiFi library — renamed to `WIFI_AP_MODE`

### L7: Dual-Core Atomic Communication
- `volatile` is insufficient on dual-core ESP32 — use `std::atomic`
- `memory_order_seq_cst` for both `state` and `moveComplete`
- `state.load()` needed for printf cast

### L8: SVG Smooth Curve Reflection
- S/T commands must reflect previous control point, not current point
- Bug: `2*_cx - _cx == _cx` (no-op reflection)
- Fix: track `_prevCpX` / `_prevCpY` across C/S/Q/T commands

### L9: FAT32 Cluster Walking Performance
- Cluster chain walking on Full Speed USB may take 10-100ms per sector
- PSRAM buffer avoids repeated reads for same file
- FSInfo `freeClusHint` speeds up write allocation

### L10: PlatformIO Test Framework
- Tests go in `test/` directory (not `tests/`)
- Framework: `unity` (available in ESP-IDF)
- Native tests run on host, not target — mock hardware dependencies