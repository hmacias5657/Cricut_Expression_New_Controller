# Plotter — ESP32 Firmware User Manual

## 1. Overview

This firmware replaces the original control board with an ESP32-S3,
adding USB flash drive support, SVG parsing, Wi-Fi control, and a quad-encoder zoom
while retaining the original membrane keyboard and OLED display.

**Hardware**: ESP32-S3-DevKitC-1U-N8R8 (8 MB PSRAM), TMC2209 stepper drivers,
SSD1322 128×64 OLED, Plotter keyboard, quadrature encoder (GPIO 18/23),
speed pot (GPIO 5), pressure pot (GPIO 35), endstop (GPIO 34).

---

## 2. Keyboard Key Functions

The membrane keyboard is a 24-column × 5-row shift-register matrix.
All 120+ keys are scanned; the table below shows the firmware actions for each key.

### 2.1 Status Mode (default, menu closed)

#### Blade Navigation (8-way arrow cluster)

| Keys | Action |
|------|--------|
| ▲ (UP) | Move blade Y −1 mm (toward top of mat, Y=0) |
| ▼ (DOWN) | Move blade Y +1 mm (toward bottom of mat) |
| ◄ (LEFT) | Move blade X −1 mm |
| ► (RIGHT) | Move blade X +1 mm |
| ◄▲ (UP-LEFT) | Move blade X −1, Y −1 |
| ▲► (UP-RIGHT) | Move blade X +1, Y −1 |
| ◄▼ (DOWN-LEFT) | Move blade X −1, Y +1 |
| ▼► (DOWN-RIGHT) | Move blade X +1, Y +1 |

Blade position is constrained to the current mat dimensions (304.8 mm × 304.8 mm for
12×12, 304.8 mm × 609.6 mm for 12×24).

#### Shift / Text

| Key | Action |
|-----|--------|
| SHIFT | Momentary shift — next letter is lowercase |
| SHIFT LOCK (CUT+SHIFT) | Toggle shift lock on/off |
| A–Z | Append letter to character buffer (upper/lower per shift) |
| 0–9 | Append digit to character buffer |
| SPACE | Append space |
| BACKSPACE | Remove last character |
| CLEAR DISPLAY | Clear entire character buffer |
| RESET ALL | Clear buffer, reset mode to Landscape, zero functions, reset quantity/size |

#### Cutting Modes

Per the Cricut Expression manual: Portrait, Mix 'n Match, and Quantity can be
combined. Size modes (Fit to Page, Fit to Length, Auto Fill) are mutually exclusive
— selecting one deselects the others.

| Key | Action |
|-----|--------|
| PORTRAIT | Toggle Portrait — rotates G-code/SVG 90° CW |
| FIT TO PAGE | Scale design to fill mat bounds (preserves aspect ratio) |
| FIT TO LENGTH | Scale design to fit the longer mat dimension |
| MIX 'N MATCH | Toggle Mix 'n Match / Landscape |
| QUANTITY | Replay design N times, stacked vertically with 2 mm gap |
| AUTO FILL | Tile design to fill mat (max 300 copies) |
| PAPER SAVER (MATERIAL SAVER) | Toggle paper-saver layout optimisation |

#### Functions

| Key | Action |
|-----|--------|
| MULTI CUT | Cycle: Off → 2 passes → 3 passes → 4 passes |
| CENTER POINT | Centers design at current blade position (Landscape only) |
| LINE RETURN | After file replay, returns X to 0 (line start) |
| FLIP | Mirror X coordinates (flip horizontally) |

#### Paper / Mat

| Key | Action |
|-----|--------|
| LOAD MAT | Print "load mat" to serial (placeholder) |
| UNLOAD MAT | Print "unload mat" to serial (placeholder) |
| MAT SIZE (SET PAPER SIZE) | Toggle 12×12 / 12×24 |
| SET CUT AREA | Print "set cut area" to serial (placeholder) |
| LOAD LAST | Placeholder |

#### Transport / Execute

| Key | Action |
|-----|--------|
| CUT | Start cutting the currently loaded file (applies multi-cut, modes) |
| REPEAT LAST | Re-play the PSRAM buffer |
| STOP (dedicated pin) | Abort running operation, clear buffer, stop motors |

#### Menu / Sound

| Key | Action |
|-----|--------|
| OK | Open main menu |
| SETTINGS | Toggle main menu on/off |
| SOUND ON/OFF | Toggle buzzer beeps on/off |

#### Unmapped Keys

F1–F6, PLUS, MINUS, XTRA1, XTRA2, SEMICOLON, COMMA, PERIOD, SLASH, EQUALS,
brackets, braces, QUOTE, and other punctuation keys are recognised by the matrix
scan but are not mapped to firmware actions (they do nothing in status mode).

### 2.2 Menu Mode (menu open)

When the menu is active, navigation keys change function:

| Key | Action |
|-----|--------|
| ▲ (UP) | Move cursor up |
| ▼ (DOWN) | Move cursor down |
| ◄ (LEFT) | Go back to previous page / cancel edit |
| ► (RIGHT) | Select current item |
| OK | Select current item |
| CUT | Select current item |
| SETTINGS | Toggle menu off |

#### Text Editing Mode (WiFi SSID/Password)

When editing a text field inside the WiFi settings menu, keys behave differently:

| Key | Action |
|-----|--------|
| A–Z | Append letter (upper/lower per shift state) |
| 0–9 | Append digit |
| SPACE | Append space |
| MINUS (–) | Append hyphen |
| PERIOD (.) | Append period |
| BACKSPACE | Delete last character |
| OK / ► (RIGHT) | Confirm edit, return to WiFi page |
| ◄ (LEFT) / SETTINGS | Cancel edit, discard changes |

---

## 3. Menu Options

Press **OK** or **SETTINGS** to open the main menu. Navigate with ▲/▼ and select
with ►/OK. Press ◄ or SETTINGS to go back.

### 3.1 Main Menu

| Item | Action |
|------|--------|
| Browse USB | List files on USB flash drive |
| Settings | Open settings sub-menus |
| About | Show firmware version info |

### 3.2 File Browser (Browse USB)

When a USB flash drive is connected and ready, selecting "Browse USB" enumerates all
files in the root directory. Hidden files (starting with `.`) are skipped.

- **Directories**: `[DIR]` entries can be entered via the ►/OK key. Navigate
  back to the parent directory with the ◄ key. Supports arbitrary nesting depth.
- Select a file to see its info (name, type)
- Select again to plot the file (or flash firmware for `.bin` files — see §9)
- Files ending in `.svg` are parsed and converted to G-code on-device
- All other files are streamed as G-code or HPGL (auto-detected)

### 3.3 Settings

Seven sub-pages:

| Item | Options |
|------|---------|
| Language | EN, FR, ES, DE |
| Units | in (1/4), in (1/10), cm, mm |
| Multi Cut | 2 passes, 3 passes, 4 passes |
| Mat Size | 12×12 in, 12×24 in |
| Char Images | Show, Hide |
| WiFi | Edit SSID / Password / Save |
| Calibrate | Speed Cal / Pressure Cal / Reset Cal |

**WiFi Editor**: Selecting "WiFi" shows the current SSID and password (masked
with `*`). Choose "SSID" or "Password" to enter text edit mode using the Plotter
keyboard (see §2.2 Text Editing Mode above). Select "Save" to write credentials
to NVS and restart the Wi-Fi access point with the new settings.

**Settings Persistence**: Language, Units, Mat Size, and Character Images settings
are saved to NVS automatically whenever you change them. They are restored on the
next power cycle.

**Calibrate Wizard**: Choose "Speed Cal" or "Pressure Cal" to step through the
5 physical detents of the respective potentiometer. At each step, turn the pot
to the next detent click position and press **OK** to record the ADC value.
After the 5th detent the calibration is saved to NVS automatically. "Reset Cal"
clears both calibrations, reverting to a flat 5-way split of the ADC range
(0–4095 ÷ 5). Runtime snaps to a ±5% band around each calibrated center value
(50-count minimum radius).

### 3.4 About

Displays:
```
Plotter
Firmware v1.0
ESP32 + TMC2209
```

Firmware updates are performed via USB pendrive — see §9.

---

## 4. Movement Limits

### 4.1 Default Limits

| Parameter | Default | Description |
|-----------|---------|-------------|
| `X_MAX_MM` | 304.8 | Maximum X travel (mm) — Cricut Expression 12″ gantry |
| `Y_MAX_MM` | 609.6 | Maximum Y travel (mm) — Cricut Expression 24″ travel |
| `STEP_PER_MM` | 80.0 | Steps per mm (belt×pulley×microstepping) |
| `MAX_FEEDRATE` | 3000.0 | Maximum feed rate (mm/min) |
| `ACCELERATION` | 800.0 | Acceleration (mm/s²) |
| `DEFAULT_FEED` | 1000.0 | Default cutting feed (mm/min) |
| `HOMING_FEED` | 500.0 | Homing speed (mm/min) |
| `HOMING_BACKOFF` | 5.0 | Back-off distance after endstop hit (mm) |

G-code moves are clamped to the `X_MAX_MM` × `Y_MAX_MM` rectangle (see
`onMove()` in `src/main.cpp`).

### 4.2 Mat Dimensions (Plotter Mode)

When using Plotter UI modes, blade position is constrained to the selected mat:

| Mat Size | Width | Height |
|----------|-------|--------|
| 12×12 in | 304.8 mm | 304.8 mm |
| 12×24 in | 304.8 mm | 609.6 mm |

### 4.3 How to Change Limits

Edit `src/config.h` and change the relevant `#define` values:

```c
// Motion parameters (mm)
#define X_MAX_MM      304.8f    // 12" — Cricut Expression gantry width
#define Y_MAX_MM      609.6f    // 24" — Cricut Expression max travel
#define STEP_PER_MM   80.0f
#define MAX_FEEDRATE  3000.0f
#define ACCELERATION  800.0f
#define DEFAULT_FEED  1000.0f
#define HOMING_FEED   500.0f
#define HOMING_BACKOFF 5.0f
```

**`STEP_PER_MM`** depends on your mechanical setup:

```
STEP_PER_MM = (motor_steps_per_rev × microsteps) / (belt_pitch_mm × pulley_teeth)
```

Example for 0.9° motors (400 steps/rev), 1/16 microstepping, 2 mm belt pitch,
20-tooth pulley:

```
(400 × 16) / (2 × 20) = 160 steps/mm
```

After editing `config.h`, rebuild and flash:

```bash
cd plotter
~/.platformio/penv/bin/pio run -t upload
```

---

## 5. Startup Initialization

When power is applied, the firmware runs the following sequence in `setup()`:

### 5.1 Serial

- Initialises UART at 115200 baud
- Prints banner: `ESP32 GCode Plotter`

### 5.2 GPIO

- Configures ENDSTOP as `INPUT_PULLUP`
- Configures button pins (if keyboard disabled)
- Configures solenoid PWM on LEDC channel 0 (5 kHz, 8-bit)
- Configures SPEED_PIN (GPIO 5) as input
- Initialises stepper motor pins

### 5.3 PSRAM Buffer

- Allocates 4 MB PSRAM pool via `ps_malloc()`
- Prints `PSRAM buffer ready` or error message if allocation fails

### 5.4 USB Host

- Starts ESP-IDF USB host driver for native USB OTG (GPIO 19/20)
- Prints `USB host started` or `USB host init failed`
- USB flash drives are enumerated asynchronously after boot

### 5.5 G-Code / HPGL Parsers

- Wires all callbacks (`onMove`, `onSolenoid`, `onHome`, `onReport`, `onFile`,
  `onError`)
- Registers G2/G3 arc interpolation callback
- Initialises drag knife compensation (`knifeCompReset()`)
- Prints `HPGL ready`

### 5.6 Plotter UI State

- Wires `PlotterState` to display and menu modules
- Loads Wi-Fi credentials from NVS (falls back to config.h defaults)
- Loads potentiometer calibration data from NVS (`spd_cal`, `prs_cal` blobs)
- Initialises OLED display, shows "Plotter / Booting..."
- Sets default pressure (50 %), zoom (1.0×), speed (3), pressure level (3),
  size (1.0″)

### 5.7 Buzzer

- Configures LEDC channel 1 for buzzer PWM
- Plays a startup double-beep: short beep (50 ms) + long beep (200 ms)
- Double-beep plays even if sound is set to OFF (hardware power-on indicator)

### 5.8 Keyboard

- Initialises Plotter keyboard shift-register scanning
- Prints `Keyboard OK`

### 5.9 Menu

- Initialises menu system (page = MAIN, cursor = 0)
- Sets plot callback and USB drive reference

### 5.10 Quadrature Encoder

- Configures ENC_A (GPIO 18) and ENC_B (GPIO 23) as `INPUT_PULLUP`
- Reads initial encoder state for transition tracking

### 5.11 Wi-Fi Access Point

- Starts soft-AP with SSID/password from NVS or config.h defaults
- Starts AsyncWebServer on port 80 with WebSocket `/ws`
- Displays AP IP address on OLED

### 5.12 Motion Task (Core 0)

- Creates `motionTask` pinned to Core 0 (stack 8192, priority 1)
- Runs custom S-curve motion planner + step pulse generator with `vTaskDelay(1)`
- `evalVelocity()` computes instantaneous velocity in O(1) per tick

### 5.13 Main Loop (Core 1)

After `setup()` completes, `loop()` runs on Core 1:

```
loop iteration:
   1. handleSerial()     — read and process serial commands (including $status)
   2. updateBeep()       — non-blocking beep state machine (turn off, advance pattern)
   3. updatePressure()   — read pressure pot (EMA smoothed, 5-detent snap)
   4. updateSpeed()      — read speed pot (EMA smoothed, 5-detent snap)
   5. updateEncoder()    — read quadrature encoder, update zoom
   6. handleKeyboard()   — scan Plotter keyboard matrix (non-blocking debounce)
   7. wifiServer.handleClient() — handle WebSocket / HTTP
   8. pollUSB()           — poll USB host events, periodic health check
   9. Display update      — error view / menu / status view
   10. STOP check          — abort if STOP pin pulled low
   11. moveComplete check  — print "ok" when move finishes (atomic read)
   12. State machine      — DWELL, PLAYING_SD (multi-cut loop), PAUSED,
       KNIFE_PIVOT (knife compensation), PostCutAction continuation
```

---

## 6. File Formats

### 6.1 G-Code

Supported commands: `G0`/`G1` (move), `G2`/`G3` (clockwise/counter-clockwise
arcs with I,J center offsets or R radius), `G4` (dwell), `G28` (home),
`G90`/`G91` (absolute/relative), `M3`/`M4`/`M5` (solenoid with S-value),
`M92` (set position), `M114` (report).

Arcs are interpolated as line segments (`GCODE_ARC_SEGMENTS = 64` per full circle).

### 6.2 HPGL

Auto-detected by two uppercase letters at line start. Supported commands:
`IN`, `PU`, `PD`, `PA`, `PR`, `SP` (pen-to-pressure mapping), `LT`,
**`SC`** (user-unit scaling), **`IP`** (input P1/P2 extents).
1016 HPGL units per inch (40 units/mm). Force mode with `$hpgl`/`$gcode`.

Inkscape's HPGL output typically includes `IP` and `SC` commands to define
the coordinate system. These are now parsed and applied automatically.
Without `SC`, the previous default scaling applies.

### 6.3 SVG

On-device conversion of `<path>` elements (`M`/`m`, `L`/`l`, `H`/`h`, `V`/`v`,
`C`/`c`, `S`/`s`, `Q`/`q`, `T`/`t`, `Z`/`z`) plus **SVG primitives**:
`<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`.
Bezier curves approximated with line segments (`SVG_CURVE_STEPS = 16`). viewBox
scaling preserves aspect ratio. Max file size 2 MB. Zoom adjusted via quadrature
encoder (0.1×–4.0×). Smooth bezier (`S`/`T`) correctly mirrors the previous
control point for tangent-continuous joins.

Multi-cut SVG plots save the converted G-code to `/GCODE/BASENAME_M##.GCO` on
the USB drive; subsequent passes replay from the cached file.

### 6.4 Drag Knife Compensation

The Cricut Expression uses a drag knife (swivel blade) with the blade tip offset
~0.75 mm from the pivot. At sharp corners (direction change > 15°), the firmware
automatically performs a **lift → pivot → lower** sequence to prevent blade tearing:

1. Raise solenoid (`M5`)
2. Move the carriage forward past the corner in the new direction (by `KNIFE_OFFSET_MM`)
3. Lower solenoid (`M3`)
4. Continue on the planned path

This is transparent to the G-code file — it is inserted at runtime by the
compensation layer. The `KNIFE_ANGLE_THRESHOLD_DEG` (default 15°) and
`KNIFE_OFFSET_MM` (default 0.75 mm) are configurable in `config.h`.

---

## 7. Serial Commands

| Command | Description |
|---------|-------------|
| `$help` | Print help |
| `$files` | List USB drive files |
| `$upload name:content` | Upload file to PSRAM buffer |
| `$pause` | Pause playback |
| `$resume` | Resume playback |
| `$stop` | Stop playback, clear buffer |
| `$menu` | Toggle menu on/off |
| `$menu up/down/select/back` | Navigate menu |
| `$hpgl` | Force HPGL mode |
| `$gcode` | Force G-code mode (or auto-detect) |
| `$status` | Dump PlotterState as JSON (mode, speed, pressure, position, state, zoom) |
| `?` | Report position + pressure |

---

## 8. Error Conditions

| Symptom | Cause |
|---------|-------|
| OLED shows "ERROR!" + "FILE TOO LARGE" + three beeps | SVG file exceeds 2 MB, or G-code/HPGL exceeds 4 MB PSRAM buffer |
| OLED shows "ERROR!" + "USB NOT READY" + three beeps | USB drive not detected or not enumerated |
| OLED shows "ERROR!" + "PSRAM FAIL" + three beeps | PSRAM allocation failed at boot |
| Serial: "error: not homed" | Attempting to move without homing first |
| Serial: "error: cannot open" | File not found on USB or PSRAM |
| No USB files listed | Drive may be High Speed (480 Mbps) — ESP32-S3 only supports Full Speed (12 Mbps) |

Errors auto-clear from the OLED after 5 seconds.

---

## 9. Firmware Update via USB

The firmware can be updated by flashing a `.bin` file from a USB pendrive, no
serial cable or JTAG needed.

### 9.1 Preparing the Update File

Build the firmware binary with PlatformIO:

```bash
cd plotter
~/.platformio/penv/bin/pio run
```

The output file is at `.pio/build/esp32s3dev/firmware.bin`. Copy it to the
**root** directory of a FAT32-formatted USB flash drive.

### 9.2 Flashing Procedure

1. Insert the USB drive with `firmware.bin` (the drive is hot-pluggable).
2. Press **OK** or **SETTINGS** to open the menu.
3. Browse USB → select `FIRMWARE.BIN`.
4. The display shows: **"Update firmware from this file?"**
5. Press **OK** to confirm (◄ to cancel).

The OLED shows a progress bar with percentage. The update process:

- Stops any running motion and clears the G-code buffer
- Reads the file from USB in 512-byte chunks (no PSRAM buffering)
- Writes each chunk to the inactive OTA partition (app1, 3.2 MB)
- Validates the complete image with CRC
- On success: displays "Success! Rebooting..." and restarts into the new firmware after 2 seconds
- On failure: displays an error message, aborts the update, and returns to the menu

### 9.3 Notes

- The ESP32-S3 has **two OTA partitions**: `app0` (running) and `app1` (inactive).
  Updates always flash the **inactive** partition. After reboot the bootloader
  swaps the active slot.
- The firmware .bin is typically **~860 KB** (app code) + ~1 MB (bootloader,
  partitions table) — easily fits in the 3.2 MB OTA partition.
- Keep the USB drive connected during the entire update.
- If the update fails (USB disconnected, file corruption, etc.), the previous
  firmware remains intact in the active partition and boots normally on reset.

### 9.4 Error Messages

| OLED Message | Cause |
|--------------|-------|
| "FAIL: cannot open" | File not found or USB drive not ready |
| "FAIL: OTA begin" | OTA partition not available or not enough space |
| "FAIL: write error" | Read error from USB or write error to flash |
| "FAIL: validation" | CRC/image validation failed after write |
