#include "../../include/ph_calibration.h"
#include "../../include/ph_filter.h"
#include <cassert>
#include <cstdio>

static void close(float actual, float expected) {
    assert(std::isfinite(actual) && std::fabs(actual - expected) < 0.0001f);
}

static void fill(ph::Window& window, float mv) {
    for (unsigned i = 0; i < ph::kWindowSize; ++i) window.push(mv);
}

int main() {
    close(ph::estimatePh(1500), 7.0f);
    close(ph::estimatePh(2032.44f), 4.0f);
    close(ph::estimatePh(1691.6784f), 5.92f);
    assert(std::isnan(ph::estimatePh(NAN)) && std::isnan(ph::estimatePh(0)));

    float block[ph::kBlockSamples];
    // Model the observed 38 mV peak-to-peak ripple, not a fabricated hardware replay.
    for (unsigned i = 0; i < ph::kBlockSamples; ++i) block[i] = 1732 + (i % 2 ? 19 : -19);
    ph::FilterResult filtered = ph::filterBlock(block, ph::kBlockSamples);
    assert(filtered.acceptable && filtered.rejected == 0);
    close(filtered.rawSpreadMv, 38);
    close(filtered.rawMeanMv, 1732);
    close(filtered.filteredMv, 1732);
    assert(filtered.retainedStdDevMv > 19 && filtered.retainedStdDevMv < 20);
    assert(!ph::filterBlock(block, 63).acceptable);
    assert(!ph::filterBlock(nullptr, ph::kBlockSamples).acceptable);

    // Isolated plausible spike can be filtered; raw diagnostics retain its effect.
    for (float& value : block) value = 1732;
    block[7] = 1932;
    filtered = ph::filterBlock(block, ph::kBlockSamples);
    assert(filtered.acceptable && filtered.rejected == 1);
    close(filtered.filteredMv, 1732);
    close(filtered.rawSpreadMv, 200);
    close(filtered.rawMeanMv, 1735.125f);
    // Never discard a rail/invalid input as if it were an ordinary outlier.
    for (float badValue : {NAN, INFINITY, -1.0f, 0.0f, 3001.0f}) {
        block[7] = badValue;
        assert(!ph::filterBlock(block, ph::kBlockSamples).acceptable);
    }
    for (float& value : block) value = 1732;
    for (unsigned i = 0; i < 7; ++i) block[i] += 50;
    filtered = ph::filterBlock(block, ph::kBlockSamples);
    assert(!filtered.acceptable && filtered.rejected == 7);
    for (float& value : block) value = 1732;
    for (unsigned i = 0; i < 6; ++i) block[i] += 200;
    filtered = ph::filterBlock(block, ph::kBlockSamples);
    assert(!filtered.acceptable && std::fabs(filtered.filterShiftMv) > 10);
    for (unsigned i = 0; i < ph::kBlockSamples; ++i) block[i] = 1732 + (i % 2 ? 60 : -60);
    assert(!ph::filterBlock(block, ph::kBlockSamples).acceptable);

    // Independent per-second filtering preserves a slow change; the capture gate rejects it.
    ph::Window filteredWindow;
    for (unsigned second = 0; second < 30; ++second) {
        for (unsigned i = 0; i < ph::kBlockSamples; ++i)
            block[i] = 1732 + 0.3f * second + (i % 2 ? 19 : -19);
        filtered = ph::filterBlock(block, ph::kBlockSamples);
        assert(filtered.acceptable);
        filteredWindow.push(filtered.filteredMv);
    }
    assert(!filteredWindow.stats().stable);
    assert(filteredWindow.stats().spread > 8);
    for (unsigned i = 0; i < ph::kBlockSamples; ++i) block[i] = i < 32 ? 1500 : 2032;
    assert(!ph::filterBlock(block, ph::kBlockSamples).acceptable); // Buffer transition.

    ph::Calibration cal;
    assert(!cal.valid() && std::isnan(cal.convert(1500)));
    cal.neutralMv = 1500;
    cal.acidMv = 2032.44f;
    assert(cal.valid());
    close(cal.convert(1500), 7.0f);
    close(cal.convert(2032.44f), 4.0f);
    close(cal.convert(1691.6784f), 5.92f); // NO temporary -1.10 correction.
    cal.acidPh = 4.01f;
    close(cal.convert(cal.acidMv), 4.01f);
    assert(std::isnan(cal.convert(NAN)) && std::isnan(cal.convert(INFINITY)));
    assert(std::isnan(cal.convert(0)) && std::isnan(cal.convert(3100)));
    assert(std::isnan(cal.convert(2999))); // Extrapolated pH out of range, never clamp.
    ph::Calibration bad = cal;
    bad.acidMv = bad.neutralMv;
    assert(!bad.valid());
    bad.acidMv = bad.neutralMv - 49;
    assert(!bad.valid());
    // Reversed polarity (voltage falls toward acid) is a valid calibration.
    ph::Calibration reversed;
    reversed.neutralMv = 1749.963f;
    reversed.neutralPh = 6.86f;
    reversed.acidMv = 1648.4f;
    assert(reversed.valid());
    close(reversed.convert(1749.963f), 6.86f);
    close(reversed.convert(1648.4f), 4.0f);
    assert(reversed.convert(1700) > 4.0f && reversed.convert(1700) < 6.86f);
    ph::Session manualSession;
    assert(!manualSession.setManual(bad));
    assert(manualSession.setManual(reversed) && manualSession.ready());
    manualSession.cancel();
    assert(manualSession.start(true, 7, 0));
    assert(!manualSession.setManual(reversed)); // Never overrides a running capture.
    bad = cal; bad.magic = 0;
    assert(!bad.valid());
    bad = cal; bad.version = 2;
    assert(!bad.valid());
    bad = cal; bad.neutralMv = NAN;
    assert(!bad.valid());
    assert(!ph::validBuffer(true, 4) && !ph::validBuffer(false, 7));

    ph::Window window;
    for (unsigned i = 0; i < 29; ++i) window.push(1500);
    assert(!window.stats().stable);
    window.push(1500);
    assert(window.stats().stable);
    close(window.stats().mean, 1500);
    window.push(1510);
    assert(!window.stats().stable);
    fill(window, 1500);
    assert(window.stats().stable); // Old outlier rolls out.
    window.push(NAN);
    assert(window.count() == 0 && !window.stats().stable);
    // Small full spread still rejected if directional half-window drift is too high.
    for (unsigned i = 0; i < 30; ++i) window.push(i < 15 ? 1500 : 1503);
    assert(window.stats().spread == 3 && !window.stats().stable);

    ph::Session session;
    assert(!session.ready());
    assert(session.start(true, 7, 100));
    assert(!session.start(false, 4, 100)); // Cannot silently replace a running capture.
    fill(session.window, 1500);
    assert(!session.captureIfStable(30099));
    assert(session.captureIfStable(30100));
    assert(session.haveNeutral && !session.ready());
    assert(session.start(false, 4.01f, 31000));
    assert(session.window.count() == 0); // No previous-buffer samples reused.
    fill(session.window, 2032.44f);
    assert(session.captureIfStable(61000));
    assert(session.ready());
    close(session.pending.convert(2032.44f), 4.01f);
    session.cancel();
    assert(!session.haveNeutral && !session.haveAcid && !session.ready());
    assert(!session.start(true, NAN, 0));
    assert(session.start(true, 7, 0));
    fill(session.window, 1500);
    assert(session.expired(180000));
    assert(!session.captureIfStable(180000));
    session.cancel();
    const uint32_t nearWrap = UINT32_MAX - 1000;
    assert(session.start(true, 7, nearWrap));
    fill(session.window, 1500);
    assert(session.captureIfStable(uint32_t(nearWrap + 30000))); // millis rollover.

    session.cancel();
    assert(session.start(true, 7, 0));
    for (unsigned second = 0; second < 30; ++second) {
        for (unsigned i = 0; i < ph::kBlockSamples; ++i) block[i] = 1732 + (i % 2 ? 19 : -19);
        filtered = ph::filterBlock(block, ph::kBlockSamples);
        assert(filtered.acceptable);
        session.window.push(filtered.filteredMv);
    }
    assert(session.captureIfStable(30000));
    close(session.pending.neutralMv, 1732);
    assert(!session.ready()); // Filtered pH7 capture alone still cannot be saved.
    puts("PASS: filter/ripple/spikes/drift, estimates, calibration, invalid data, sessions and rollover checks");
}
