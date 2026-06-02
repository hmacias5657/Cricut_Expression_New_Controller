# Plotter — ESP32 Firmware Replacement

Open-source firmware for a CNC cutting machine. Drives two
stepper motors with TMC2209 drivers, controls solenoid pressure via MOSFET PWM, accepts
G-code/HPGL/SVG over USB serial, USB flash drive, or Wi-Fi upload, hosts a Wi-Fi
WebSocket control interface, and emulates the original machine UI on a 128×64
OLED display with the original membrane keyboard. **Beware, Not tested with hardware yet**

## Features

### Plotter UI Emulation
- **Full keyboard support** — all 120+ matrix keys: blade navigation (8-way), Shift/Shift
  Lock, text entry (A–Z, 0–9), Clear Display, Reset All, Repeat Last, Sound On/Off,
  Load/Unload Mat, Set Paper Size, Load Last
- **7 cutting modes**: Landscape (default), Portrait (90° CW rotation), Mix 'n Match,
  Quantity (stacked replay), Fit to Page (scale to mat), Fit to Length (scale to fit),
  Auto Fill (tiled grid, max 300 copies). Size modes mutually exclusive per manual.
- **5 functions**: Multi Cut (2/3/4 passes), Center Point (offset to blade position,
  Landscape only), Line Return (X return after file), Flip (mirror X), Paper Saver
- **7 settings menus**: Language (EN/FR/ES/DE), Units (in-1/4, in-1/10, cm, mm),
  Multi Cut passes, Default Mat Size (12×12 / 12×24), Character Images show/hide,
  WiFi SSID/Password (Plotter keyboard text entry, saved to NVS),
  Calibrate (Speed/Pressure pot detent wizard)
- **Potentiometer calibration**: Settings → Calibrate wizard steps through 5 physical
  detents per pot, snap radius ±5% of ADC value, persisted in NVS, fallback to equal
  5-way ADC split when uncalibrated
- **5-line Plotter-style OLED status view**: mode bar, blade X/Y position, Size dial
  value, Speed bars (1–5), Pressure bars (1–5), function toggle states, quantity/
  sound/language/units status
- **Buzzer/beep**: short keypress confirmation, long beep on stop, respects Sound On/Off
- **Multi-cut auto-replay**: reopens and replays the file for each pass
- **Size dial**: defaults to 1.0″ on startup (input source TBD)
- **WiFi credentials editor**: SSID/Password editable from Settings menu via Plotter keyboard text entry, saved to NVS, applied live

### Motion & Control
- **G-code**: G0/G1, G4, G28, G90/G91, M3/M4/M5 (solenoid with S-value pressure),
  M92 (set position), M114 (report). Configurable steps/mm, acceleration, max feedrate.
- **HPGL**: IN, PU, PD, PA, PR, SP (pen-to-pressure mapping), LT. Auto-detected or
  forced via `$hpgl`/`$gcode`. Compatible with Inkscape HPGL output.
- **SVG parsing**: on-device conversion of SVG `<path>` elements (M/L/H/V/C/S/Q/T/Z)
   to G-code with viewBox scaling and quadrature encoder zoom. Max 2 MB.
- **Dual-core**: Core 0 runs `AccelStepper.run()` in a tight loop for jitter-free
  step pulses; Core 1 handles UI, serial, Wi-Fi, file I/O, pots, display, keyboard.
- **Single endstop homing**: homes X axis (rightmost = X_MAX_MM), backs off, tracks position.
- **Mode transforms**: Portrait (90° rotation), Flip (mirror X), Fit to Page/Length
  (scale from bounding box), Center Point (offset to blade position), Quantity (stack),
  Auto Fill (tile grid), Line Return (X → 0 after file). All applied in `onMove()`.
- **Speed control**: potentiometer (GPIO 5) with 5-detent snap → feed rate 500–3000 mm/min
- **Pressure control**: potentiometer (GPIO 35) with 5-detent snap → solenoid PWM via MOSFET
  (5 kHz, 8-bit, LEDC). Priority: G-code S-value > potentiometer.

### Communication & Storage
- **Serial**: 115200 baud terminal
- **Wi-Fi AP**: AsyncWebServer + WebSocket, file upload to PSRAM, command log
- **USB flash drive** (native ESP32-S3 OTG): file listing, streaming G-code/HPGL/SVG
  playback with pause/resume/stop. Full Speed (12 Mbps), MSC Bulk-Only Transport.
  GPIO 19 (D−) / 20 (D+), fixed.
- **PSRAM file buffer**: 4 MB pool; files larger than buffer show OLED error + beep
- **Menu system**: Browse USB, Settings (7 sub-pages), About. Navigate via keyboard,
  buttons, or `$menu` serial commands.
- **Firmware update**: Select a `.bin` file from the USB file browser → confirmation
  dialog → OTA flash to the inactive partition with OLED progress bar → automatic
  reboot. Streaming reads avoid PSRAM buffering. Validation via `Update.end()`.

## Wiring

| Component        | GPIO | Notes                              |
|------------------|------|------------------------------------|
| X STEP           | 12   |                                    |
| X DIR            | 13   |                                    |
| Y STEP           | 14   |                                    |
| Y DIR            | 27   |                                    |
| Enable (drivers) | 26   | Active low, both drivers           |
| Endstop          | 34   |                                    |
| Solenoid PWM     | 32   | LEDC channel 0, 5 kHz, 8-bit      |
| Pot (pressure)   | 35   | 10 kΩ, ADC, 5 detent positions    |
| Pot (speed)      | 5    | 10 kΩ, ADC, 5 detent positions    |
| Buzzer           | —    | Set BUZZER_PIN in config.h         |
| OLED CS          | 15   |                                    |
| OLED DC          | 2    |                                    |
| OLED RST         | 4    | Shares KBD_ROW4 (saved/restored)   |
| OLED MOSI        | 16   |                                    |
| OLED SCK         | 17   |                                    |
| USB D+           | 20   | USB OTG (fixed, internal PHY)      |
| USB D−           | 19   | USB OTG (fixed, internal PHY)      |
| ENC A            | 18   | Quadrature encoder channel A       |
| ENC B            | 23   | Quadrature encoder channel B       |

### I2C IO Expander (optional)

If more GPIOs are needed, an MCP23017 (or compatible) can be added via I2C. Set
`I2C_IO_EXPANDER_ENABLE 1` in `config.h` and wire SDA/SCL to the configured pins.

### Plotter Keyboard

The original membrane keyboard uses a shift-register matrix
(24 columns × 5 rows). The 14-pin ribbon cable connects as follows:

| KBD Pin | GPIO | Notes                          |
|---------|------|--------------------------------|
| CLK     | 33   | Shift register clock           |
| DATA    | 25   | Shift register data            |
| ROW 0   | 36   | ADC1 input-only, top row       |
| ROW 1   | 39   | ADC1 input-only                |
| ROW 2   | 22   | Shares BTN_SELECT              |
| ROW 3   | 21   | Shares BTN_BACK                |
| ROW 4   | 4    | Shares OLED_RST (saved/restored)|
| STOP    | —    | Set KBD_STOP in config.h       |
| LED EN  | —    | Key backlight enable (optional)|

Set `KBD_ENABLE 0` in `config.h` to disable the keyboard and recover the button
GPIOs. The keyboard supports all original machine keys — see
`include/plotter_ui.h` for the complete key map.

## Quick Start

```bash
cd plotter
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor
```

Connect to the `PlotterAP` Wi-Fi network and open `192.168.4.1` in a browser.

## Configuration

Edit `src/config.h`:

- `STEP_PER_MM` — steps per mm (belt pitch × pulley teeth × microstepping)
- `X_MAX_MM` / `Y_MAX_MM` — build area limits (default 304.8 / 609.6 mm for Cricut Expression)
- `MAX_FEEDRATE` / `ACCELERATION` — motion constraints
- `BUZZER_PIN` — set to a GPIO for piezo buzzer beeps
- `KBD_STOP` — set to a GPIO for the dedicated STOP button
- `WIFI_SSID` / `WIFI_PASS` — AP credentials (editable from UI, saved to NVS)
- `ENC_A_PIN` / `ENC_B_PIN` — quadrature encoder for zoom control
- `HPGL_UNITS_PER_MM` / `HPGL_PEN_PRESSURE` — HPGL scaling and pen mapping
- `USB_ENABLE` — 1 to enable USB OTG host, 0 for PSRAM-only (WiFi/serial uploads)

### Plotter UI parameters (edit config.h)

| Macro              | Default | Description                    |
|--------------------|---------|--------------------------------|
| `BUZZER_PIN`       | -1      | Piezo buzzer GPIO (−1 = off)  |
| `SIZE_MIN_INCHES`  | 0.25    | Size dial minimum (inches)     |
| `SIZE_MAX_INCHES`  | 11.5    | Size dial maximum (inches)     |
| `MAT_W_MM`         | 304.8   | Mat width (12″)                |
| `MAT_H_MM`         | 304.8   | Mat height (12″ or 24″)        |

## Commands

Standard G-code plus `$` system commands:

| Command               | Description                       |
|-----------------------|-----------------------------------|
| `$help`               | Print help                        |
| `$files`              | List USB drive files              |
| `$upload n:content`   | Upload content to PSRAM buffer    |
| `$pause`              | Pause playback                    |
| `$resume`             | Resume playback                   |
| `$stop`               | Stop playback                     |
| `$menu`               | Toggle menu on/off                |
| `$menu up/down/select/back` | Navigate menu             |
| `$hpgl`               | Force HPGL mode                   |
| `$gcode`              | Force G-code mode (or auto-detect)|
| `?`                   | Report position + pressure        |

## Plotter UI

### Modes

Toggle via keyboard keys or `PlotterState.mode`:

| Mode          | Key          | Description                       |
|---------------|--------------|-----------------------------------|
| Landscape     | (default)    | No transform                      |
| Portrait      | PORTRAIT     | 90° CW rotation (swap X↔Y, mirror)|
| Mix 'n Match  | MIXMATCH     | Tracked; file alternation TBD     |
| Quantity      | QUANTITY     | Stack file N times vertically     |
| Fit to Page   | FITPAGE      | Scale to fill mat bounds (AR)     |
| Fit to Length | FITLENGTH    | Scale to fit longer mat dimension |
| Auto Fill     | AUTOFILL     | Tile grid to fill mat (≤ 300)     |

### Functions

| Function     | Key          | Effect                                |
|--------------|--------------|---------------------------------------|
| Multi Cut    | MULTICUT     | Cycles Off → 2× → 3× → 4× passes     |
| Center Point | CENTERPOINT  | Offset coordinates so blade position becomes design centre (Landscape only) |
| Line Return  | LINERETURN   | After file replay, X returns to 0     |
| Flip         | FLIP         | Mirror X coordinates (X′ = X_MAX − X)|
| Paper Saver  | MATERIALSAVER| Toggle tracked; layout opt not implemented |

### Settings

Access via **Settings** key or `$menu` → Settings. Seven items:

1. **Language** — EN, FR, ES, DE
2. **Units** — in (1/4), in (1/10), cm, mm
3. **Multi Cut** — default pass count: 2, 3, or 4
4. **Mat Size** — 12×12″ or 12×24″
5. **Character Images** — Show or Hide preview
6. **WiFi** — edit SSID and password via Plotter keyboard, saved to NVS, applied live
7. **Calibrate** — Speed Cal / Pressure Cal / Reset Cal (step through 5 detents, save to NVS)

### Display — 128×64 OLED Status View

The original 128×64 graphical LCD is emulated on the SSD1322 OLED:

```
[Land] [MixN] [Qty] [FPG] [FTL] [AF]  Snd PS   ← mode bar
X:+0.0 Y:+0.0            Size: 1.00"            ← position + size dial
Speed [▣▣▣▣▣]   Press [▣▣▣▣▣]                  ← speed/pressure bars (1–5)
MC:Off CP:Off LR:Off F:Off                      ← function toggles
Qty:1 Snd:ON Lang:EN Unit:in-1/4               ← status line
```

### Key Bindings (Status Mode)

Blade navigation (8-way arrow keys) moves the virtual blade position. Letter keys
(A–Z) and number keys (0–9) enter text into the on-screen character buffer.
Shift/Shift Lock toggle case. **CUT** starts the loaded file with the current
multi-cut and mode settings. **STOP** (dedicated pin) aborts any running operation.

## Pressure Control

PWM solenoid via logic-level MOSFET (e.g., IRLZ44N). 5 kHz, 8-bit, LEDC channel 0.

- **Speed potentiometer** (10 kΩ, GPIO 5): maps ADC to 5 discrete levels via calibration.
  Each level maps to a feed rate (500–3000 mm/min).
- **Pressure potentiometer** (10 kΩ, GPIO 35): maps ADC to 5 discrete levels via calibration.
  Each level maps to 0–100% solenoid duty (0, 25, 50, 75, 100).
- **G-code**: `M3 S<0-100>` engages solenoid at given pressure; `M5` disengages.
- **Priority**: G-code S-value > potentiometer.
- **Calibration**: Settings → Calibrate wizard reads each physical detent and saves ADC
  values to NVS. Runtime snaps to ±5% band around calibrated values. Fallback: equal
  5-way ADC split when uncalibrated.

## OLED Display

Newhaven NHD-2.7-12864OLED (SSD1322, 128×64, SPI, monochrome yellow/black) via
U8G2 software SPI. Refresh rate 4 Hz in status mode; the menu system draws at
its own pace.

## Dual-Core Architecture

| Core | Task                    | Responsibility                        |
|------|-------------------------|---------------------------------------|
| 0    | `motionTask`            | `AccelStepper.run()` tight loop       |
| 1    | Arduino `loop()`        | Serial, Wi-Fi, file I/O, pots,        |
|      |                         | keyboard, display, state machine      |

Solenoid PWM (LEDC) and keyboard scanning are hardware-driven or debounced
independently.

## File Formats

### SVG Parsing

On-device SVG `<path>` → G-code conversion. Supported commands:
`M`/`m`, `L`/`l`, `H`/`h`, `V`/`v`, `C`/`c`, `S`/`s`, `Q`/`q`, `T`/`t`, `Z`/`z`.

Bezier curves are approximated with line segments (configurable via
`SVG_CURVE_STEPS`). viewBox scaling preserves aspect ratio. Max file size 2 MB.
Magnification defaults to 1.0x on startup, adjustable via quadrature encoder (GPIO 18/23) in 0.1x steps up to 4.0x.

### HPGL (HP Graphics Language)

Auto-detected by two uppercase letters at line start. Alternatively force with
`$hpgl` or `$gcode`.

| Command | Action                                           |
|---------|--------------------------------------------------|
| `IN;`   | Initialise, pen up, position (0,0)               |
| `PU;`   | Pen up                                            |
| `PU x,y;` | Pen up + move to (x,y)                         |
| `PD;`   | Pen down (current pressure)                      |
| `PD x,y;` | Pen down + draw to (x,y)                       |
| `PA x,y;` | Plot absolute                                   |
| `PR x,y;` | Plot relative                                   |
| `SP n;` | Select pen (maps to pressure %, see config)      |

Scale: 1016 HPGL units per inch (40 units/mm). Override via `HPGL_UNITS_PER_MM`.

## Pin Sharing & Conflicts

| GPIO | Primary      | Shared With             |
|------|--------------|-------------------------|
| 4    | OLED RST     | KBD_ROW4 (saved/restored)|
| 19   | USB_DN       | (fixed USB OTG pin)     |
| 20   | USB_DP       | (fixed USB OTG pin)     |
| 21   | BTN_BACK     | KBD_ROW3                |
| 22   | BTN_SELECT   | KBD_ROW2                |
| 25   | BTN_DOWN     | KBD_DATA                |
| 33   | BTN_UP       | KBD_CLK                 |
| 5    | Pot (speed)  | Speed potentiometer ADC   |
| 34   | ENDSTOP      |                     |
| 36   | KBD_ROW0     | ADC1 input-only         |
| 39   | KBD_ROW1     | ADC1 input-only         |
| 18   | ENC_A        | Quadrature encoder A    |
| 23   | ENC_B        | Quadrature encoder B    |

Set `KBD_ENABLE 0` to disable keyboard and recover buttons.

## Project Structure

```
plotter/
├── include/
│   ├── plotter_ui.h         Plotter UI spec constants, enums, PlotterState struct
├── src/
│   ├── config.h            Pin definitions, motion params, feature toggles
│   ├── main.cpp            Entry point, state machine, dual-core, Plotter UI, FW update
│   ├── stepper.h/cpp       AccelStepper XY + homing
│   ├── gcode_parser.h/cpp  G-code interpreter
│   ├── hpgl_parser.h/cpp   HPGL interpreter
│   ├── usb_drive.h/cpp     Native USB OTG MSC driver + FAT32 reader + streaming read
│   ├── psram_buffer.h/cpp  PSRAM file buffer (4 MB pool, overflow error)
│   ├── svg_parser.h/cpp    SVG → G-code converter
│   ├── wifi_server.h/cpp   Async web server + WebSocket
│   ├── display.h/cpp       SSD1322 OLED — Plotter status view + menu + error + FW progress
│   └── menu.h/cpp          Plotter-style menu (Browse USB, Settings, About, FW confirm)
├── platformio.ini          PlatformIO config, PSRAM, dependencies
├── AGENTS.md               Session context, architecture notes, open issues
└── README.md
```

## Dependencies

- [ESPAsyncWebServer-esphome](https://github.com/esphome/ESPAsyncWebServer-esphome)
- [U8g2](https://github.com/olikraus/u8g2)

## License

MIT
