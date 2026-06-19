# PROJECTS — Per-Project Registry

## esp32-gcode-plotter

### Commands
| Action    | Command                                  |
|-----------|------------------------------------------|
| Build     | `~/.platformio/penv/bin/pio run`         |
| Test      | `~/.platformio/penv/bin/pio test`        |
| Upload    | `~/.platformio/penv/bin/pio run -t upload`|
| Monitor   | `~/.platformio/penv/bin/pio device monitor`|

### Paths
| Key        | Value                                           |
|------------|-------------------------------------------------|
| Root       | `/Users/mariacabrera/Documents/TestVScode/C_Cut_E-plotter` |
| Source     | `src/`                                          |
| Headers    | `include/`                                      |
| Tests      | `test/`                                         |
| Scripts    | `scripts/`                                      |
| Config     | `src/config.h`                                  |
| Version    | `version.txt`                                   |
| Build out  | `.pio/build/esp32s3dev/`                        |

### Language & Framework
| Field     | Value                              |
|-----------|------------------------------------|
| Language  | C++ (Arduino framework on ESP-IDF) |
| Platform  | espressif32                        |
| Board     | esp32-s3-devkitc-1 (8MB flash + 8MB PSRAM) |
| Build sys | PlatformIO                         |

### Convention File
- `AGENTS.md` — session context, architecture notes, open issues

### Verification Ladder
| Level | Check                  | Command          |
|-------|------------------------|------------------|
| 1     | Compile (always)       | `pio run`        |
| 2     | Unit tests (if avail.) | `pio test`       |

> No linter configured for Arduino C++; rely on compiler warnings (Level 1).

### Permission Allowlist
```
Bash(pio run:*)
Bash(pio test:*)
Bash(pio run -t upload:*)
Bash(git status:*)
Bash(git diff:*)
Bash(git add:*)
Bash(git commit:*)
Bash(git log:*)
Bash(git revert:*)
Bash(git branch:*)
```

### Never Allowlist (always prompt user)
- `git push`
- `git reset --hard`
- `rm -rf`
- force flags
- package publish
- deploy

### Pin Table
| GPIO | Function              | Notes                        |
|------|-----------------------|------------------------------|
| 12   | X_STEP                |                              |
| 13   | X_DIR                 |                              |
| 14   | Y_STEP                |                              |
| 27   | Y_DIR                 |                              |
| 26   | ENABLE                | Active low, both drivers     |
| 34   | ENDSTOP               |                              |
| 32   | SOLENOID              | LEDC PWM, 5 kHz, 8-bit      |
| 35   | POT (pressure)        | ADC, 10 kΩ                  |
| 5    | SPEED_PIN             | ADC, 10 kΩ                  |
| 37   | STOP Button           | Dedicated GPIO (not in matrix)|
| 19   | USB_DN                | USB OTG (fixed, no config)  |
| 20   | USB_DP                | USB OTG (fixed, no config)  |
| 15   | OLED_CS               | SPI CS                       |
| 2    | OLED_DC               | Data/Command                 |
| 4    | OLED_RST / KBD_ROW4   | Saved/restored per scan      |
| 16   | OLED_MOSI             | SPI MOSI                     |
| 17   | OLED_SCK              | SPI SCK                      |
| 33   | KBD_CLK / BTN_UP      | Output when KBD enabled      |
| 25   | KBD_DATA / BTN_DOWN   | Output when KBD enabled      |
| 22   | KBD_ROW2 / BTN_SELECT | Input when KBD enabled       |
| 21   | KBD_ROW3 / BTN_BACK   | Input when KBD enabled       |
| 36   | KBD_ROW0              | ADC1 input-only              |
| 39   | KBD_ROW1              | ADC1 input-only              |
| 18   | ENC_A                 | Quadrature encoder A         |
| 23   | ENC_B                 | Quadrature encoder B         |

### Build Budget
| Resource | Limit   | Current          |
|----------|---------|------------------|
| RAM      | ≤ 60 KB | ~52 KB (16.0%)   |
| Flash    | ≤ 1 MB  | ~878 KB (26.3%)  |
| PSRAM    | 8 MB    | Used for file buffers |

### Versioning
| Field           | Value                                           |
|-----------------|-------------------------------------------------|
| Source of truth | `version.txt` (semver + build counter)          |
| Injection       | `scripts/versioning.py` (PlatformIO extra_script)|
| Runtime         | `config.h` FIRMWARE_VERSION + `build_version.h` FIRMWARE_BUILD |
| Filename        | `firmware_v{VER}_b{BUILD}.bin`                  |

### Dev/Release Distinction
- Release: clean semver (e.g. `1.0.0`)
- Dev: append `-dev` or `-<branch>` (e.g. `1.1.0-dev`, `1.1.0-feat-hpgl`)
- Build numbers always increment regardless of suffix

### Commit Message Format
```
<scope>: <imperative verb> <what changed>

- <file>: <what and why>
- <file>: <what and why>
```

Example:
```
esp32-plotter: Add HPGL bounding box scanner
- src/hpgl_parser.cpp: Add scanHPGLBoundingBox() for PA/PD/PU coordinate extraction
- src/main.cpp: Integrate HPGL bbox into scanBoundingBox() fallback path
```

### Branch Policy
- Default branch: `main`
- Medium/Large work: create feature branch `feat/<name>` from main
- Trivial/Small: may commit directly to main
- Never amend commits — always fresh commits
- Never force-push

### Dirty-Repo Protocol
1. At start of any work: run `git status`
2. If dirty: report uncommitted changes to user, ask before proceeding
3. If clean: proceed with work
4. Pre-existing untracked files: leave unstaged, warn orchestrator, don't block
5. Pre-commit hooks: run first; on failure report, never `--no-verify`

### Gate Rules
1. Build must pass before test: `pio run`
2. Test must pass before verify: `pio test`
3. Verify must pass before commit
4. Commit must succeed before docs
5. Never commit red (test failure)

### Rollback Procedures
| Scenario                  | Command                                |
|---------------------------|----------------------------------------|
| Single bad commit         | `git revert <sha>`                     |
| Range of commits          | `git revert <old-sha>..<new-sha>`      |
| Discard uncommitted       | `git reset --hard <known-good-sha>`    |
| Always verify build after rollback | `pio run`                     |

### Speed Principles
- Explore once per session; cache results for all work units
- Fail fast: build/test before verify/commit effort
- Parallel at project level; single coder per project
- Mechanical steps (build, lint, git) run inline — not as subagents

### Artifact Envelope
- Use for Medium/Large tasks (multi-module, parallelizable)
- Skip for Trivial/Small tasks
- Template: see `envelope_template.md`
- Orchestrator passes envelope to each subagent; subagent returns it augmented