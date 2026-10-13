#pragma once

#include <stddef.h>
#include <stdint.h>

// Native Nextion frames and the earlier newline-terminated ASCII commands.
// Fixed-length binary payloads may contain LF or FF; neither ends that payload.
namespace nextion_wire {
enum class Kind : uint8_t { NONE, TOUCH, PAGE, NUMBER, READY, ASCII };
struct Event {
    Kind kind = Kind::NONE;
    uint8_t page = 0;
    uint8_t component = 0;
    bool pressed = false;
    uint32_t number = 0;
    const char *text = nullptr;
};

class Decoder {
public:
    void reset() {
        mode_ = Mode::IDLE;
        length_ = expected_ = terminators_ = 0;
    }

    Event push(uint8_t byte) {
        Event event;
        if (mode_ == Mode::BINARY) {
            buffer_[length_++] = byte;
            if (length_ < expected_) return event;
            const bool valid = buffer_[expected_ - 3] == 0xFF &&
                               buffer_[expected_ - 2] == 0xFF &&
                               buffer_[expected_ - 1] == 0xFF;
            if (!valid) {
                mode_ = Mode::DISCARD_BINARY;
                terminators_ = 0;
                for (uint8_t i = length_; i > 0 && buffer_[i - 1] == 0xFF; --i)
                    ++terminators_;
                length_ = 0;
                return event;
            }
            switch (buffer_[0]) {
                case 0x65:
                    if (buffer_[3] <= 1) {
                        event.kind = Kind::TOUCH;
                        event.page = buffer_[1];
                        event.component = buffer_[2];
                        event.pressed = buffer_[3] == 1;
                    }
                    break;
                case 0x66:
                    event.kind = Kind::PAGE;
                    event.page = buffer_[1];
                    break;
                case 0x71:
                    event.kind = Kind::NUMBER;
                    event.number = uint32_t(buffer_[1]) |
                        (uint32_t(buffer_[2]) << 8) |
                        (uint32_t(buffer_[3]) << 16) |
                        (uint32_t(buffer_[4]) << 24);
                    break;
                case 0x88: event.kind = Kind::READY; break;
            }
            reset();
            return event;
        }

        if (mode_ == Mode::DISCARD_BINARY || mode_ == Mode::DISCARD_ASCII) {
            if (byte == 0xFF) {
                if (++terminators_ == 3) reset();
            } else {
                terminators_ = 0;
                if (byte == '\n' && mode_ == Mode::DISCARD_ASCII) reset();
            }
            return event;
        }

        if (mode_ == Mode::ASCII) {
            if (byte == '\n') {
                buffer_[length_] = 0;
                event.kind = Kind::ASCII;
                event.text = reinterpret_cast<const char *>(buffer_);
                reset();
                return event;
            }
            if (byte == '\r') return event;
            if (byte < 32 || byte > 126 || length_ == sizeof(buffer_) - 1) {
                mode_ = Mode::DISCARD_ASCII;
                length_ = 0;
                terminators_ = byte == 0xFF ? 1 : 0;
                return event;
            }
            buffer_[length_++] = byte;
            return event;
        }

        if (byte == '\r' || byte == '\n') return event;
        switch (byte) {
            case 0x65: expected_ = 7; break;
            case 0x66: expected_ = 5; break;
            case 0x71: expected_ = 8; break;
            case 0x88: expected_ = 4; break;
            case 0x67: case 0x68: expected_ = 9; break;
            default: expected_ = 0; break;
        }
        if (expected_) {
            mode_ = Mode::BINARY;
            buffer_[0] = byte;
            length_ = 1;
        } else if (byte == 'M' || byte == 'D') {
            mode_ = Mode::ASCII;
            buffer_[0] = byte;
            length_ = 1;
        } else {
            // String responses and device status packets must be consumed
            // through FF FF FF, even if their body contains an ASCII command.
            mode_ = (byte >= 32 && byte <= 126 && byte != 0x70)
                        ? Mode::DISCARD_ASCII : Mode::DISCARD_BINARY;
            terminators_ = byte == 0xFF ? 1 : 0;
        }
        return event;
    }

private:
    enum class Mode : uint8_t { IDLE, ASCII, BINARY, DISCARD_ASCII, DISCARD_BINARY };
    Mode mode_ = Mode::IDLE;
    uint8_t buffer_[48] = {};
    uint8_t length_ = 0;
    uint8_t expected_ = 0;
    uint8_t terminators_ = 0;
};
} // namespace nextion_wire
