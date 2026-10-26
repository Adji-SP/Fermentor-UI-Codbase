#pragma once
// Test-only host adapters. Never included in a PlatformIO firmware build.
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <deque>
#include <functional>

constexpr int LOW = 0, OUTPUT = 1;
static uint32_t fakeUs = 0;
inline uint32_t micros() { return fakeUs; }
inline uint32_t millis() { return fakeUs / 1000; }
inline void delay(uint32_t ms) { fakeUs += ms * 1000; }
inline void digitalWrite(uint8_t, int) {}
inline void pinMode(uint8_t, int) {}

struct FakeSerial {
    std::string output;
    std::deque<char> input;
    std::function<void()> onAvailable;
    void begin(unsigned) {}
    void setTxTimeoutMs(unsigned) {}
    explicit operator bool() const { return true; }
    int available() { if (onAvailable) onAvailable(); return int(input.size()); }
    int read() { const int value = input.front(); input.pop_front(); return value; }
    void print(const char* text) { output += text; }
    void print(char ch) { output += ch; }
    void println(const char* text) { output += std::string(text) + "\n"; }
    void printf(const char* format, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        output += buffer;
    }
    void queue(const std::string& text) { for (char ch : text) input.push_back(ch); }
};
static FakeSerial Serial;
