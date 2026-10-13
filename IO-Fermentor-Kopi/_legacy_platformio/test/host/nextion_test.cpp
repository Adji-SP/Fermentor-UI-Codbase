#include <cassert>
#include <initializer_list>
#include <iostream>
#include "Arduino.h"
#include "../../src/hmi_motor.h"
#include "../../src/nextion_protocol.h"

static void boot() {
    fake::now = 0;
    std::fill(std::begin(fake::pwm), std::end(fake::pwm), 0);
    std::fill(std::begin(fake::digital), std::end(fake::digital), 0);
    Serial = FakeSerial{};
    Serial1 = FakeSerial{};
    hmiMotorBegin();
    hmiMotorTick();
    assert(Serial1.tx.find("sendme") != std::string::npos);
}
static void feed(std::initializer_list<uint8_t> bytes) {
    for (uint8_t byte : bytes) Serial1.rx.push_back(byte);
    hmiMotorTick();
}
static void ascii(const std::string &text) {
    for (unsigned char ch : text) Serial1.rx.push_back(ch);
    hmiMotorTick();
}
static void page(uint8_t id = 0) { feed({0x66, id, 0xff, 0xff, 0xff}); }
static void touch(uint8_t id, bool pressed = false, uint8_t onPage = 0) {
    feed({0x65, onPage, id, uint8_t(pressed), 0xff, 0xff, 0xff});
}
static void number(uint32_t value) {
    feed({0x71, uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16),
          uint8_t(value >> 24), 0xff, 0xff, 0xff});
}
static void stopped() {
    assert(fake::pwm[7] == 0 && fake::pwm[6] == 0);
    assert(fake::digital[5] == LOW && fake::digital[4] == LOW);
}
static void flushDisplay() {
    for (int i = 0; i < 15; ++i) { ++fake::now; hmiMotorTick(); }
}

static void test_mapping_and_initial_sync() {
    boot();
    HmiReadings readings;
    readings.processTemp = {24.25f, ReadingStatus::OK};
    readings.ph = {6.5f, ReadingStatus::OK};
    readings.scdTemp = {25.5f, ReadingStatus::OK};
    readings.shtTemp = {23.75f, ReadingStatus::OK};
    readings.pressure = {0.125f, ReadingStatus::OK};
    readings.oxygen = {20.75f, ReadingStatus::OK};
    readings.co2 = {650.0f, ReadingStatus::OK};
    readings.scdRh = {55.5f, ReadingStatus::OK};
    readings.shtRh = {60.25f, ReadingStatus::OK};
    hmiMotorSetReadings(readings);
    page();
    flushDisplay();
    for (const char *expected : {
        "t0.txt=\"24.25 C\"", "t1.txt=\"6.50\"", "t2.txt=\"25.50 C\"",
        "t3.txt=\"23.75 C\"", "t4.txt=\"0.125 bar\"", "t5.txt=\"20.75 %\"",
        "t6.txt=\"650 ppm\"", "t7.txt=\"55.50 %\"", "t8.txt=\"60.25 %\"",
        "t9.txt=\"OFF\"", "t10.txt=\"64/255 (25%)\"", "h0.val=64"
    }) assert(Serial1.tx.find(expected) != std::string::npos);
    assert(Serial1.tx.find("gPt") == std::string::npos);
    assert(Serial1.tx.find("tMotor") == std::string::npos);
    stopped();
}

static void test_buttons_direction_pause_and_stop_during_tx_backpressure() {
    boot();
    page();
    touch(13, true);
    stopped();
    touch(13, false, 1);
    stopped();
    touch(13);
    assert(fake::pwm[7] == 64 && fake::pwm[6] == 0);
    fake::now = 10;
    touch(14);
    stopped();
    fake::now = 20;
    touch(14); // A duplicate event must not start early.
    stopped();
    fake::now = 259;
    hmiMotorTick();
    stopped();
    fake::now = 260;
    hmiMotorTick();
    assert(fake::pwm[6] == 64 && fake::pwm[7] == 0);
    Serial1.writable = 0;
    touch(15);
    stopped();
}

static void test_slider_reply_validation_and_zero_stop() {
    boot();
    page();
    number(255); // Unsolicited numeric replies cannot change PWM.
    touch(13);
    assert(fake::pwm[7] == 64);
    touch(16, true);
    touch(16);
    assert(Serial1.tx.find("get h0.val") != std::string::npos);
    number(255); // Payload contains FF.
    assert(fake::pwm[7] == 255);
    touch(16);
    number(0);
    stopped();
    touch(16);
    number(10); // Payload contains LF.
    stopped(); // Increasing speed cannot restart the motor.
    touch(13);
    assert(fake::pwm[7] == 10);
    touch(16);
    number(0xffffffff);
    assert(fake::pwm[7] == 10);
    touch(16);
    fake::now += 501;
    number(200); // Late reply.
    assert(fake::pwm[7] == 10);
    touch(16);
    touch(15);
    number(100);
    stopped();
}

static void test_disconnect_restart_wrong_page_and_reversal_cancel() {
    boot();
    page();
    touch(13);
    fake::now = 3001;
    hmiMotorTick();
    stopped();
    touch(13); // Requires page/link synchronization again.
    stopped();
    page();
    touch(13);
    page(1);
    stopped();
    page();
    touch(13);
    feed({0, 0, 0, 0xff, 0xff, 0xff, 0x88, 0xff, 0xff, 0xff});
    stopped(); // Nextion reboot stops the motor.
    page();
    touch(13);
    touch(14);
    touch(15);
    fake::now += 300;
    hmiMotorTick();
    stopped();
}

static void test_invalid_frames_and_ascii_compatibility() {
    boot();
    page();
    ascii(std::string(60, 'M') + "MOTOR:FWD\n");
    stopped();
    feed({0x70}); // Nextion string payload cannot inject an ASCII command.
    ascii("MOTOR:FWD\n");
    stopped();
    feed({0xff, 0xff, 0xff});
    ascii("MOTOR:SPEED:-1\nMOTOR:SPEED:256\n");
    touch(13);
    assert(fake::pwm[7] == 64);
    ascii("MOTOR:STOP\n");
    stopped();
    feed({0x65, 0, 13, 0, 0xff, 0, 0xff});
    ascii("MOTOR:FWD\n");
    stopped();
    feed({0xff, 0xff, 0xff});
    ascii("DASH:SYNC\nMOTOR:SPEED:123\nMOTOR:FWD\n");
    assert(fake::pwm[7] == 123);
    ascii("MOTOR:STOP\n");
    stopped();
}

int main() {
    test_mapping_and_initial_sync();
    test_buttons_direction_pause_and_stop_during_tx_backpressure();
    test_slider_reply_validation_and_zero_stop();
    test_disconnect_restart_wrong_page_and_reversal_cancel();
    test_invalid_frames_and_ascii_compatibility();
    std::cout << "PASS: single-page mapping, native frames, slider validation, motor controls and link timeout\n";
}
