#pragma once
#include <cstddef>
#include <cstring>
#include <vector>

struct Preferences {
    std::vector<unsigned char> bytes;
    bool failWrite = false;
    bool begin(const char*, bool) { return true; }
    bool isKey(const char*) { return !bytes.empty(); }
    bool remove(const char*) { bytes.clear(); return true; }
    std::size_t getBytesLength(const char*) { return bytes.size(); }
    std::size_t getBytes(const char*, void* target, std::size_t count) {
        if (count < bytes.size()) return 0;
        memcpy(target, bytes.data(), bytes.size());
        return bytes.size();
    }
    std::size_t putBytes(const char*, const void* value, std::size_t count) {
        if (failWrite) return 0;
        const auto* first = static_cast<const unsigned char*>(value);
        bytes.assign(first, first + count);
        return count;
    }
};
