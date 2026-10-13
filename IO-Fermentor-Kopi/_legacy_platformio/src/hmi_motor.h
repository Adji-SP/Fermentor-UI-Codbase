#pragma once

#include <Arduino.h>

enum class ReadingStatus : uint8_t { WAIT, OK, OFFLINE, ERROR };

struct HmiReading {
    HmiReading(float reading = NAN, ReadingStatus state = ReadingStatus::WAIT)
        : value(reading), status(state) {}
    float value;
    ReadingStatus status;
};

struct HmiReadings {
    HmiReading processTemp;
    HmiReading oxygen;
    HmiReading ph;
    HmiReading co2;
    HmiReading scdTemp;
    HmiReading scdRh;
    HmiReading shtTemp;
    HmiReading shtRh;
    HmiReading pressure;
};

// The UART and motor pins follow Schematic_Fermentor (1).pdf, not the older PCB.
void hmiMotorBegin();
void hmiMotorTick();
void hmiMotorSetReadings(const HmiReadings &readings);
