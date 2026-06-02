#pragma once
#include "config.h"
#include "plotter_ui.h"
#include <U8g2lib.h>

class PlotterMenu;

class PlotterDisplay {
public:
    void begin();
    void update();

    // Status mode
    void setState(const char* s) { _state = s; }
    void setPosition(float x, float y) { _x = x; _y = y; }
    void setPressure(int pct) { _pressure = pct; }
    void setZoom(float z) { _zoom = z; }
    void setIP(const char* ip) { _ip = ip; }

    // Plotter UI state link
    void setPlotterState(PlotterState* cs) { _cs = cs; }
    void setSpeedLevel(uint8_t l) { _speedLevel = l; }
    void setPressureLevel(uint8_t l) { _pressureLevel = l; }
    void setSizeInches(float s) { _sizeInches = s; }

    // Error mode
    void showError(const char* msg);
    bool isErrorMode() { return _errorMode; }
    void clearError();

    // Firmware update mode
    void showFwUpdate(const char* msg, int percent);
    void clearFwUpdate();
    bool isFwUpdateMode() { return _fwUpdateMode; }

    // Menu mode
    void showMenu(bool on) { _menuMode = on; }
    bool isMenuMode() { return _menuMode; }
    void drawMenu(PlotterMenu& menu);

private:
    U8G2_SSD1322_NHD_128X64_F_4W_SW_SPI _u8g2{U8G2_R0, OLED_SCK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RST};
    const char* _state{"IDLE"};
    const char* _ip{""};
    float _x{0}, _y{0};
    int _pressure{0};
    float _zoom{1.0f};
    uint8_t _speedLevel{3};
    uint8_t _pressureLevel{3};
    float _sizeInches{1.0f};
    unsigned long _lastRefresh{0};
    char _buf[20];
    bool _menuMode{false};
    bool _errorMode{false};
    const char* _errorMsg{nullptr};
    unsigned long _errorStart{0};

    // Firmware update overlay
    bool     _fwUpdateMode{false};
    char     _fwUpdateMsg[32]{0};
    int      _fwUpdatePercent{0};

    // Plotter runtime state (set by main.cpp)
    PlotterState* _cs{nullptr};

    // Plotter-style drawing helpers
    void drawPlotterStatus();
    void drawModeBar();
    void drawBars(int x, int y, int level, int maxLevel, bool active);
    void drawSizeValue();
};
