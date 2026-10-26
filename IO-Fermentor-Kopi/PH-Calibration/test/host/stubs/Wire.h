#pragma once
#include <cstdint>
#include <vector>

struct FakeWire {
    std::vector<uint8_t> writes;
    std::vector<uint32_t> starts;
    bool online = true, ready = true;
    uint8_t reg = 0, byteIndex = 0;
    uint16_t word = 0;
    int16_t adc = 13856; // 1732.000 mV
    bool begin(uint8_t, uint8_t, uint32_t) { return true; }
    void setTimeOut(unsigned) {}
    void beginTransmission(uint8_t) { writes.clear(); }
    void write(uint8_t value) { writes.push_back(value); }
    unsigned endTransmission(bool = true) {
        if (!online) return 2;
        if (!writes.empty()) reg = writes.front();
        if (writes.size() == 3) starts.push_back(micros());
        return 0;
    }
    unsigned requestFrom(uint8_t, uint8_t count) {
        byteIndex = 0;
        word = reg == 1 ? (ready ? 0xC383 : 0x4383) : uint16_t(adc);
        return online ? count : 0;
    }
    unsigned read() { return byteIndex++ == 0 ? word >> 8 : word & 0xFF; }
};
static FakeWire Wire;
