#include "display.h"
#include "menu.h"
#include <string.h>

void PlotterDisplay::begin() {
    _u8g2.begin();
    _u8g2.setContrast(128);
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_profont22_tf);
    _u8g2.drawStr(0, 20, "Plotter");
    _u8g2.setFont(u8g2_font_profont17_tf);
    _u8g2.drawStr(0, 40, "Booting...");
    _u8g2.sendBuffer();
}

void PlotterDisplay::update() {
    unsigned long now = millis();
    if (now - _lastRefresh < 250) return;
    _lastRefresh = now;

    if (_fwUpdateMode) {
        _u8g2.clearBuffer();
        _u8g2.setFont(u8g2_font_profont15_tf);
        _u8g2.drawStr(4, 16, "FW UPDATE");
        _u8g2.setFont(u8g2_font_profont10_tf);
        _u8g2.drawStr(4, 32, _fwUpdateMsg);
        // Progress bar
        _u8g2.drawFrame(4, 42, 120, 12);
        int barW = (_fwUpdatePercent * 118) / 100;
        if (barW > 0) _u8g2.drawBox(5, 43, barW, 10);
        snprintf(_buf, sizeof(_buf), "%d%%", _fwUpdatePercent);
        // Center percentage under bar
        _u8g2.drawStr(56, 62, _buf);
        _u8g2.sendBuffer();
        return;
    }

    if (_errorMode) {
        // Auto-clear error after 5 seconds
        if (now - _errorStart > 5000) {
            _errorMode = false;
        } else {
            // Keep showing error screen
            return;
        }
    }

    if (_menuMode) return; // menu draws itself via drawMenu()

    _u8g2.clearBuffer();
    drawPlotterStatus();
    _u8g2.sendBuffer();
}

// ─── Plotter-style status view ───────────────────────────────
// Layout (128×64, profont10 = ~25 chars × 6 lines):
//   Line 0:  [Land] [MixN] [Qty] [FPG] [FTL] [AF]   Sound ON PSv ON
//   Line 1:  X:+0.0 Y:+0.0            Size: 1.00"
//   Line 2:  Speed [▣▣▣▣▣]   Press [▣▣▣▣▣]
//   Line 3:  MC:Off CP:Off LR:Off Flip:Off
//   Line 4:  Qty:1 Cuts:1 Snd:ON  Lang:EN Unit:in

void PlotterDisplay::drawPlotterStatus() {
    _u8g2.setFont(u8g2_font_profont10_tf);

    // ── Line 0: Mode bar + Sound/PSaver state ──
    drawModeBar();

    // ── Line 1: Position + Size ──
    snprintf(_buf, sizeof(_buf), "X:%+.1f Y:%+.1f", _x, _y);
    _u8g2.drawStr(0, LINE_POS_SIZE, _buf);
    snprintf(_buf, sizeof(_buf), "S:%.2f\"", (double)_sizeInches);
    _u8g2.drawStr(80, LINE_POS_SIZE, _buf);

    // ── Line 2: Speed bars + Pressure bars ──
    _u8g2.drawStr(0, LINE_SPEED_PRS, "Speed");
    drawBars(36, LINE_SPEED_PRS, _speedLevel, SPEED_BARS, true);
    _u8g2.drawStr(68, LINE_SPEED_PRS, "Press");
    drawBars(104, LINE_SPEED_PRS, _pressureLevel, PRESSURE_BARS, true);

    // ── Line 3: Function toggles ──
    if (_cs) {
        const PlotterFunctions& f = _cs->funcs;
        const char* mc = (f.multiCut == 0) ? "Off" :
                         (f.multiCut == 1) ? "2x" :
                         (f.multiCut == 2) ? "3x" : "4x";
        snprintf(_buf, sizeof(_buf), "MC:%s CP:%s LR:%s F:%s",
                 mc,
                 f.centerPoint ? "On" : "Off",
                 f.lineReturn  ? "On" : "Off",
                 f.flip        ? "On" : "Off");
        _u8g2.drawStr(0, LINE_FUNCTIONS, _buf);
    }

    // ── Line 4: Status line ──
    if (_cs) {
        snprintf(_buf, sizeof(_buf), "Qty:%d Snd:%s Lang:%s Unit:%s",
                 _cs->quantityCopies,
                 _cs->soundOn ? "ON" : "OF",
                 LANG_LABELS[_cs->lang],
                 UNIT_LABELS[_cs->unit]);
        _u8g2.drawStr(0, LINE_STATUS, _buf);
    } else {
        snprintf(_buf, sizeof(_buf), "%s  P:%d%% Z:%.1fx", _state, _pressure, (double)_zoom);
        _u8g2.drawStr(0, LINE_STATUS, _buf);
    }
}

void PlotterDisplay::drawModeBar() {
    int x = 0;
    for (int i = 0; i <= MODE_AUTOFILL; i++) {
        bool active = _cs && _cs->mode == i;
        _u8g2.setDrawColor(active ? 1 : 0);
        if (active) {
            _u8g2.drawBox(x, LINE_MODE_BAR - FONT_SMALL_H + 1, strlen(MODE_LABELS[i]) * FONT_SMALL_W + 2, FONT_SMALL_H);
            _u8g2.setDrawColor(0);
        } else {
            _u8g2.setDrawColor(1);
        }
        _u8g2.drawStr(x + 1, LINE_MODE_BAR, MODE_LABELS[i]);
        x += strlen(MODE_LABELS[i]) * FONT_SMALL_W + 3;
    }
    _u8g2.setDrawColor(1);

    // Sound / Paper Saver state
    int rightX = 110;
    if (_cs) {
        _u8g2.drawStr(rightX - 25, LINE_MODE_BAR, _cs->soundOn ? "Snd" : "snd");
        _u8g2.drawStr(rightX - 10, LINE_MODE_BAR, _cs->funcs.paperSaver ? "PS" : "ps");
    }
}

void PlotterDisplay::drawBars(int x, int y, int level, int maxLevel, bool active) {
    for (int i = 0; i < maxLevel; i++) {
        int bx = x + i * (BAR_W + BAR_GAP);
        int by = y - BAR_Y_OFF - BAR_H;
        bool filled = (i < level);
        if (active && filled) {
            _u8g2.drawBox(bx, by, BAR_W, BAR_H);
            _u8g2.setDrawColor(0);
            _u8g2.drawBox(bx + 1, by + 1, BAR_W - 2, BAR_H - 2);
            _u8g2.setDrawColor(1);
        } else {
            _u8g2.drawFrame(bx, by, BAR_W, BAR_H);
        }
    }
}

void PlotterDisplay::drawSizeValue() {
    // size currently shown inline in line 1 of drawPlotterStatus()
}

// ─── Error display ──────────────────────────────────────────

void PlotterDisplay::showError(const char* msg) {
    _errorMode = true;
    _errorMsg = msg;
    _errorStart = millis();

    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_profont22_tf);
    _u8g2.drawStr(4, 24, "ERROR!");
    _u8g2.setFont(u8g2_font_profont15_tf);
    _u8g2.drawStr(4, 44, msg);
    _u8g2.sendBuffer();
}

void PlotterDisplay::clearError() {
    _errorMode = false;
    _errorMsg = nullptr;
}

void PlotterDisplay::showFwUpdate(const char* msg, int percent) {
    _fwUpdateMode = true;
    _fwUpdatePercent = constrain(percent, 0, 100);
    strncpy(_fwUpdateMsg, msg, sizeof(_fwUpdateMsg) - 1);
    _fwUpdateMsg[sizeof(_fwUpdateMsg) - 1] = '\0';
}

void PlotterDisplay::clearFwUpdate() {
    _fwUpdateMode = false;
    _fwUpdateMsg[0] = '\0';
    _fwUpdatePercent = 0;
}

void PlotterDisplay::drawMenu(PlotterMenu& menu) {
    _u8g2.clearBuffer();

    // Title bar
    _u8g2.setFont(u8g2_font_profont15_tf);
    snprintf(_buf, sizeof(_buf), " %s", menu.pageTitle());
    _u8g2.drawStr(0, 12, _buf);
    _u8g2.drawHLine(0, 14, 128);

    _u8g2.setFont(u8g2_font_profont17_tf);
    int visible = 4;
    int total = 0;

    switch (menu.currentPage()) {
        case MENU_MAIN: {
            const char* items[] = {"Browse SD", "Settings", "About"};
            total = 3;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 16;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 12, 128, 14);
                _u8g2.setDrawColor(sel ? 0 : 1);
                snprintf(_buf, sizeof(_buf), " %s", items[i]);
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_FILE_LIST: {
            total = menu.fileCount();
            int scroll = (menu.cursor() / visible) * visible;
            for (int i = 0; i < visible && (scroll + i) < total; i++) {
                int idx = scroll + i;
                int y = 32 + i * 16;
                bool sel = (idx == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 12, 128, 14);
                _u8g2.setDrawColor(sel ? 0 : 1);
                const char* name = menu.fileName(idx);
                _buf[0] = menu.isDir(idx) ? 'D' : ' ';
                strncpy(_buf + 1, name, 13);
                _buf[14] = '\0';
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            if (total > visible) {
                _u8g2.setFont(u8g2_font_profont10_tf);
                snprintf(_buf, sizeof(_buf), "%d/%d", menu.cursor() + 1, total);
                _u8g2.drawStr(100, 62, _buf);
            }
            break;
        }

        case MENU_FILE_INFO: {
            int idx = menu.cursor();
            _u8g2.drawStr(4, 30, menu.fileName(idx));
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 46, menu.isDir(idx) ? "[DIR]" : "[FILE]");
            _u8g2.drawStr(4, 60, "Select to plot");
            break;
        }

        case MENU_FILE_PLOT: {
            _u8g2.setFont(u8g2_font_profont22_tf);
            _u8g2.drawStr(4, 30, "Plotting...");
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 50, menu.fileName(menu.cursor()));
            break;
        }

        case MENU_SETTINGS_MAIN: {
            const char* items[] = {"Language", "Units", "Multi Cut", "Mat Size", "Char Images", "WiFi", "Calibrate"};
            int total = 7;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                _u8g2.drawStr(4, y, items[i]);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_LANG: {
            int total = 4;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                snprintf(_buf, sizeof(_buf), "%s %s", LANG_LABELS[i],
                         (_cs && _cs->lang == i) ? "*" : "");
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_UNITS: {
            int total = 4;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                snprintf(_buf, sizeof(_buf), "%s %s", UNIT_LABELS[i],
                         (_cs && _cs->unit == i) ? "*" : "");
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_MCUT: {
            const char* items[] = {"2 passes", "3 passes", "4 passes"};
            int total = 3;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                uint8_t cur = _cs ? _cs->funcs.multiCut : 0;
                snprintf(_buf, sizeof(_buf), "%s %s", items[i],
                         (cur > 0 && cur == i + 1) ? "*" : "");
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_MAT: {
            const char* items[] = {"12 x 12 in", "12 x 24 in"};
            int total = 2;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                snprintf(_buf, sizeof(_buf), "%s %s", items[i],
                         (_cs && _cs->matSize == i) ? "*" : "");
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_CHAR: {
            const char* items[] = {"Show", "Hide"};
            int total = 2;
            for (int i = 0; i < total; i++) {
                int y = 32 + i * 11;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 10, 128, 11);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                snprintf(_buf, sizeof(_buf), "%s %s", items[i],
                         (_cs && _cs->charImages == (i == 0)) ? "*" : "");
                _u8g2.drawStr(4, y, _buf);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_ABOUT: {
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawStr(4, 20, "Plotter Firmware");
            snprintf(_buf, sizeof(_buf), "v%s (build %d)", FIRMWARE_VERSION, FIRMWARE_BUILD);
            _u8g2.drawStr(4, 34, _buf);
            _u8g2.drawStr(4, 48, "ESP32-S3 + TMC2209");
            _u8g2.drawStr(4, 62, "S-Curve Motion");
            break;
        }

        case MENU_SETTINGS_WIFI: {
            const char* items[] = {"SSID", "Password", "Save"};
            int total = 3;
            for (int i = 0; i < total; i++) {
                int y = 24 + i * 14;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 11, 128, 13);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                _u8g2.drawStr(4, y - 2, items[i]);

                // Show current value
                if (_cs) {
                    char val[22] = "";
                    if (i == 0) {
                        strncpy(val, _cs->wifiSSID, 21);
                    } else if (i == 1) {
                        int len = strlen(_cs->wifiPass);
                        int show = len > 18 ? 18 : len;
                        memset(val, '*', show);
                        val[show] = '\0';
                    }
                    if (val[0]) {
                        _u8g2.drawStr(52, y - 2, val);
                    }
                }
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_WIFI_EDIT: {
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawStr(4, 20, menu.editingField() == 0 ? "SSID:" : "PASS:");
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 38, menu.editBuffer());

            // Show confirm hint
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawStr(4, 56, "OK=Done  BACK=Cancel");
            break;
        }

        case MENU_SETTINGS_CAL: {
            const char* items[] = {"Speed Cal", "Pressure Cal", "Reset Cal"};
            int total = 3;
            for (int i = 0; i < total; i++) {
                int y = 24 + i * 14;
                bool sel = (i == menu.cursor());
                if (sel) _u8g2.drawBox(0, y - 11, 128, 13);
                _u8g2.setDrawColor(sel ? 0 : 1);
                _u8g2.setFont(u8g2_font_profont10_tf);
                _u8g2.drawStr(4, y - 2, items[i]);
                _u8g2.setDrawColor(1);
            }
            break;
        }

        case MENU_SETTINGS_CAL_DO: {
            _u8g2.setFont(u8g2_font_profont10_tf);
            const char* label = (menu.calType == 0) ? "SPEED CAL" : "PRESS CAL";
            snprintf(_buf, sizeof(_buf), "%s  Step %d/5", label, menu.calStep + 1);
            _u8g2.drawStr(4, 20, _buf);
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 42, "Set detent & OK");
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawStr(4, 58, "OK=Read  BACK=Abort");
            break;
        }

        case MENU_FW_CONFIRM: {
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 24, "Update firmware");
            _u8g2.setFont(u8g2_font_profont15_tf);
            _u8g2.drawStr(4, 44, "from this file?");
            _u8g2.setFont(u8g2_font_profont10_tf);
            _u8g2.drawStr(4, 60, "OK=Yes  BACK=No");
            break;
        }

        case MENU_FW_PROGRESS:
            // Rendered by display.update() via showFwUpdate()
            break;

        default:
            break;
    }

    _u8g2.sendBuffer();
}
