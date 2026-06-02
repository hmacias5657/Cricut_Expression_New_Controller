#pragma once
#include <Arduino.h>
#include <functional>

#include "plotter_ui.h"
#include "usb_drive.h"

enum MenuPage {
    MENU_MAIN,
    MENU_FILE_LIST,
    MENU_FILE_INFO,
    MENU_FILE_PLOT,
    MENU_SETTINGS_MAIN,
    MENU_SETTINGS_LANG,
    MENU_SETTINGS_UNITS,
    MENU_SETTINGS_MCUT,
    MENU_SETTINGS_MAT,
    MENU_SETTINGS_CHAR,
    MENU_SETTINGS_WIFI,
    MENU_SETTINGS_WIFI_EDIT,
    MENU_SETTINGS_CAL,
    MENU_SETTINGS_CAL_DO,
    MENU_FW_CONFIRM,
    MENU_FW_PROGRESS,
    MENU_ABOUT,
};

struct MenuItem {
    const char* label;
    void (*action)();
    MenuPage subPage;
};

class PlotterMenu {
public:
    using PlotCallback = std::function<void(const char* path)>;

    void begin();
    void up();
    void down();
    void select();
    void back();
    void update();

    MenuPage currentPage() { return _page; }
    int cursor() { return _cursor; }
    const char* pageTitle();

    // Plotter state link
    void setPlotterState(PlotterState* cs) { _cs = cs; }

    // USB drive link (replaces SD)
    void setUSBDrive(USBDrive* drv) { _usbDrive = drv; }

    // File browser state (populated from USB drive enumeration)
    int fileCount() { return _fileCount; }
    const char* fileName(int idx);
    bool isDir(int idx);

    // Callbacks
    void setPlotCb(PlotCallback cb) { _plotCb = cb; }

    // Firmware update callback
    using FwUpdateCallback = std::function<void(const char* path)>;
    void setFwUpdateCb(FwUpdateCallback cb) { _fwUpdateCb = cb; }

    // File browser — USB enumeration callback calls this
    static const int MAX_FILES = 64;
    void addFileEntry(const USBFileEntry& entry);

    // Text editing
    bool isEditing();
    void feedChar(char c);
    void feedBackspace();
    void feedConfirm();
    void feedCancel();
    const char* editBuffer() { return _editBuffer; }
    int editingField() { return _editingField; }

    // WiFi save
    void saveWiFi();

    // Firmware update callback
    FwUpdateCallback _fwUpdateCb;

    // Calibration wizard state (public for display access)
    int calStep{0};
    int calType{0};  // 0=speed, 1=pressure

private:
    MenuPage _page{MENU_MAIN};
    int _cursor{0};
    int _scroll{0};
    unsigned long _lastBtn{0};

    // File browser
    char _fileNames[MAX_FILES][64];
    bool _isDir[MAX_FILES];
    size_t _fileSizes[MAX_FILES];
    int _fileCount{0};
    PlotCallback _plotCb;

    // Plotter runtime state
    PlotterState* _cs{nullptr};

    // USB drive (replaces SD)
    USBDrive* _usbDrive{nullptr};

    // Text editing state
    bool _editing{false};
    int _editingField{0};   // 0=SSID, 1=Password
    char _editBuffer[65]{};
    int _editLen{0};

    // Calibration wizard state
    int _calStep{0};
    int _calType{0};  // 0=speed, 1=pressure

    void enterPage(MenuPage p);
    void refreshFileList();
    void drawMain();
    void drawFileList();
    void drawFileInfo();
    void drawSettings();
    void drawSettingsLang();
    void drawSettingsUnits();
    void drawSettingsMCut();
    void drawSettingsMat();
    void drawSettingsChar();
    void drawSettingsWiFi();
    void drawWifiEditor();
    void drawSettingsCal();
    void drawCalWizard();
    void drawAbout();
    void drawPlotStatus();
    void drawText(const char* text, int y, bool inverted = false);
    void drawLine(int y);
};

// Calibration functions (implemented in main.cpp)
void saveCalSpeed();
void saveCalPressure();
void resetCalibration();
