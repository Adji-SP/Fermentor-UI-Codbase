// Exercise the actual firmware command handler, acquisition and serial formatter.
#include "../../src/main.cpp"
#include <cassert>
#include <sstream>
#include <vector>

static void commandLine(const char* text) {
    char buffer[100];
    std::snprintf(buffer, sizeof(buffer), "%s", text);
    handleCommand(buffer);
}

static std::vector<std::string> fields(const std::string& row) {
    std::vector<std::string> result;
    std::stringstream stream(row);
    std::string field;
    while (std::getline(stream, field, ',')) result.push_back(field);
    return result;
}

int main() {
    setup();
    assert(Serial.output.find("ESTIMATE_UNCALIBRATED") != std::string::npos);
    Serial.output.clear();
    const uint32_t start = micros();
    const Sample sample = readPh();
    assert(!sample.error && sample.filter.acceptable);
    assert(Wire.starts.size() == ph::kBlockSamples);
    assert(micros() - start >= 990000 && micros() - start < 1010000);
    for (std::size_t i = 0; i < Wire.starts.size(); ++i) {
        const uint32_t late = Wire.starts[i] - start - uint32_t(i) * ph::kReadingIntervalUs;
        assert(late < 1000);
    }
    reportSample(sample);
    assert(Serial.output.find("pH: 5.693 | ESTIMATE_UNCALIBRATED") != std::string::npos);
    assert(Serial.output.find("filtered=1732.000 mV | raw=1732.000 mV") != std::string::npos);
    commandLine("csv on");
    Serial.output.clear();
    reportSample(sample);
    auto row = fields(Serial.output);
    assert(row.size() == 15 && row[4].empty() && row[5] == "5.693");
    assert(row[6] == "ESTIMATE_UNCALIBRATED");

    // Capturing a point must not force the displayed pH to the buffer label.
    commandLine("cal7 7.00");
    assert(session.capturing());
    Serial.output.clear();
    reportSample(sample);
    assert(fields(Serial.output)[5] == "5.693");
    for (unsigned i = 0; i < 30; ++i) session.window.push(1732);
    fakeUs += 30000000;
    assert(session.captureIfStable(millis()));
    commandLine("save");
    assert(!active.valid() && !storage.isKey(kKey));
    commandLine("cal4 4.01");
    for (unsigned i = 0; i < 30; ++i) session.window.push(2264.44f);
    fakeUs += 30000000;
    assert(session.captureIfStable(millis()));
    storage.failWrite = true;
    commandLine("save");
    assert(!active.valid());
    storage.failWrite = false;
    commandLine("save");
    assert(active.valid() && storage.isKey(kKey));
    Serial.output.clear();
    reportSample(sample);
    row = fields(Serial.output);
    assert(row[4] == "7.000" && row[5].empty() && row[6] == "CALIBRATED");

    Wire.online = false;
    Sample fault = readPh();
    assert(fault.error);
    Serial.output.clear();
    reportSample(fault);
    row = fields(Serial.output);
    assert(row[4].empty() && row[5].empty());
    Wire.online = true;
    Wire.ready = false;
    fault = readPh();
    assert(fault.error && !std::strcmp(fault.error, "ADS_TIMEOUT"));
    Wire.ready = true;
    Wire.adc = 0;
    fault = readPh();
    assert(fault.error && !std::strcmp(fault.error, "ADC_RAIL_OR_NONPOSITIVE_INPUT"));
    Wire.adc = 13856;

    // Serial commands remain responsive inside a sample block; old-buffer data discarded.
    bool injected = false;
    Serial.onAvailable = [&]() {
        if (!injected) { injected = true; Serial.queue("cal7 7.00\n"); }
    };
    fault = readPh();
    assert(fault.error && !std::strcmp(fault.error, "ACQUISITION_RESTARTED"));
    assert(session.capturing() && session.window.count() == 0);
    Serial.onAvailable = nullptr;
    commandLine("cancel");
    assert(!session.capturing() && active.valid());

    // A delayed sample cannot be compressed into a fast catch-up burst.
    bool delayed = false;
    Serial.onAvailable = [&]() { if (!delayed) { delayed = true; fakeUs += 25000; } };
    fault = readPh();
    assert(fault.error && !std::strcmp(fault.error, "ACQUISITION_TIMING_GAP"));
    Serial.onAvailable = nullptr;
    fakeUs = UINT32_MAX - 200000;
    assert(!readPh().error); // microsecond rollover during acquisition.

    // Recorded points can be entered manually; they still need an explicit save.
    commandLine("setcal 6.85 1742.47 4.00");
    assert(!session.ready());
    commandLine("setcal 6.85 1742.47 4.00 1667.72 9");
    assert(!session.ready());
    commandLine("setcal 6.85 1742.47 4.00 1742.00");
    assert(!session.ready());
    commandLine("setcal 6.85 1742.47 4.00 1667.72");
    assert(session.ready() && std::fabs(active.convert(2264.44f) - 4.01f) < 0.001f);
    commandLine("save");
    assert(std::fabs(active.convert(1742.47f) - 6.85f) < 0.001f);
    assert(std::fabs(active.convert(1667.72f) - 4.00f) < 0.001f);
    puts("PASS: actual firmware sampler, pH/CSV labels, commands, save failure, faults and timing checks");
}
