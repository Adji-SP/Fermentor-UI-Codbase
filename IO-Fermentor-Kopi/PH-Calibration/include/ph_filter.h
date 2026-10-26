#pragma once

#include <algorithm>
#include "ph_calibration.h"

namespace ph {

// Independent ~1-second blocks, not an EMA that can conceal slow drift.
constexpr std::size_t kBlockSamples = 64;
constexpr uint32_t kReadingIntervalUs = 15625; // 64 evenly spaced starts/second.
constexpr uint32_t kMaxStartLatenessUs = 4000;
constexpr float kRawNoiseWarningMv = 5.0f; // Diagnostic only, no longer a veto.
constexpr float kOutlierMadFactor = 6.0f;
constexpr float kMinOutlierDistanceMv = 5.0f;
constexpr std::size_t kMaxRejectedSamples = 6; // Fewer than 10% of 64 samples.
constexpr float kMaxRetainedStdDevMv = 25.0f;
constexpr float kMaxFilterShiftMv = 10.0f;

struct FilterResult {
    float rawMeanMv = NAN;
    float filteredMv = NAN;
    float rawSpreadMv = NAN;
    float retainedStdDevMv = NAN;
    float filterShiftMv = NAN;
    std::size_t rejected = 0;
    bool acceptable = false;
    const char* quality = "INVALID_BLOCK";
};

inline FilterResult filterBlock(const float* values, std::size_t count) {
    FilterResult result;
    if (!values || count != kBlockSamples) return result;
    float sorted[kBlockSamples], deviations[kBlockSamples], total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        // Never hide a bad ADC/module value by filtering it away.
        if (!validMv(values[i])) return result;
        sorted[i] = values[i];
        total += values[i];
    }
    std::sort(sorted, sorted + count);
    result.rawMeanMv = total / count;
    result.rawSpreadMv = sorted[count - 1] - sorted[0];
    const float median = (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
    for (std::size_t i = 0; i < count; ++i) deviations[i] = std::fabs(values[i] - median);
    std::sort(deviations, deviations + count);
    const float mad = (deviations[count / 2 - 1] + deviations[count / 2]) / 2;
    const float cutoff = std::max(kMinOutlierDistanceMv, kOutlierMadFactor * mad);
    float retainedTotal = 0;
    std::size_t retained = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::fabs(values[i] - median) <= cutoff) {
            retainedTotal += values[i];
            ++retained;
        }
    }
    result.rejected = count - retained;
    if (!retained) return result;
    result.filteredMv = retainedTotal / retained;
    float varianceSum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::fabs(values[i] - median) <= cutoff) {
            const float difference = values[i] - result.filteredMv;
            varianceSum += difference * difference;
        }
    }
    result.retainedStdDevMv = retained > 1 ? std::sqrt(varianceSum / (retained - 1)) : 0;
    result.filterShiftMv = result.filteredMv - result.rawMeanMv;
    if (result.rejected > kMaxRejectedSamples) result.quality = "TOO_MANY_OUTLIERS";
    else if (result.retainedStdDevMv > kMaxRetainedStdDevMv) result.quality = "EXCESSIVE_NOISE";
    else if (std::fabs(result.filterShiftMv) > kMaxFilterShiftMv) result.quality = "EXCESSIVE_FILTER_SHIFT";
    else {
        result.acceptable = true;
        result.quality = result.rawSpreadMv > kRawNoiseWarningMv ? "RAW_NOISE_WARNING" : "OK";
    }
    return result;
}

} // namespace ph
