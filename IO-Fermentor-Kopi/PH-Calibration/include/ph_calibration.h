#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ph {

constexpr float kMinMv = 50.0f;
constexpr float kMaxMv = 3000.0f; // DFRobot SEN0161-V2 signal output, not ADS full scale.
constexpr std::size_t kWindowSize = 30;
constexpr float kMaxSpreadMv = 5.0f;
constexpr float kMaxHalfDriftMv = 2.0f;
constexpr uint32_t kCaptureMinMs = 30000;
constexpr uint32_t kCaptureTimeoutMs = 180000;

inline bool validMv(float mv) {
    return std::isfinite(mv) && mv >= kMinMv && mv <= kMaxMv;
}

inline bool validBuffer(bool neutral, float value) {
    return std::isfinite(value) && (neutral ? (value >= 6.5f && value <= 7.5f)
                                                        : (value >= 3.5f && value <= 4.5f));
}

struct Calibration {
    uint32_t magic = 0x50484331;
    uint32_t version = 1;
    float neutralMv = 0;
    float acidMv = 0;
    float neutralPh = 7.00f;
    float acidPh = 4.00f;

    bool valid() const {
        // Module polarity varies: voltage may rise or fall toward the acid buffer.
        // The span guard rejects identical/near points; not a probe certification.
        return magic == 0x50484331 && version == 1 && validMv(neutralMv) &&
               validMv(acidMv) && validBuffer(true, neutralPh) &&
               validBuffer(false, acidPh) && std::fabs(acidMv - neutralMv) >= 50.0f;
    }

    float convert(float mv) const {
        if (!valid() || !validMv(mv)) return NAN;
        const float result = neutralPh + (mv - neutralMv) * (acidPh - neutralPh) /
                                            (acidMv - neutralMv);
        return std::isfinite(result) && result >= 0.0f && result <= 14.0f ? result : NAN;
    }
};

// Display-only fallback. Never saved or used as proof of buffer calibration.
// Same original example points as the main project, WITHOUT its -1.10 offset.
inline float estimatePh(float mv) {
    Calibration example;
    example.neutralMv = 1500.0f;
    example.acidMv = 2032.44f;
    return example.convert(mv);
}

struct Stability {
    float mean = NAN;
    float spread = NAN;
    float halfDrift = NAN;
    bool stable = false;
};

class Window {
public:
    void clear() { count_ = next_ = 0; }
    std::size_t count() const { return count_; }

    void push(float mv) {
        if (!validMv(mv)) {
            clear();
            return;
        }
        values_[next_] = mv;
        next_ = (next_ + 1) % kWindowSize;
        if (count_ < kWindowSize) ++count_;
    }

    Stability stats() const {
        Stability out;
        if (!count_) return out;
        float sum = 0, low = kMaxMv, high = kMinMv, first = 0, last = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const float value = values_[(count_ == kWindowSize ? next_ + i : i) % kWindowSize];
            sum += value;
            if (value < low) low = value;
            if (value > high) high = value;
            if (i < kWindowSize / 2) first += value;
            else last += value;
        }
        out.mean = sum / count_;
        out.spread = high - low;
        if (count_ == kWindowSize) {
            out.halfDrift = (last - first) / (kWindowSize / 2);
            out.stable = out.spread <= kMaxSpreadMv && std::fabs(out.halfDrift) <= kMaxHalfDriftMv;
        }
        return out;
    }

private:
    float values_[kWindowSize] = {};
    std::size_t count_ = 0, next_ = 0;
};

// A fresh calibration session must capture BOTH points before it may be saved.
// The previously saved calibration is not modified by capture or cancel.
class Session {
public:
    Calibration pending;
    Window window;
    bool haveNeutral = false, haveAcid = false;

    bool start(bool neutral, float bufferPh, uint32_t now) {
        if (capturing_ || !validBuffer(neutral, bufferPh)) return false;
        neutral_ = neutral;
        bufferPh_ = bufferPh;
        started_ = now;
        capturing_ = true;
        if (neutral) haveNeutral = false;
        else haveAcid = false;
        window.clear();
        return true;
    }

    // Load previously recorded points instead of capturing; still requires save.
    bool setManual(const Calibration& points) {
        if (capturing_ || !points.valid()) return false;
        pending = points;
        haveNeutral = haveAcid = true;
        return true;
    }

    bool capturing() const { return capturing_; }
    void cancel() { *this = Session{}; }
    bool ready() const { return !capturing_ && haveNeutral && haveAcid && pending.valid(); }
    bool expired(uint32_t now) const {
        return capturing_ && uint32_t(now - started_) >= kCaptureTimeoutMs;
    }

    bool captureIfStable(uint32_t now) {
        if (!capturing_ || expired(now) || uint32_t(now - started_) < kCaptureMinMs) return false;
        const Stability stability = window.stats();
        if (!stability.stable) return false;
        if (neutral_) {
            pending.neutralMv = stability.mean;
            pending.neutralPh = bufferPh_;
            haveNeutral = true;
        } else {
            pending.acidMv = stability.mean;
            pending.acidPh = bufferPh_;
            haveAcid = true;
        }
        capturing_ = false;
        return true;
    }

private:
    bool capturing_ = false, neutral_ = false;
    float bufferPh_ = 0;
    uint32_t started_ = 0;
};

} // namespace ph
