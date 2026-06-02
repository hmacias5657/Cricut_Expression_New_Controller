#include "psram_buffer.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

bool PSRAMBuffer::begin() {
    _capacity = DEFAULT_CAPACITY;
#if BOARD_HAS_PSRAM
    _buf = (uint8_t*)ps_malloc(_capacity);
#else
    _buf = (uint8_t*)malloc(_capacity);
#endif
    if (!_buf) {
        _ready = false;
        _error = true;
        _errorMsg = "PSRAM alloc failed";
        return false;
    }
    _ready = true;
    _error = false;
    _full = false;
    _size = 0;
    return true;
}

void PSRAMBuffer::clear() {
    _size = 0;
    _full = false;
    if (_error) {
        _error = false;
        _errorMsg = nullptr;
    }
}

bool PSRAMBuffer::write(const uint8_t* data, size_t len) {
    if (!_ready || _full) return false;
    if (_size + len > _capacity) {
        _full = true;
        _error = true;
        _errorMsg = "File too large for PSRAM";
        return false;
    }
    memcpy(_buf + _size, data, len);
    _size += len;
    return true;
}

size_t PSRAMBuffer::read(size_t offset, uint8_t* buf, size_t len) {
    if (!_ready || offset >= _size) return 0;
    size_t available = _size - offset;
    if (len > available) len = available;
    memcpy(buf, _buf + offset, len);
    return len;
}

uint8_t PSRAMBuffer::at(size_t offset) {
    if (!_ready || offset >= _size) return 0;
    return _buf[offset];
}

void PSRAMBuffer::setError(const char* msg) {
    _error = true;
    _errorMsg = msg;
}
