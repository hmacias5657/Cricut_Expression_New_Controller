#pragma once
#include <stdint.h>
#include <stddef.h>

class PSRAMBuffer {
public:
    bool    begin();
    void    clear();

    bool    write(const uint8_t* data, size_t len);
    size_t  read(size_t offset, uint8_t* buf, size_t len);
    uint8_t at(size_t offset);

    bool    isFull()        { return _full; }
    bool    hasError()      { return _error; }
    bool    isReady()       { return _ready; }
    size_t  size()          { return _size; }
    size_t  capacity()      { return _capacity; }
    size_t  remaining()     { return _capacity - _size; }
    uint8_t* buffer()       { return _buf; }

    void    setError(const char* msg);
    const char* errorMsg()  { return _errorMsg; }

    // Allocate a large PSRAM pool (most of the 8 MB)
    static const size_t DEFAULT_CAPACITY = 4 * 1024 * 1024;  // 4 MB default

private:
    uint8_t* _buf{nullptr};
    size_t   _size{0};
    size_t   _capacity{0};
    bool     _ready{false};
    bool     _full{false};
    bool     _error{false};
    const char* _errorMsg{nullptr};
};
