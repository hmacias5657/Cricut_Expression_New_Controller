#include "menu.h"
#include "config.h"
#include "usb_drive.h"
#include <Preferences.h>

static PlotterMenu* gMenu = nullptr;
static bool gSavingPath = false;
static char gPendingPath[80] = "";
static Preferences prefs;

void PlotterMenu::begin() {
    gMenu = this;
    _page = MENU_MAIN;
    _cursor = 0;
    _scroll = 0;
    _editing = false;
}

void PlotterMenu::enterPage(MenuPage p) {
    _page = p;
    _cursor = 0;
    _scroll = 0;
}

const char* PlotterMenu::pageTitle() {
    switch (_page) {
        case MENU_MAIN:          return "MENU";
        case MENU_FILE_LIST:     return "FILES";
        case MENU_FILE_INFO:     return "INFO";
        case MENU_FILE_PLOT:     return "PLOT";
        case MENU_SETTINGS_MAIN: return "SETTINGS";
        case MENU_SETTINGS_LANG: return "LANGUAGE";
        case MENU_SETTINGS_UNITS:return "UNITS";
        case MENU_SETTINGS_MCUT: return "MULTI CUT";
        case MENU_SETTINGS_MAT:  return "MAT SIZE";
        case MENU_SETTINGS_CHAR: return "CHARS";
        case MENU_SETTINGS_WIFI: return "WIFI";
        case MENU_SETTINGS_WIFI_EDIT: return "WIFI EDIT";
        case MENU_SETTINGS_CAL:        return "CALIBRATE";
        case MENU_SETTINGS_CAL_DO:     return "CAL WIZARD";
        case MENU_FW_CONFIRM:    return "FW UPDATE";
        case MENU_FW_PROGRESS:   return "FW PROGRESS";
        case MENU_ABOUT:         return "ABOUT";
        default:                 return "";
    }
}

void PlotterMenu::up() {
    unsigned long now = millis();
    if (now - _lastBtn < BTN_DEBOUNCE_MS) return;
    _lastBtn = now;
    if (_cursor > 0) _cursor--;
    if (_cursor < _scroll) _scroll = _cursor;
}

void PlotterMenu::down() {
    unsigned long now = millis();
    if (now - _lastBtn < BTN_DEBOUNCE_MS) return;
    _lastBtn = now;
    int maxItems = 10;
    switch (_page) {
        case MENU_FILE_LIST:    maxItems = _fileCount; break;
        case MENU_SETTINGS_MAIN: maxItems = 7; break;
        case MENU_SETTINGS_WIFI: maxItems = 3; break;
        default:                maxItems = 10; break;
    }
    if (_cursor < maxItems - 1) _cursor++;
    if (_cursor > _scroll + 3) _scroll = _cursor - 3;
}

void PlotterMenu::back() {
    unsigned long now = millis();
    if (now - _lastBtn < BTN_DEBOUNCE_MS) return;
    _lastBtn = now;

    // Settings sub-pages go back to settings main
    switch (_page) {
        case MENU_FILE_LIST:
            if (_currentDir[0] != '\0') {
                leaveDir();
            } else {
                enterPage(MENU_MAIN);
            }
            break;
        case MENU_FILE_INFO:
        case MENU_FILE_PLOT:
            enterPage(MENU_MAIN);
            break;

        case MENU_SETTINGS_LANG:
        case MENU_SETTINGS_UNITS:
        case MENU_SETTINGS_MCUT:
        case MENU_SETTINGS_MAT:
        case MENU_SETTINGS_CHAR:
        case MENU_SETTINGS_WIFI:
        case MENU_SETTINGS_CAL:
            enterPage(MENU_SETTINGS_MAIN);
            break;

        case MENU_SETTINGS_WIFI_EDIT:
        case MENU_SETTINGS_CAL_DO:
            _editing = false;
            enterPage(MENU_SETTINGS_WIFI);
            break;

        case MENU_FW_CONFIRM:
        case MENU_FW_PROGRESS:
            enterPage(MENU_FILE_LIST);
            break;

        case MENU_SETTINGS_MAIN:
        case MENU_ABOUT:
            enterPage(MENU_MAIN);
            break;

        default:
            break;
    }
}

void PlotterMenu::select() {
    unsigned long now = millis();
    if (now - _lastBtn < BTN_DEBOUNCE_MS) return;
    _lastBtn = now;

    switch (_page) {
        case MENU_MAIN:
            switch (_cursor) {
                case 0: refreshFileList(); enterPage(MENU_FILE_LIST); break;
                case 1: enterPage(MENU_SETTINGS_MAIN); break;
                case 2: enterPage(MENU_ABOUT); break;
            }
            break;

        case MENU_FILE_LIST:
            if (_fileCount > 0 && _cursor < _fileCount) {
                if (_isDir[_cursor]) {
                    enterDir(_cursor);
                } else {
                    enterPage(MENU_FILE_INFO);
                }
            }
            break;

        case MENU_FILE_INFO: {
            const char* fname = _fileNames[_cursor];
            const char* dot = strrchr(fname, '.');
            bool isBin = dot && (strcasecmp(dot, ".bin") == 0);
            if (isBin) {
                enterPage(MENU_FW_CONFIRM);
            } else {
                enterPage(MENU_FILE_PLOT);
            }
            break;
        }

        case MENU_FILE_PLOT: {
            char path[64];
            snprintf(path, sizeof(path), "/%s", _fileNames[_cursor]);
            if (_plotCb) _plotCb(path);
            enterPage(MENU_MAIN);
            break;
        }

        // ── Settings Main (5 items from original manual + WiFi) ──
        case MENU_SETTINGS_MAIN:
            switch (_cursor) {
                case 0: enterPage(MENU_SETTINGS_LANG);  break;
                case 1: enterPage(MENU_SETTINGS_UNITS); break;
                case 2: enterPage(MENU_SETTINGS_MCUT);  break;
                case 3: enterPage(MENU_SETTINGS_MAT);   break;
                case 4: enterPage(MENU_SETTINGS_CHAR);  break;
                case 5: enterPage(MENU_SETTINGS_WIFI);  break;
                case 6: enterPage(MENU_SETTINGS_CAL);   break;
            }
            break;

        // ── Calibrate submenu ──
        case MENU_SETTINGS_CAL:
            if (_cs) {
                if (_cursor == 0) {
                    // Speed calibration wizard
                    calType = 0;
                    calStep = 0;
                    enterPage(MENU_SETTINGS_CAL_DO);
                } else if (_cursor == 1) {
                    // Pressure calibration wizard
                    calType = 1;
                    calStep = 0;
                    enterPage(MENU_SETTINGS_CAL_DO);
                } else if (_cursor == 2) {
                    resetCalibration();
                    enterPage(MENU_SETTINGS_CAL);
                }
            }
            break;

        // ── Calibration wizard ──
        case MENU_SETTINGS_CAL_DO: {
            int pin = (calType == 0) ? SPEED_PIN : POT_PIN;
            uint16_t val = (uint16_t)analogRead(pin);
            if (_cs) {
                if (calType == 0) {
                    _cs->calSpeed[calStep] = val;
                } else {
                    _cs->calPressure[calStep] = val;
                }
            }
            calStep++;
            if (calStep >= 5) {
                if (calType == 0) {
                    saveCalSpeed();
                } else {
                    saveCalPressure();
                }
                enterPage(MENU_SETTINGS_CAL);
            }
            break;
        }

        // ── Language selection ──
        case MENU_SETTINGS_LANG:
            if (_cs && _cursor <= LANG_DE) {
                _cs->lang = (PlotterLanguage)_cursor;
                saveSettings();
                enterPage(MENU_SETTINGS_MAIN);
            }
            break;

        // ── Units selection ──
        case MENU_SETTINGS_UNITS:
            if (_cs && _cursor <= UNIT_MM) {
                _cs->unit = (PlotterUnit)_cursor;
                saveSettings();
                enterPage(MENU_SETTINGS_MAIN);
            }
            break;

        // ── Multi Cut passes (2/3/4) ──
        case MENU_SETTINGS_MCUT:
            if (_cs) {
                _cs->funcs.multiCut = _cursor + 1;  // 1→2pass, 2→3pass, 3→4pass
                saveSettings();
                enterPage(MENU_SETTINGS_MAIN);
            }
            break;

        // ── Mat Size (12x12 / 12x24) ──
        case MENU_SETTINGS_MAT:
            if (_cs && _cursor <= MAT_12X24) {
                _cs->matSize = (PlotterMatSize)_cursor;
                saveSettings();
                enterPage(MENU_SETTINGS_MAIN);
            }
            break;

        // ── Character Images show/hide ──
        case MENU_SETTINGS_CHAR:
            if (_cs) {
                _cs->charImages = (_cursor == 0);  // 0=Show, 1=Hide
                saveSettings();
                enterPage(MENU_SETTINGS_MAIN);
            }
            break;

        // ── WiFi settings ──
        case MENU_SETTINGS_WIFI:
            if (_cs) {
                if (_cursor == 0) {
                    // Edit SSID
                    _editingField = 0;
                    strncpy(_editBuffer, _cs->wifiSSID, sizeof(_editBuffer) - 1);
                    _editBuffer[sizeof(_editBuffer) - 1] = '\0';
                    _editLen = strlen(_editBuffer);
                    _editing = true;
                    enterPage(MENU_SETTINGS_WIFI_EDIT);
                } else if (_cursor == 1) {
                    // Edit Password
                    _editingField = 1;
                    strncpy(_editBuffer, _cs->wifiPass, sizeof(_editBuffer) - 1);
                    _editBuffer[sizeof(_editBuffer) - 1] = '\0';
                    _editLen = strlen(_editBuffer);
                    _editing = true;
                    enterPage(MENU_SETTINGS_WIFI_EDIT);
                } else if (_cursor == 2) {
                    // Save
                    saveWiFi();
                    enterPage(MENU_SETTINGS_MAIN);
                }
            }
            break;

        // ── Firmware update confirmation ──
        case MENU_FW_CONFIRM: {
            char path[64];
            snprintf(path, sizeof(path), "/%s", _fileNames[_cursor]);
            if (_fwUpdateCb) _fwUpdateCb(path);
            enterPage(MENU_FW_PROGRESS);
            break;
        }

        case MENU_SETTINGS_WIFI_EDIT:
            // Confirm edit
            if (_cs) {
                if (_editingField == 0) {
                    strncpy(_cs->wifiSSID, _editBuffer, sizeof(_cs->wifiSSID) - 1);
                    _cs->wifiSSID[sizeof(_cs->wifiSSID) - 1] = '\0';
                } else {
                    strncpy(_cs->wifiPass, _editBuffer, sizeof(_cs->wifiPass) - 1);
                    _cs->wifiPass[sizeof(_cs->wifiPass) - 1] = '\0';
                }
            }
            _editing = false;
            enterPage(MENU_SETTINGS_WIFI);
            break;

        default:
            break;
    }
}

// Callback for USB drive enumeration — populates file list
static bool enumCallback(const USBFileEntry& entry, void* userData) {
    PlotterMenu* menu = (PlotterMenu*)userData;
    if (entry.name[0] == '.') return true;  // skip hidden
    menu->addFileEntry(entry);
    return true;
}

void PlotterMenu::addFileEntry(const USBFileEntry& entry) {
    if (_fileCount >= MAX_FILES) return;
    strncpy(_fileNames[_fileCount], entry.name, 63);
    _fileNames[_fileCount][63] = '\0';
    _isDir[_fileCount] = entry.isDir;
    _fileSizes[_fileCount] = entry.size;
    _fileCount++;
}

void PlotterMenu::refreshFileList() {
    _fileCount = 0;
    if (_usbDrive && _usbDrive->isReady()) {
        // If in a subdirectory, pass current dir path
        if (_currentDir[0] != '\0') {
            _usbDrive->enumerate(enumCallback, this, _currentDir);
        } else {
            _usbDrive->enumerate(enumCallback, this);
        }
    }
}

const char* PlotterMenu::fileName(int idx) {
    if (idx < 0 || idx >= _fileCount) return "";
    return _fileNames[idx];
}

bool PlotterMenu::isDir(int idx) {
    if (idx < 0 || idx >= _fileCount) return false;
    return _isDir[idx];
}

// ─── Text editing methods ─────────────────────────────────────

bool PlotterMenu::isEditing() {
    return _editing;
}

void PlotterMenu::feedChar(char c) {
    if (!_editing) return;
    if (_editLen < (int)sizeof(_editBuffer) - 1) {
        _editBuffer[_editLen++] = c;
        _editBuffer[_editLen] = '\0';
    }
}

void PlotterMenu::feedBackspace() {
    if (!_editing) return;
    if (_editLen > 0) {
        _editLen--;
        _editBuffer[_editLen] = '\0';
    }
}

void PlotterMenu::feedConfirm() {
    // Same action as select() on MENU_SETTINGS_WIFI_EDIT
    if (_editing && _page == MENU_SETTINGS_WIFI_EDIT) {
        select();
    }
}

void PlotterMenu::feedCancel() {
    if (_editing) {
        _editing = false;
        enterPage(MENU_SETTINGS_WIFI);
    }
}

// ─── Directory navigation ─────────────────────────────────────

void PlotterMenu::enterDir(int idx) {
    if (idx < 0 || idx >= _fileCount || !_isDir[idx]) return;

    char newDir[64];
    if (_currentDir[0] != '\0') {
        snprintf(newDir, sizeof(newDir), "%s/%s", _currentDir, _fileNames[idx]);
    } else {
        snprintf(newDir, sizeof(newDir), "/%s", _fileNames[idx]);
    }
    strncpy(_currentDir, newDir, sizeof(_currentDir) - 1);
    _currentDir[sizeof(_currentDir) - 1] = '\0';
    refreshFileList();
    _cursor = 0;
    _scroll = 0;
}

void PlotterMenu::leaveDir() {
    // Find parent directory path
    char* lastSlash = strrchr(_currentDir, '/');
    if (lastSlash == nullptr) {
        _currentDir[0] = '\0';
    } else if (lastSlash == _currentDir) {
        _currentDir[0] = '\0';
    } else {
        *lastSlash = '\0';
    }
    refreshFileList();
    _cursor = 0;
    _scroll = 0;
}

// ─── Settings persistence ─────────────────────────────────────

void PlotterMenu::saveSettings() {
    if (!_cs) return;
    prefs.begin("plotter", false);
    prefs.putUChar("lang", (uint8_t)_cs->lang);
    prefs.putUChar("unit", (uint8_t)_cs->unit);
    prefs.putUChar("matSize", (uint8_t)_cs->matSize);
    prefs.putUChar("charImg", _cs->charImages ? 1 : 0);
    prefs.end();
}

void PlotterMenu::loadSettings() {
    if (!_cs) return;
    prefs.begin("plotter", true);
    _cs->lang = (PlotterLanguage)prefs.getUChar("lang", (uint8_t)LANG_EN);
    _cs->unit = (PlotterUnit)prefs.getUChar("unit", (uint8_t)UNIT_IN_QUARTER);
    _cs->matSize = (PlotterMatSize)prefs.getUChar("matSize", (uint8_t)MAT_12X12);
    _cs->charImages = prefs.getUChar("charImg", 1) != 0;
    prefs.end();
}

// ─── WiFi save to NVS ─────────────────────────────────────────

void PlotterMenu::saveWiFi() {
    if (!_cs) return;
    prefs.begin("plotter", false);
    prefs.putString("wifi_ssid", _cs->wifiSSID);
    prefs.putString("wifi_pass", _cs->wifiPass);
    prefs.end();
    Serial.printf("// WiFi saved: SSID=%s\n", _cs->wifiSSID);
    // Mark that WiFi should be restarted; main loop will pick this up
    _cs->wifiChanged = true;
}
