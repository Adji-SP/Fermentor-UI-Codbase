// Standalone pH measurement + two-buffer calibration. No image-based offset.
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "ph_calibration.h"
#include "ph_filter.h"

namespace {
constexpr uint8_t kSda = 8, kScl = 9, kAdsAddress = 0x48;
// OS=start, MUX=A0/GND, PGA=+/-4.096V, single-shot, 128 SPS, comparator off.
constexpr uint16_t kAdsConfig = 0xC383;
constexpr float kMvPerCount = 0.125f;
constexpr uint32_t kSampleMs = 1000;
constexpr char kNamespace[] = "ph-only-v1";
constexpr char kKey[] = "calibration";

Preferences storage;
ph::Calibration active;
ph::Session session;
bool storageReady = false, wireReady = false, csv = false;
uint32_t lastSample = 0;
// Commands that change capture/calibration invalidate any partly collected block.
uint32_t acquisitionEpoch = 0;
char command[80] = {};
std::size_t commandLength = 0;
bool commandOverflow = false;

struct Sample {
    ph::FilterResult filter;
    const char* error = nullptr;
};

void serviceSerial();

bool writeRegister(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(kAdsAddress);
    Wire.write(reg);
    Wire.write(uint8_t(value >> 8));
    Wire.write(uint8_t(value));
    return Wire.endTransmission() == 0;
}

bool readRegister(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(kAdsAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(kAdsAddress, uint8_t(2)) != 2) return false;
    value = uint16_t(Wire.read()) << 8;
    value |= uint16_t(Wire.read());
    return true;
}

Sample readPh() {
    Sample result;
    if (!wireReady) { result.error = "I2C_INIT_FAILED"; return result; }
    float values[ph::kBlockSamples];
    const uint32_t epoch = acquisitionEpoch;
    const uint32_t blockStart = micros();
    for (std::size_t i = 0; i < ph::kBlockSamples; ++i) {
        const uint32_t scheduled = blockStart + uint32_t(i) * ph::kReadingIntervalUs;
        while (int32_t(micros() - scheduled) < 0) {
            serviceSerial();
            if (epoch != acquisitionEpoch) { result.error = "ACQUISITION_RESTARTED"; return result; }
            delay(1);
        }
        // A timing gap invalidates this block; never catch up with a tight burst.
        if (uint32_t(micros() - scheduled) > ph::kMaxStartLatenessUs) {
            result.error = "ACQUISITION_TIMING_GAP";
            return result;
        }
        if (!writeRegister(0x01, kAdsConfig)) {
            result.error = "ADS1115_OFFLINE_OR_BUS_ERROR";
            return result;
        }
        const uint32_t started = millis();
        delay(9); // 128 SPS ~= 7.8 ms; still check the ready flag.
        uint16_t config = 0;
        while (true) {
            if (!readRegister(0x01, config)) { result.error = "I2C_READ_ERROR"; return result; }
            if ((config & 0x7FFF) != (kAdsConfig & 0x7FFF)) {
                result.error = "ADS_CONFIG_MISMATCH";
                return result;
            }
            if (config & 0x8000) break;
            if (uint32_t(millis() - started) >= 50) { result.error = "ADS_TIMEOUT"; return result; }
            delay(1);
        }
        uint16_t raw = 0;
        if (!readRegister(0x00, raw)) { result.error = "I2C_READ_ERROR"; return result; }
        const int16_t signedRaw = static_cast<int16_t>(raw);
        if (signedRaw == INT16_MAX || signedRaw <= 0) {
            result.error = "ADC_RAIL_OR_NONPOSITIVE_INPUT";
            return result;
        }
        values[i] = float(signedRaw) * kMvPerCount;
        if (!ph::validMv(values[i])) { result.error = "MODULE_VOLTAGE_OUT_OF_RANGE"; return result; }
    }
    result.filter = ph::filterBlock(values, ph::kBlockSamples);
    return result;
}

void printHelp() {
    Serial.println("# PH ONLY: ESP32-S3 | ADS1115 0x48 A0 | SDA=8 SCL=9, 100kHz");
    Serial.println("# Native USB Serial/JTAG by default; 115200 baud. Commands need Enter/newline.");
    Serial.println("# help / status / export / cal7 [7.00] / cal4 [4.00 or 4.01]");
    Serial.println("# save / cancel / reset YES / csv on / csv off");
    Serial.println("# setcal <neutral pH> <neutral mV> <acid pH> <acid mV>: load recorded points, then save");
    Serial.println("# Capture: fresh 30-second stable window, timeout 180 seconds. No automatic save.");
    Serial.println("# FILTER v2: 64 samples/~1s, median/MAD outlier rejection, then mean; raw noise stays visible.");
    Serial.println("# pH before save is ESTIMATE_UNCALIBRATED (example 1500.00/2032.44 mV points), NOT an accurate calibration.");
    Serial.println("# Use actual buffer pH at its measured temperature. No temperature compensation.");
    Serial.println("# No Wi-Fi, Nextion, other sensors or motor operation. No -1.10 offset.");
    Serial.println("# A steady signal does NOT prove correct pH or a connected/healthy probe.");
}

void printCalibration(const ph::Calibration& calibration, const char* label) {
    if (!calibration.valid()) {
        Serial.printf("# %s: UNCALIBRATED\n", label);
        return;
    }
    Serial.printf("# %s: pH %.3f at %.3f mV; pH %.3f at %.3f mV; slope %.7f pH/mV\n",
                  label, calibration.neutralPh, calibration.neutralMv,
                  calibration.acidPh, calibration.acidMv,
                  (calibration.acidPh - calibration.neutralPh) /
                      (calibration.acidMv - calibration.neutralMv));
}

void printStatus() {
    printCalibration(active, "ACTIVE (saved)");
    Serial.printf("# NVS=%s | capture=%s | new pH7=%s | new pH4=%s | ready to save=%s\n",
                  storageReady ? "OK" : "ERROR", session.capturing() ? "RUNNING" : "IDLE",
                  session.haveNeutral ? "CAPTURED" : "MISSING",
                  session.haveAcid ? "CAPTURED" : "MISSING", session.ready() ? "YES" : "NO");
    if (session.haveNeutral) Serial.printf("# Pending neutral: pH %.3f at %.3f mV\n", session.pending.neutralPh, session.pending.neutralMv);
    if (session.haveAcid) Serial.printf("# Pending acid: pH %.3f at %.3f mV\n", session.pending.acidPh, session.pending.acidMv);
}

bool parseBuffer(const char* token, float fallback, float& value) {
    if (!token) { value = fallback; return true; }
    errno = 0;
    char* end = nullptr;
    value = std::strtof(token, &end);
    return end != token && *end == '\0' && errno == 0 && std::isfinite(value);
}

void handleCommand(char* line) {
    char* context = nullptr;
    const char* name = strtok_r(line, " \t", &context);
    if (!name) return;
    if (!strcmp(name, "setcal")) {
        float values[4] = {};
        bool ok = true;
        for (float& value : values) {
            const char* token = strtok_r(nullptr, " \t", &context);
            ok = ok && token && parseBuffer(token, 0, value);
        }
        ph::Calibration manual;
        manual.neutralPh = values[0];
        manual.neutralMv = values[1];
        manual.acidPh = values[2];
        manual.acidMv = values[3];
        if (!ok || strtok_r(nullptr, " \t", &context) || !session.setManual(manual)) {
            Serial.println("# ERROR: usage setcal <neutral pH> <neutral mV> <acid pH> <acid mV>; points invalid or capture running");
            return;
        }
        Serial.println("# MANUAL points loaded (RAM only, not captured live). Check status, then save.");
        printStatus();
        return;
    }
    const char* arg = strtok_r(nullptr, " \t", &context);
    if (strtok_r(nullptr, " \t", &context)) { Serial.println("# ERROR: too many arguments"); return; }

    if (!strcmp(name, "cal7") || !strcmp(name, "cal4")) {
        const bool neutral = !strcmp(name, "cal7");
        float reference = 0;
        if (!parseBuffer(arg, neutral ? 7.00f : 4.00f, reference) ||
            !session.start(neutral, reference, millis())) {
            Serial.println("# ERROR: invalid buffer value or another capture is running; use cancel first");
            return;
        }
        ++acquisitionEpoch;
        Serial.printf("# Capturing pH %.3f. Keep probe in this buffer; wait for CAPTURED. Old saved calibration stays active.\n", reference);
        return;
    }
    if (!strcmp(name, "csv") && arg && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
        csv = !strcmp(arg, "on");
        Serial.println(csv ? "uptime_ms,raw_adc_mean,raw_millivolts,filtered_millivolts,ph,ph_estimate,status,raw_spread_mv,retained_sd_mv,rejected,filter_shift_mv,quality,window_spread_mv,window_half_drift_mv,stable"
                           : "# Human-readable output enabled");
        return;
    }
    if (!strcmp(name, "reset") && arg && !strcmp(arg, "YES")) {
        if (!storageReady || (storage.isKey(kKey) && !storage.remove(kKey))) {
            Serial.println("# ERROR: reset failed; active calibration unchanged");
            return;
        }
        active = ph::Calibration{};
        session.cancel();
        ++acquisitionEpoch;
        Serial.println("# Removed ONLY this program's saved calibration. Now UNCALIBRATED.");
        return;
    }
    if (arg) { Serial.println("# ERROR: unexpected argument; type help"); return; }
    if (!strcmp(name, "help")) printHelp();
    else if (!strcmp(name, "status")) printStatus();
    else if (!strcmp(name, "cancel")) {
        session.cancel();
        ++acquisitionEpoch;
        Serial.println("# Capture and both pending points cleared. Saved calibration unchanged.");
    } else if (!strcmp(name, "save")) {
        if (!session.ready()) {
            Serial.println("# ERROR: capture BOTH fresh buffers first. Acid and neutral mV must differ by at least 50 mV.");
            return;
        }
        if (!storageReady || storage.putBytes(kKey, &session.pending, sizeof(session.pending)) != sizeof(session.pending)) {
            Serial.println("# ERROR: NVS write failed; active calibration unchanged");
            return;
        }
        active = session.pending;
        session.cancel();
        ++acquisitionEpoch;
        printCalibration(active, "SAVED; active now and after restart");
        Serial.println("# Recheck fresh buffers and an independent sample. No offset or automatic drift correction is applied.");
    } else if (!strcmp(name, "export")) {
        printCalibration(active, "EXPORT");
        if (active.valid()) {
            Serial.printf("# pH = %.6f + (mV - %.6f) * (%.6f - %.6f) / (%.6f - %.6f)\n",
                          active.neutralPh, active.neutralMv, active.acidPh,
                          active.neutralPh, active.acidMv, active.neutralMv);
            Serial.println("# When transferring this calibration, remove any temporary -1.10 offset. Rust is not changed automatically.");
        }
    } else Serial.println("# Unknown command. Type help. Reset requires: reset YES");
}

void serviceSerial() {
    // Bounded input work; flooding the console cannot starve measurement forever.
    for (unsigned budget = 0; budget < 128 && Serial.available(); ++budget) {
        const char ch = char(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (commandOverflow) Serial.println("# ERROR: command too long; entire line discarded");
            else if (commandLength) { command[commandLength] = '\0'; handleCommand(command); }
            commandLength = 0;
            commandOverflow = false;
        } else if (ch == '\b' || ch == 127) {
            if (!commandOverflow && commandLength) --commandLength;
        } else if (!commandOverflow && ((ch >= 32 && ch <= 126) || ch == '\t')) {
            if (commandLength + 1 < sizeof(command)) command[commandLength++] = ch;
            else commandOverflow = true;
        }
    }
}

void reportSample(const Sample& sample) {
    const ph::Stability stability = session.window.stats();
    const ph::FilterResult& filtered = sample.filter;
    const bool calibrated = active.valid();
    const float value = sample.error ? NAN : calibrated ? active.convert(filtered.filteredMv)
                                                        : ph::estimatePh(filtered.filteredMv);
    const char* status = sample.error ? sample.error : !std::isfinite(value) ? "PH_OUT_OF_RANGE" :
                         calibrated ? "CALIBRATED" : "ESTIMATE_UNCALIBRATED";
    if (csv) {
        Serial.printf("%lu,%.2f,%.3f,%.3f,", (unsigned long)millis(),
                      filtered.rawMeanMv / kMvPerCount, filtered.rawMeanMv, filtered.filteredMv);
        // Estimates get their OWN column; never masquerade as calibrated CSV data.
        if (calibrated && std::isfinite(value)) Serial.printf("%.3f", value);
        Serial.print(',');
        if (!calibrated && std::isfinite(value)) Serial.printf("%.3f", value);
        Serial.printf(",%s,%.3f,%.3f,%u,%.3f,%s,%.3f,%.3f,%u\n", status,
                      filtered.rawSpreadMv, filtered.retainedStdDevMv, unsigned(filtered.rejected),
                      filtered.filterShiftMv, sample.error ? "INVALID_BLOCK" : filtered.quality,
                      stability.spread, stability.halfDrift, unsigned(stability.stable));
    } else {
        Serial.printf("pH: ");
        if (std::isfinite(value)) Serial.printf("%.3f", value);
        else Serial.print("--");
        Serial.printf(" | %s | filtered=%.3f mV | raw=%.3f mV ADC=%.2f | raw spread=%.3f mV SD=%.3f mV rejected=%u/64 shift=%.3f mV | %s | window=%u/30 spread=%.3f mV drift=%.3f mV | %s\n",
                      status, filtered.filteredMv, filtered.rawMeanMv, filtered.rawMeanMv / kMvPerCount,
                      filtered.rawSpreadMv, filtered.retainedStdDevMv, unsigned(filtered.rejected),
                      filtered.filterShiftMv, sample.error ? "INVALID_BLOCK" : filtered.quality, unsigned(session.window.count()),
                      stability.spread, stability.halfDrift, stability.stable ? "SHORT_TERM_STABLE" : "SETTLING/NOISY");
    }
}
} // namespace

void setup() {
    // Safeguard only: keep the existing bridge disabled; never generate PWM.
    // Disconnect motor power before flashing; boot/reset pin states need hardware pull-downs.
    for (const uint8_t pin : {uint8_t(5), uint8_t(4), uint8_t(7), uint8_t(6)}) {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }
    Serial.begin(115200);
#if PH_USE_NATIVE_USB
    Serial.setTxTimeoutMs(20);
#endif
    // Never wait indefinitely for a terminal connection.
    const uint32_t started = millis();
    while (!Serial && uint32_t(millis() - started) < 2000) delay(10);
    wireReady = Wire.begin(kSda, kScl, 100000);
    Wire.setTimeOut(50);
    storageReady = storage.begin(kNamespace, false);
    if (storageReady && storage.isKey(kKey)) {
        ph::Calibration loaded;
        if (storage.getBytesLength(kKey) == sizeof(loaded) &&
            storage.getBytes(kKey, &loaded, sizeof(loaded)) == sizeof(loaded) && loaded.valid()) {
            active = loaded;
        } else Serial.println("# ERROR: saved calibration invalid; ignoring it (UNCALIBRATED)");
    }
    printHelp();
    printStatus();
}

void loop() {
    serviceSerial();
    const uint32_t now = millis();
    if (session.expired(now)) {
        session.cancel();
        Serial.println("# ERROR: capture timed out; pending points discarded, saved calibration unchanged. Check probe/buffer/noise.");
    }
    if (uint32_t(now - lastSample) >= kSampleMs) {
        if (uint32_t(now - lastSample) > 2 * kSampleMs) session.window.clear();
        lastSample = now;
        const Sample sample = readPh();
        if (sample.error && !strcmp(sample.error, "ACQUISITION_RESTARTED")) {
            // A new cal/cancel/save command took effect mid-block. Discard old-liquid data.
            lastSample = millis() - kSampleMs;
            return;
        }
        // Judge independent filtered means over 30 seconds, not the raw peak-to-peak
        // spread. Keep rejecting bus faults, excessive noise and excessive filtering.
        if (sample.error || !sample.filter.acceptable) session.window.clear();
        else session.window.push(sample.filter.filteredMv);
        reportSample(sample);
        if (session.captureIfStable(millis())) {
            Serial.println("# CAPTURED (RAM only). Rinse, move to the other buffer and run its cal command, or save if both captured.");
            printStatus();
        }
    }
    delay(1);
}
