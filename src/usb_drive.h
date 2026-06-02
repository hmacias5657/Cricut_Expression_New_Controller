#pragma once
#include <Arduino.h>
#include "psram_buffer.h"

// File entry returned by listFiles()
struct USBFileEntry {
    char name[64];
    bool isDir;
    size_t size;
};

// Callback for directory enumeration
typedef bool (*USBEnumCallback)(const USBFileEntry& entry, void* userData);

class USBDrive {
public:
    bool    begin();
    bool    isReady() { return _ready; }

    // Enumerate root directory; calls cb for each entry
    bool    enumerate(USBEnumCallback cb, void* userData);

    // Load an entire file into a PSRAM buffer (supports /dir/file paths)
    bool    loadFile(const char* path, PSRAMBuffer& buf);

    // Quick check if a file exists (supports /dir/file paths)
    bool    exists(const char* path);

    // Print file listing to a stream
    bool    listFiles(Print& out, const char* dir = "/");

    // Create a directory in the root (e.g. "/GCODE")
    bool    makeDir(const char* path);

    // Write data to a file (supports /dir/file paths)
    bool    writeFile(const char* path, const uint8_t* data, size_t len);

    // Streaming file read (for firmware update)
    bool    openFile(const char* path);
    size_t  readFile(uint8_t* buf, size_t len);
    void    closeFile();
    size_t  currentFileSize() { return _streamOpen ? _streamFileSize : 0; }

private:
    bool _ready{false};
    unsigned long _lastPoll{0};

    // Streaming read state
    bool     _streamOpen{false};
    uint32_t _streamCluster{0};
    uint8_t  _streamSector{0};
    uint16_t _streamOffset{0};
    size_t   _streamRemaining{0};
    size_t   _streamFileSize{0};
};

// Must be called periodically from loop() to service USB host stack
void pollUSB();
