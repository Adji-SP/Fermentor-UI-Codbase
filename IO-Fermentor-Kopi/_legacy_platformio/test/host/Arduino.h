#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

constexpr int LOW = 0, HIGH = 1, OUTPUT = 1, SERIAL_8N1 = 0;
namespace fake {
inline uint32_t now = 0;
inline int pwm[48] = {};
inline int digital[48] = {};
}
inline uint32_t millis() { return fake::now; }
inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t pin, int value) { fake::digital[pin] = value; }
inline void analogWrite(uint8_t pin, int value) { fake::pwm[pin] = value; }

struct FakeSerial {
    std::deque<uint8_t> rx;
    std::string tx;
    int writable = 128;
    void begin(uint32_t, int = 0, int = 0, int = 0) {}
    int available() const { return static_cast<int>(rx.size()); }
    int availableForWrite() const { return writable; }
    int read() { int value = rx.front(); rx.pop_front(); return value; }
    void print(const char *text) { tx += text; }
    void println(const char *text) { tx += text; tx += '\n'; }
    size_t write(uint8_t byte) { tx += static_cast<char>(byte); return 1; }
    void printf(const char *format, ...) {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        tx += buffer;
    }
};
inline FakeSerial Serial, Serial1;
