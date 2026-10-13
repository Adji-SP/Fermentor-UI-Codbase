#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============================================================
//  FERMENTOR KOPI — SENSOR DASHBOARD AND NEXTION MOTOR CONTROL
//  Target : ESP32-S3 (PlatformIO C++ / Arduino Framework)
//  Sensors: RTD PT100  (MAX31865 / raw SPI)
//           DFRobot O2 SEN0322 (I2C, addr 0x73)
//           DFRobot pH SEN0161-V2 (ADS1115 A0 by default; direct ADC optional)
//           Sensirion SCD41 CO2 (I2C, addr 0x62)
//           Sensirion SHT20 chamber temperature/RH (I2C, addr 0x40)
//           Sensirion SHT30 temperature/RH (I2C, addr 0x44)
// ============================================================

// ------------------------------------------------------------
// Pin Definitions (ESP32-S3)
// ------------------------------------------------------------
// 1 = custom PCB pada "WhatsApp Image 2026-08-13 at 14.12.21 (1).jpeg"
//     SDI/MOSI=GPIO35, CLK=GPIO36, SDO/MISO=GPIO37, CS=GPIO38.
// 0 = wiring kabel lama pada "WIRING DIAGRAM FERMENTOR KOPI.png"
//     SDI/MOSI=GPIO11, CLK=GPIO12, SDO/MISO=GPIO13, CS=GPIO10.
#define USE_CUSTOM_PCB_WIRING 1

#if USE_CUSTOM_PCB_WIRING
#define PIN_SPI_SCK   36
#define PIN_SPI_MOSI  35
#define PIN_SPI_MISO  37
#define PIN_SPI_CS    38
#else
#define PIN_SPI_SCK   12
#define PIN_SPI_MOSI  11
#define PIN_SPI_MISO  13
#define PIN_SPI_CS    10
#endif

#define PIN_I2C_SDA   8
#define PIN_I2C_SCL   9
#define PIN_PH_ANALOG 15

// HW-390 / BTS7960 motor driver (connector J13 on the custom PCB).
// J13 net labels GPIO4..GPIO7 are routed in reverse order to the ESP32:
// net GPIO4 -> ESP GPIO7, net GPIO5 -> ESP GPIO6,
// net GPIO6 -> ESP GPIO5, net GPIO7 -> ESP GPIO4.
#define PIN_MOTOR_LPWM 7
#define PIN_MOTOR_RPWM 6
#define PIN_MOTOR_L_EN 5
#define PIN_MOTOR_R_EN 4

// Custom PCB Nextion connector: ESP TX -> Nextion RX5, ESP RX <- Nextion TX5.
// USB CDC is used for Serial diagnostics so this UART remains dedicated to HMI.
#define PIN_NEXTION_TX 43
#define PIN_NEXTION_RX 44
const uint32_t NEXTION_BAUD = 9600;

// ------------------------------------------------------------
// Per-Sensor Test Switches
// Set exactly the sensor(s) under test to 1. A sensor set to 0 is
// not initialized and is omitted from the repeating sample output.
// The raw I2C scanner still runs every time the ESP32 starts.
// ------------------------------------------------------------
#define TEST_PT100   1
#define TEST_O2      1
#define TEST_PH      1
#define TEST_CO2     1
#define TEST_SHT20   1
#define TEST_SHT30   0

// Manual motor commands come only from the Nextion UART. The motor starts OFF.
const uint8_t MOTOR_DEFAULT_DUTY = 64; // 0..255; 64 is about 25% PWM duty.
const uint32_t MOTOR_REVERSE_PAUSE_MS = 250;
const uint32_t MOTOR_COMMAND_TIMEOUT_MS = 3000;
const uint32_t SENSOR_SAMPLE_INTERVAL_MS = 1000;

// ------------------------------------------------------------
// pH Input Selection
// Choose exactly one mode for the pH signal:
//   PH_INPUT_DIRECT_ADC : pH board A -> ESP32-S3 GPIO15
//   PH_INPUT_ADS1115    : pH board A -> ADS1115 A0, ADDR -> GND (0x48)
// ------------------------------------------------------------
#define PH_INPUT_DIRECT_ADC  1
#define PH_INPUT_ADS1115     2
#ifndef PH_INPUT_MODE
#define PH_INPUT_MODE PH_INPUT_ADS1115
#endif

static_assert(
    PH_INPUT_MODE == PH_INPUT_DIRECT_ADC || PH_INPUT_MODE == PH_INPUT_ADS1115,
    "PH_INPUT_MODE harus PH_INPUT_DIRECT_ADC atau PH_INPUT_ADS1115"
);

// ------------------------------------------------------------
// Sensor Constants & Register Addresses
// ------------------------------------------------------------
const float   PT100_REF_OHM     = 431.0f;
const float   PT100_NOMINAL_OHM = 100.0f;

const uint8_t O2_SENSOR_ADDR    = 0x73;
const uint8_t ADS1115_ADDR      = 0x48;
const uint8_t SCD41_ADDR        = 0x62;
const uint8_t SHT20_ADDR        = 0x40;
const uint8_t SHT30_ADDR        = 0x44;

const uint8_t MAX_REG_CONFIG    = 0x00;
const uint8_t MAX_REG_RTD_MSB   = 0x01;
const uint8_t MAX_REG_RTD_LSB   = 0x02;
const uint8_t MAX_REG_FAULT     = 0x07;
// 0xD1 = Vbias ON | Auto conversion mode | 3-wire compensation | 50Hz filter.
// If your RTD is wired 2-wire/4-wire instead of 3-wire, use 0xC1 (clears D4).
const uint8_t MAX_CONFIG_INIT   = 0xD1;

const uint8_t O2_REG_READ       = 0x03;
const uint8_t O2_REG_GET_KEY    = 0x0A;
const float   O2_DEFAULT_KEY    = 20.9f / 120.0f;

// Default calibration values from the DFRobot_PH library for SEN0161-V2.
// Replace these after two-point calibration with pH 7 and pH 4 buffers.
const float   PH_NEUTRAL_MV     = 1500.0f;
const float   PH_ACID_MV        = 2032.44f;
const float   PH_OFFSET         = 0.0f;

const uint16_t SCD41_CMD_START_PERIODIC   = 0x21B1;
const uint16_t SCD41_CMD_READ_MEASUREMENT = 0xEC05;
const uint16_t SCD41_CMD_DATA_READY       = 0xE4B8;

// Single-shot, high-repeatability measurement with clock stretching disabled.
const uint16_t SHT30_CMD_SINGLE_SHOT      = 0x2400;

// SHT20 no-hold-master commands. The sensor keeps the I2C bus free while measuring.
const uint8_t SHT20_CMD_TEMP = 0xF3;
const uint8_t SHT20_CMD_RH   = 0xF5;

// SPI: 200 kHz, MSBFIRST, Mode 1 (CPOL=0, CPHA=1)
SPISettings spiSettings(200000, MSBFIRST, SPI_MODE1);

void nextion_poll();
void motor_update();

void wait_with_hmi(uint32_t waitMs) {
    uint32_t started = millis();
    while (millis() - started < waitMs) {
        nextion_poll();
        motor_update();
        delay(2);
    }
}

// ============================================================
// MAX31865 SPI Helpers
// ============================================================
uint8_t max31865_read_reg(uint8_t reg) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(10);
    SPI.transfer(reg & 0x7F);
    uint8_t val = SPI.transfer(0x00);
    delayMicroseconds(10);
    digitalWrite(PIN_SPI_CS, HIGH);
    SPI.endTransaction();
    return val;
}

void max31865_write_reg(uint8_t reg, uint8_t val) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(10);
    SPI.transfer(reg | 0x80);
    SPI.transfer(val);
    delayMicroseconds(10);
    digitalWrite(PIN_SPI_CS, HIGH);
    SPI.endTransaction();
}

// Baca MSB+LSB RTD dalam satu transaksi SPI (satu window CS-low),
// supaya tidak ada konversi baru yang menyelinap di antara dua
// pembacaan register terpisah.
void max31865_read_rtd(uint8_t &msb, uint8_t &lsb) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(PIN_SPI_CS, LOW);
    delayMicroseconds(10);
    SPI.transfer(MAX_REG_RTD_MSB & 0x7F);
    msb = SPI.transfer(0x00);
    lsb = SPI.transfer(0x00);
    delayMicroseconds(10);
    digitalWrite(PIN_SPI_CS, HIGH);
    SPI.endTransaction();
}

// Callendar-Van Dusen equation (IEC 60751)
float pt100_r_to_c(float r, float r0) {
    const float A = 3.9083e-3f;
    const float B = -5.775e-7f;
    float disc = A * A - 4.0f * B * (1.0f - r / r0);
    if (disc < 0.0f) return NAN;
    return (-A + sqrtf(disc)) / (2.0f * B);
}

// ============================================================
// DFRobot O2 Sensor (SEN0322) Helpers
// ============================================================
bool o2_probe() {
    Wire.beginTransmission(O2_SENSOR_ADDR);
    return Wire.endTransmission() == 0;
}

bool o2_read_register(uint8_t reg, uint8_t *buf, uint8_t len, uint16_t waitMs) {
    Wire.beginTransmission(O2_SENSOR_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;

    wait_with_hmi(waitMs);
    if (Wire.requestFrom((uint8_t)O2_SENSOR_ADDR, len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

bool read_o2(float &oxygenPercent) {
    // SEN0322 stores its calibration key little-endian in 0x0A/0x0B.
    // A zero key means the factory/default 20.9/120 conversion must be used.
    uint8_t keyBuf[2];
    if (!o2_read_register(O2_REG_GET_KEY, keyBuf, 2, 50)) return false;
    uint16_t storedKey = ((uint16_t)keyBuf[1] << 8) | keyBuf[0];
    float key = (storedKey == 0) ? O2_DEFAULT_KEY : (float)storedKey / 1000.0f;

    // The three bytes are decimal components, not one 16-bit integer.
    uint8_t data[3];
    if (!o2_read_register(O2_REG_READ, data, 3, 100)) return false;
    float raw = (float)data[0] + (float)data[1] / 10.0f + (float)data[2] / 100.0f;
    float pct = key * raw;

    // Nominal range is 0-25 %vol; retain a small diagnostic margin.
    if (!isfinite(pct) || pct < 0.0f || pct > 30.0f) return false;

    oxygenPercent = pct;
    return true;
}

// ============================================================
// pH via ESP32-S3 ADC on GPIO15
// ============================================================
float ph_mv_to_value(float millivolts) {
    // Same two-point equation used by DFRobot_PH::readPH().
    float slope = (7.0f - 4.0f) /
                  ((PH_NEUTRAL_MV - 1500.0f) / 3.0f -
                   (PH_ACID_MV - 1500.0f) / 3.0f);
    float intercept = 7.0f - slope * (PH_NEUTRAL_MV - 1500.0f) / 3.0f;
    return slope * (millivolts - 1500.0f) / 3.0f + intercept + PH_OFFSET;
}

void read_ph_adc(uint16_t &rawAverage, float &millivoltsAverage) {
    constexpr uint8_t SAMPLE_COUNT = 16;
    uint32_t rawSum = 0;
    uint32_t millivoltsSum = 0;

    for (uint8_t i = 0; i < SAMPLE_COUNT; ++i) {
        rawSum += analogRead(PIN_PH_ANALOG);
        millivoltsSum += analogReadMilliVolts(PIN_PH_ANALOG);
        delay(2);
    }

    rawAverage = (uint16_t)(rawSum / SAMPLE_COUNT);
    millivoltsAverage = (float)millivoltsSum / (float)SAMPLE_COUNT;
}

// ADS1115 continuous conversion: AIN0 to GND, +/-4.096 V, 250 SPS.
bool ads1115_initialize() {
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write((uint8_t)0x01); // Configuration register
    Wire.write((uint8_t)0xC2);
    Wire.write((uint8_t)0x83);
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    return true;
}

bool read_ph_ads1115(int16_t &counts, float &millivolts) {
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write((uint8_t)0x00); // Conversion register
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint8_t)ADS1115_ADDR, (uint8_t)2) != 2) return false;
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    counts = (int16_t)(((uint16_t)msb << 8) | lsb);

    // PGA is +/-4.096 V: one ADS1115 count is exactly 0.125 mV.
    millivolts = (float)counts * 0.125f;
    return true;
}

// ============================================================
// Sensirion SCD41 (CO2) Helpers
// ============================================================
// CRC-8, polynomial 0x31, init 0xFF (Sensirion command/data checksum).
uint8_t crc8(const uint8_t *data, uint8_t len, uint8_t initial = 0xFF) {
    uint8_t crc = initial;
    for (uint8_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// ============================================================
// Sensirion SHT20 (chamber temperature and relative humidity)
// ============================================================
bool sht20_read_raw(uint8_t command, uint16_t waitMs, uint16_t &raw) {
    Wire.beginTransmission(SHT20_ADDR);
    Wire.write(command);
    if (Wire.endTransmission() != 0) return false;

    wait_with_hmi(waitMs);
    if (Wire.requestFrom((uint8_t)SHT20_ADDR, (uint8_t)3) != 3) return false;

    uint8_t data[3];
    for (uint8_t i = 0; i < 3; ++i) data[i] = Wire.read();
    // SHT2x uses the same polynomial as SHT3x, but starts CRC at 0x00.
    if (crc8(data, 2, 0x00) != data[2]) return false;
    if ((data[1] & 0x02) != ((command == SHT20_CMD_RH) ? 0x02 : 0x00)) return false;

    raw = (((uint16_t)data[0] << 8) | data[1]) & 0xFFFC;
    return true;
}

bool sht20_read_measurement(float &tempC, float &humRh) {
    uint16_t rawTemp, rawHum;
    // Maximum conversion times at the default 14-bit T / 12-bit RH resolution.
    if (!sht20_read_raw(SHT20_CMD_TEMP, 90, rawTemp)) return false;
    if (!sht20_read_raw(SHT20_CMD_RH, 35, rawHum)) return false;

    tempC = -46.85f + 175.72f * (float)rawTemp / 65536.0f;
    humRh = -6.0f + 125.0f * (float)rawHum / 65536.0f;
    return true;
}

// ============================================================
// Sensirion SHT30 (temperature and relative humidity) Helpers
// ============================================================
bool sht30_read_measurement(float &tempC, float &humRh) {
    Wire.beginTransmission(SHT30_ADDR);
    Wire.write((uint8_t)(SHT30_CMD_SINGLE_SHOT >> 8));
    Wire.write((uint8_t)(SHT30_CMD_SINGLE_SHOT & 0xFF));
    if (Wire.endTransmission() != 0) return false;

    // A high-repeatability SHT30 measurement takes at most about 15 ms.
    delay(20);
    if (Wire.requestFrom((uint8_t)SHT30_ADDR, (uint8_t)6) != 6) return false;

    uint8_t buf[6];
    for (uint8_t i = 0; i < 6; ++i) buf[i] = Wire.read();

    if (crc8(&buf[0], 2) != buf[2]) return false;
    if (crc8(&buf[3], 2) != buf[5]) return false;

    uint16_t rawTemp = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawHum  = ((uint16_t)buf[3] << 8) | buf[4];

    tempC = -45.0f + 175.0f * (float)rawTemp / 65535.0f;
    humRh = 100.0f * (float)rawHum / 65535.0f;
    return true;
}

bool scd41_send_command(uint16_t cmd) {
    Wire.beginTransmission(SCD41_ADDR);
    Wire.write((uint8_t)(cmd >> 8));
    Wire.write((uint8_t)(cmd & 0xFF));
    return Wire.endTransmission() == 0;
}

bool scd41_data_ready(bool &ready) {
    if (!scd41_send_command(SCD41_CMD_DATA_READY)) return false;
    delay(1);
    if (Wire.requestFrom((uint8_t)SCD41_ADDR, (uint8_t)3) != 3) return false;
    uint8_t buf[3];
    for (uint8_t i = 0; i < 3; ++i) buf[i] = Wire.read();

    if (crc8(buf, 2) != buf[2]) return false;

    uint16_t status = ((uint16_t)buf[0] << 8) | buf[1];
    ready = (status & 0x07FF) != 0;
    return true;
}

bool scd41_read_measurement(uint16_t &co2, float &tempC, float &humRh) {
    if (!scd41_send_command(SCD41_CMD_READ_MEASUREMENT)) return false;
    delay(1);
    if (Wire.requestFrom((uint8_t)SCD41_ADDR, (uint8_t)9) != 9) return false;
    uint8_t buf[9];
    for (uint8_t i = 0; i < 9; ++i) buf[i] = Wire.read();

    if (crc8(&buf[0], 2) != buf[2]) return false;
    if (crc8(&buf[3], 2) != buf[5]) return false;
    if (crc8(&buf[6], 2) != buf[8]) return false;

    co2 = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawTemp = ((uint16_t)buf[3] << 8) | buf[4];
    uint16_t rawHum  = ((uint16_t)buf[6] << 8) | buf[7];

    tempC = -45.0f + 175.0f * (float)rawTemp / 65535.0f;
    humRh = 100.0f * (float)rawHum / 65535.0f;
    return true;
}

// ============================================================
// I2C Bus Scanner (raw connectivity check, no device-specific protocol)
// ============================================================
uint8_t i2c_scan() {
    Serial.println("[I2C SCAN] Memindai bus 0x01-0x7F...");
    uint8_t found = 0;
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        nextion_poll();
        motor_update();
        if (err == 0) {
            Serial.printf("  --> Perangkat terdeteksi di alamat 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) {
        Serial.println("[I2C SCAN] TIDAK ADA perangkat ditemukan. Cek SDA/SCL/GND/VCC & continuity.");
    } else {
        Serial.printf("[I2C SCAN] %u perangkat ditemukan di bus.\n", found);
    }
    return found;
}

// ============================================================
// HW-390 / BTS7960 motor controlled by Nextion commands
// ============================================================
// The physical direction depends on how the two motor leads are connected,
// so FWD and REV below name LPWM and RPWM, not physical rotation direction.
enum class MotorDirection : uint8_t {
    STOP,
    LPWM,
    RPWM
};

MotorDirection motorDirection = MotorDirection::STOP;
MotorDirection motorPendingDirection = MotorDirection::STOP;
uint8_t motorDuty = MOTOR_DEFAULT_DUTY;
uint32_t motorStoppedAtMs = 0;
uint32_t motorLastCommandMs = 0;
bool motorCommandActive = false;
bool hmiRefreshRequested = true;
bool hmiSliderSyncRequested = false;

char hmiPt100[20] = "WAIT";
char hmiO2[20] = "WAIT";
char hmiPh[20] = "WAIT";
char hmiCo2[20] = "WAIT";
char hmiScdTemp[20] = "WAIT";
char hmiScdRh[20] = "WAIT";
char hmiSht20Temp[20] = "WAIT";
char hmiSht20Rh[20] = "WAIT";
char hmiSht30Temp[20] = "DISABLED";
char hmiSht30Rh[20] = "DISABLED";

void nextion_set_text(const char *component, const char *value) {
    Serial1.print(component);
    Serial1.print(".txt=\"");
    Serial1.print(value);
    Serial1.print('"');
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
}

void nextion_sync_slider() {
    Serial1.printf("h0.val=%u", motorDuty);
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
}

void nextion_publish_dashboard() {
    nextion_set_text("tPt", hmiPt100);
    nextion_set_text("tO2", hmiO2);
    nextion_set_text("tPh", hmiPh);
    nextion_set_text("tCo2", hmiCo2);
    nextion_set_text("tScT", hmiScdTemp);
    nextion_set_text("tScH", hmiScdRh);
    nextion_set_text("tChT", hmiSht20Temp);
    nextion_set_text("tChH", hmiSht20Rh);
    nextion_set_text("tS3T", hmiSht30Temp);
    nextion_set_text("tS3H", hmiSht30Rh);

    const char *motorStatus = "OFF";
    if (motorPendingDirection != MotorDirection::STOP) {
        motorStatus = "STARTING";
    } else if (motorDirection == MotorDirection::LPWM) {
        motorStatus = "ON FWD";
    } else if (motorDirection == MotorDirection::RPWM) {
        motorStatus = "ON REV";
    }
    nextion_set_text("tMotor", motorStatus);

    char speedText[20];
    snprintf(speedText, sizeof(speedText), "%u/255 (%u%%)", motorDuty,
             (unsigned int)motorDuty * 100U / 255U);
    nextion_set_text("tDuty", speedText);

    if (hmiSliderSyncRequested) {
        nextion_sync_slider();
        hmiSliderSyncRequested = false;
    }
    hmiRefreshRequested = false;
}

void motor_stop() {
    analogWrite(PIN_MOTOR_LPWM, 0);
    analogWrite(PIN_MOTOR_RPWM, 0);
    digitalWrite(PIN_MOTOR_L_EN, LOW);
    digitalWrite(PIN_MOTOR_R_EN, LOW);
    motorDirection = MotorDirection::STOP;
}

void motor_begin() {
    digitalWrite(PIN_MOTOR_LPWM, LOW);
    digitalWrite(PIN_MOTOR_RPWM, LOW);
    digitalWrite(PIN_MOTOR_L_EN, LOW);
    digitalWrite(PIN_MOTOR_R_EN, LOW);

    pinMode(PIN_MOTOR_LPWM, OUTPUT);
    pinMode(PIN_MOTOR_RPWM, OUTPUT);
    pinMode(PIN_MOTOR_L_EN, OUTPUT);
    pinMode(PIN_MOTOR_R_EN, OUTPUT);

    motor_stop();
    motorPendingDirection = MotorDirection::STOP;
    motorCommandActive = false;
}

void motor_request_stop(const char *reason) {
    motorPendingDirection = MotorDirection::STOP;
    motorCommandActive = false;
    motor_stop();
    hmiRefreshRequested = true;
    Serial.printf("[MOTOR] OFF (%s)\n", reason);
}

void motor_request_direction(MotorDirection requested) {
    if (motorDuty == 0) {
        motor_request_stop("speed is zero");
        return;
    }

    motorLastCommandMs = millis();
    motorCommandActive = true;
    if (motorDirection == requested && motorPendingDirection == MotorDirection::STOP) {
        return;
    }

    // Always remove both PWM signals before a direction change.
    motor_stop();
    motorPendingDirection = requested;
    motorStoppedAtMs = millis();
    hmiRefreshRequested = true;
    Serial.println("[MOTOR] Direction requested; waiting for reversal pause");
}

void motor_update() {
    if (!motorCommandActive) return;

    uint32_t now = millis();
    if (now - motorLastCommandMs >= MOTOR_COMMAND_TIMEOUT_MS) {
        motor_request_stop("Nextion heartbeat timeout");
        return;
    }

    if (motorPendingDirection != MotorDirection::STOP &&
        now - motorStoppedAtMs >= MOTOR_REVERSE_PAUSE_MS) {
        digitalWrite(PIN_MOTOR_L_EN, HIGH);
        digitalWrite(PIN_MOTOR_R_EN, HIGH);
        if (motorPendingDirection == MotorDirection::LPWM) {
            analogWrite(PIN_MOTOR_LPWM, motorDuty);
            Serial.printf("[MOTOR] LPWM running at %u/255\n", motorDuty);
        } else {
            analogWrite(PIN_MOTOR_RPWM, motorDuty);
            Serial.printf("[MOTOR] RPWM running at %u/255\n", motorDuty);
        }
        motorDirection = motorPendingDirection;
        motorPendingDirection = MotorDirection::STOP;
        hmiRefreshRequested = true;
    }
}

void nextion_handle_command(const char *command) {
    if (strcmp(command, "DASH:SYNC") == 0) {
        hmiRefreshRequested = true;
        hmiSliderSyncRequested = true;
    } else if (strcmp(command, "MOTOR:STOP") == 0) {
        motor_request_stop("HMI stop");
    } else if (strcmp(command, "MOTOR:FWD") == 0) {
        motor_request_direction(MotorDirection::LPWM);
    } else if (strcmp(command, "MOTOR:REV") == 0) {
        motor_request_direction(MotorDirection::RPWM);
    } else if (strcmp(command, "MOTOR:KEEPALIVE") == 0) {
        if (motorCommandActive) motorLastCommandMs = millis();
    } else if (strncmp(command, "MOTOR:SPEED:", 12) == 0) {
        const char *digit = command + 12;
        if (*digit == '\0') return;
        uint16_t value = 0;
        while (*digit != '\0') {
            if (*digit < '0' || *digit > '9') return;
            value = value * 10 + (*digit++ - '0');
            if (value > 255) return;
        }
        motorDuty = (uint8_t)value;
        hmiRefreshRequested = true;
        Serial.printf("[MOTOR] Speed set to %u/255\n", motorDuty);
        if (motorDuty == 0) {
            motor_request_stop("speed is zero");
        } else if (motorDirection != MotorDirection::STOP) {
            analogWrite(motorDirection == MotorDirection::LPWM ? PIN_MOTOR_LPWM : PIN_MOTOR_RPWM, motorDuty);
            motorLastCommandMs = millis();
        }
    }
}

void nextion_poll() {
    static char command[32];
    static uint8_t length = 0;
    static bool valid = true;

    while (Serial1.available()) {
        uint8_t ch = (uint8_t)Serial1.read();
        if (ch == '\n') {
            if (valid && length > 0) {
                command[length] = '\0';
                nextion_handle_command(command);
            }
            length = 0;
            valid = true;
        } else if (ch == '\r') {
            continue;
        } else if (ch < 32 || ch > 126) {
            // Ignore Nextion boot/touch packets, which are binary and end in 0xFF.
            length = 0;
            valid = true;
        } else if (valid && length < sizeof(command) - 1) {
            command[length++] = (char)ch;
        } else {
            length = 0;
            valid = false;
        }
    }
}

// ============================================================
// Setup & Loop
// ============================================================
uint32_t sampleCount = 0;
bool max31865_online = false;
bool o2_online       = false;
bool ads1115_online  = false;
bool scd41_online    = false;
bool sht20_online    = false;
bool sht30_online    = false;

void setup() {
    // ── Matikan Brownout Detector di register RTC
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Keep the motor driver disabled until a valid Nextion run command arrives.
    motor_begin();

    Serial.begin(115200);
    Serial1.begin(NEXTION_BAUD, SERIAL_8N1, PIN_NEXTION_RX, PIN_NEXTION_TX);
    delay(500);

    Serial.println();
    Serial.println("**************************************************");
    Serial.println("*** FERMENTOR KOPI -- SENSOR TEST (STABLE MODE)***");
    Serial.println("**************************************************");
    Serial.println();

    Serial.printf("[NEXTION] UART 9600: ESP TX=GPIO%u, ESP RX=GPIO%u\n", PIN_NEXTION_TX, PIN_NEXTION_RX);
    Serial.printf("[MOTOR] HMI control ready; motor OFF. LPWM=GPIO%u, RPWM=GPIO%u, L_EN=GPIO%u, R_EN=GPIO%u\n",
                  PIN_MOTOR_LPWM, PIN_MOTOR_RPWM, PIN_MOTOR_L_EN, PIN_MOTOR_R_EN);
    Serial.println("[MOTOR] Commands: MOTOR:FWD, MOTOR:REV, MOTOR:STOP, MOTOR:SPEED:0..255, MOTOR:KEEPALIVE");
    Serial.println();

    // ── STEP 1: SPI MAX31865 (RTD PT100) ─────────────────────────────────
    if (TEST_PT100) {
        Serial.printf(
            "[STEP 1] Inisialisasi SPI (Mode 1, SCK=%u, MOSI=%u, MISO=%u, CS=%u)...\n",
            PIN_SPI_SCK, PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_CS
        );
        pinMode(PIN_SPI_CS, OUTPUT);
        digitalWrite(PIN_SPI_CS, HIGH);
        SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CS);
        delay(200);

        max31865_write_reg(MAX_REG_CONFIG, MAX_CONFIG_INIT);
        delay(50);
        max31865_write_reg(MAX_REG_CONFIG, MAX_CONFIG_INIT);
        delay(100);

        uint8_t cfg_rb = max31865_read_reg(MAX_REG_CONFIG);
        Serial.printf("[STEP 1 DONE] MAX31865 config readback = 0x%02X (diharapkan 0x%02X)\n", cfg_rb, MAX_CONFIG_INIT);
        max31865_online = (cfg_rb == MAX_CONFIG_INIT);
        if (!max31865_online) {
            Serial.println("[STEP 1 ERROR] MAX31865 tidak berkomunikasi lewat SPI.");
            Serial.println("  PCB baru: cek GPIO35/36/37/38 dan pastikan OE TXS0108E = HIGH (3.3V), bukan GND.");
            Serial.println("  Pembacaan PT100 dinonaktifkan agar raw 0 tidak dicetak sebagai suhu -246.86 C.");
        }
    } else {
        Serial.println("[STEP 1 SKIP] PT100/MAX31865 tidak dipilih untuk pengujian.");
    }

    // ── STEP 2: I2C (SDA=GPIO8, SCL=GPIO9) 50 kHz ────────────────────────
    Serial.println("[STEP 2] Inisialisasi I2C 50kHz (SDA=GPIO8, SCL=GPIO9)...");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(50000);
    Wire.setTimeOut(100);

    i2c_scan();

    Serial.println("[STEP 2] Respon I2C bus:");

    if (TEST_PH) {
        if (PH_INPUT_MODE == PH_INPUT_DIRECT_ADC) {
            pinMode(PIN_PH_ANALOG, INPUT);
            analogReadResolution(12);
            // ESP32-S3 GPIO15 is ADC2_CH4. 11 dB covers the pH board's 0-3.0 V output.
            analogSetPinAttenuation(PIN_PH_ANALOG, ADC_11db);
            Serial.println("[STEP 2 PH] Mode DIRECT ADC aktif: pH A -> GPIO15.");
        } else {
            ads1115_online = ads1115_initialize();
            if (ads1115_online) {
                Serial.println("[STEP 2 PH] Mode ADS1115 aktif: 0x48 terdeteksi, pH A -> A0.");
            } else {
                Serial.println("[STEP 2 PH WARN] Mode ADS1115 aktif tetapi 0x48 tidak merespon.");
            }
        }

    // ── STEP 3: O2 Sensor (0x73) ─────────────────────────────────────────
    } else {
        Serial.println("[STEP 2 SKIP] pH tidak dipilih untuk pengujian.");
    }

    if (TEST_O2) {
    Serial.println("[STEP 3] Cek O2 Sensor (0x73)...");
    // Probe only. Never overwrite the SEN0322 calibration during normal boot.
    o2_online = o2_probe();
    if (o2_online) {
        Serial.println("[STEP 3 DONE] O2 Sensor respon OK di 0x73");
    } else {
        Serial.println("[STEP 3 WARN] O2 Sensor (0x73) belum merespon. (Cek kabel VCC 5V O2 Sensor)");
    }

    // ── STEP 4: SCD41 CO2 Sensor (0x62) ───────────────────────────────────
    } else {
        Serial.println("[STEP 3 SKIP] O2 tidak dipilih untuk pengujian.");
    }

    if (TEST_CO2) {
    Serial.println("[STEP 4] Inisialisasi SCD41 (CO2 Sensor, 0x62)...");

    scd41_online = scd41_send_command(SCD41_CMD_START_PERIODIC);
    if (scd41_online) {
        Serial.println("[STEP 4 DONE] SCD41 mulai periodic measurement (update setiap 5 detik)");
    } else {
        Serial.println("[STEP 4 WARN] SCD41 (0x62) belum merespon. (Cek kabel I2C)");
    }

    } else {
        Serial.println("[STEP 4 SKIP] CO2/SCD41 tidak dipilih untuk pengujian.");
    }

    // STEP 5: SHT20 chamber temperature and humidity (0x40)
    if (TEST_SHT20) {
        Serial.println("[STEP 5] Cek SHT20 (Temperature/RH Sensor, 0x40)...");
        float sht20Temp, sht20Hum;
        sht20_online = sht20_read_measurement(sht20Temp, sht20Hum);
        if (sht20_online) {
            Serial.printf("[STEP 5 DONE] SHT20 respon OK | Suhu: %.2f C | RH: %.2f %%\n", sht20Temp, sht20Hum);
        } else {
            Serial.println("[STEP 5 WARN] SHT20 (0x40) belum merespon atau CRC salah. (Cek kabel I2C)");
        }
    } else {
        Serial.println("[STEP 5 SKIP] SHT20 tidak dipilih untuk pengujian.");
    }

    if (TEST_SHT30) {
    Serial.println("[STEP 6] Cek SHT30 (Temperature/RH Sensor, 0x44)...");
    float sht30Temp, sht30Hum;
    sht30_online = sht30_read_measurement(sht30Temp, sht30Hum);
    if (sht30_online) {
        Serial.printf("[STEP 6 DONE] SHT30 respon OK | Suhu: %.2f C | RH: %.2f %%\n", sht30Temp, sht30Hum);
    } else {
        Serial.println("[STEP 6 WARN] SHT30 (0x44) belum merespon atau CRC salah. (Cek kabel I2C / alamat sensor)");
    }

    } else {
        Serial.println("[STEP 6 SKIP] SHT30 tidak dipilih untuk pengujian.");
    }

    Serial.println();
    Serial.println("==================================================");
    Serial.println("  Mulai Pembacaan Loop Sensor (setiap 1 detik)");
    Serial.println("==================================================");
    Serial.println();
}

void loop() {
    nextion_poll();
    motor_update();
    if (hmiRefreshRequested) nextion_publish_dashboard();

    static uint32_t lastSensorSampleMs = 0;
    uint32_t now = millis();
    if (now - lastSensorSampleMs < SENSOR_SAMPLE_INTERVAL_MS) {
        delay(5);
        return;
    }
    lastSensorSampleMs = now;

    sampleCount++;
    Serial.println("--------------------------------------------------");
    Serial.printf(">>> Sample #%04u\n", sampleCount);

    // ── 1. RTD PT100 ─────────────────────────────────────────────────────
    if (TEST_PT100) {
    if (!max31865_online) {
        snprintf(hmiPt100, sizeof(hmiPt100), "OFFLINE");
        Serial.println("  [PT100]  Status: ERROR (MAX31865 SPI offline; config readback gagal)");
    } else {
        uint8_t rtd_msb, rtd_lsb;
        max31865_read_rtd(rtd_msb, rtd_lsb);
        uint8_t fault = rtd_lsb & 0x01;

        if (fault != 0) {
            uint8_t fault_reg = max31865_read_reg(MAX_REG_FAULT);
            snprintf(hmiPt100, sizeof(hmiPt100), "FAULT 0x%02X", fault_reg);
            Serial.printf("  [PT100]  Status: FAULT (0x%02X) | MSB=0x%02X LSB=0x%02X\n", fault_reg, rtd_msb, rtd_lsb);
            max31865_write_reg(MAX_REG_CONFIG, MAX_CONFIG_INIT | 0x02);
            delay(10);
            max31865_write_reg(MAX_REG_CONFIG, MAX_CONFIG_INIT);
        } else {
            uint16_t raw_adc = ((uint16_t)rtd_msb << 7) | ((uint16_t)rtd_lsb >> 1);
            if (raw_adc == 0) {
                snprintf(hmiPt100, sizeof(hmiPt100), "ERROR");
                Serial.println("  [PT100]  Status: ERROR (raw ADC = 0; periksa jalur RTD/MAX31865)");
            } else {
                float resistance = ((float)raw_adc / 32768.0f) * PT100_REF_OHM;
                float temp_c = pt100_r_to_c(resistance, PT100_NOMINAL_OHM);
                if (isfinite(temp_c)) {
                    snprintf(hmiPt100, sizeof(hmiPt100), "%.2f C", temp_c);
                } else {
                    snprintf(hmiPt100, sizeof(hmiPt100), "ERROR");
                }
                Serial.printf("  [PT100]  Raw: %5u | Resistansi: %.2f Ohm | Suhu: %.2f C\n", raw_adc, resistance, temp_c);
            }
        }
    }

    // ── 2. DFRobot O2 Sensor ─────────────────────────────────────────────
    }

    if (TEST_O2) {
    float o2_val = 0.0f;
    if (!o2_online) {
        // Recover automatically if sensor power or wiring returns after startup.
        o2_online = o2_probe();
    }

    if (o2_online && read_o2(o2_val)) {
        snprintf(hmiO2, sizeof(hmiO2), "%.2f %%", o2_val);
        Serial.printf("  [O2]     Konsentrasi O2: %.2f %%vol\n", o2_val);
    } else {
        o2_online = false;
        snprintf(hmiO2, sizeof(hmiO2), "OFFLINE");
        Serial.println("  [O2]     Status: ERROR (Sensor tidak merespon di 0x73)");
    }

    // ── 3. DFRobot pH via selected ADC path ────────────────────────────────
    }

    if (TEST_PH) {
        if (PH_INPUT_MODE == PH_INPUT_DIRECT_ADC) {
            uint16_t raw;
            float millivolts;
            read_ph_adc(raw, millivolts);
            float voltage = millivolts / 1000.0f;
            float ph = ph_mv_to_value(millivolts);

            if (millivolts >= 50.0f && millivolts <= 3000.0f && isfinite(ph)) {
                snprintf(hmiPh, sizeof(hmiPh), "%.2f", ph);
            } else {
                snprintf(hmiPh, sizeof(hmiPh), "VOLT ERR");
            }

            Serial.printf(
                "[pH/ADC] GPIO15 Raw: %4u | Tegangan: %.4f V | Nilai pH: %.2f\n",
                raw, voltage, ph
            );
            if (millivolts < 50.0f || millivolts > 3000.0f) {
                Serial.println("         WARN: Tegangan pH di luar rentang 0-3.0 V; cek kabel A/GND.");
            }
        } else {
            if (!ads1115_online) {
                // Recover automatically when ADS1115 power/wiring returns.
                ads1115_online = ads1115_initialize();
            }

            int16_t counts = 0;
            float millivolts = 0.0f;
            if (ads1115_online && read_ph_ads1115(counts, millivolts)) {
                float voltage = millivolts / 1000.0f;
                float ph = ph_mv_to_value(millivolts);
                if (millivolts >= 50.0f && millivolts <= 3000.0f && isfinite(ph)) {
                    snprintf(hmiPh, sizeof(hmiPh), "%.2f", ph);
                } else {
                    snprintf(hmiPh, sizeof(hmiPh), "VOLT ERR");
                }
                Serial.printf(
                    "[pH/ADS] A0 Raw: %6d | Tegangan: %.4f V | Nilai pH: %.2f\n",
                    counts, voltage, ph
                );
                if (millivolts < 50.0f || millivolts > 3000.0f) {
                    Serial.println("         WARN: Tegangan pH di luar rentang 0-3.0 V; cek A0/GND.");
                }
            } else {
                ads1115_online = false;
                snprintf(hmiPh, sizeof(hmiPh), "OFFLINE");
                Serial.println("[pH/ADS] Status: ERROR (ADS1115 0x48 tidak merespon)");
            }
        }

    // ── 4. SCD41 CO2 Sensor ─────────────────────────────────────────────
    }

    if (TEST_CO2) {
    bool ready = false;
    if (!scd41_online) {
        snprintf(hmiCo2, sizeof(hmiCo2), "OFFLINE");
        snprintf(hmiScdTemp, sizeof(hmiScdTemp), "OFFLINE");
        snprintf(hmiScdRh, sizeof(hmiScdRh), "OFFLINE");
        Serial.println("  [CO2]    Status: ERROR (SCD41 tidak terdeteksi saat startup)");
    } else if (scd41_data_ready(ready)) {
        if (ready) {
            uint16_t co2;
            float temp, hum;
            if (scd41_read_measurement(co2, temp, hum)) {
                snprintf(hmiCo2, sizeof(hmiCo2), "%u ppm", co2);
                snprintf(hmiScdTemp, sizeof(hmiScdTemp), "%.2f C", temp);
                snprintf(hmiScdRh, sizeof(hmiScdRh), "%.2f %%", hum);
                Serial.printf("  [CO2]    SCD41: %5u ppm | Suhu: %.2f C | RH: %.2f %%\n", co2, temp, hum);
            } else {
                snprintf(hmiCo2, sizeof(hmiCo2), "ERROR");
                snprintf(hmiScdTemp, sizeof(hmiScdTemp), "ERROR");
                snprintf(hmiScdRh, sizeof(hmiScdRh), "ERROR");
                Serial.println("  [CO2]    Status: ERROR (Gagal membaca SCD41 / CRC salah)");
            }
        } else {
            Serial.println("  [CO2]    Status: Menunggu data (update setiap 5 detik)");
        }
    } else {
        snprintf(hmiCo2, sizeof(hmiCo2), "OFFLINE");
        snprintf(hmiScdTemp, sizeof(hmiScdTemp), "OFFLINE");
        snprintf(hmiScdRh, sizeof(hmiScdRh), "OFFLINE");
        Serial.println("  [CO2]    Status: ERROR (SCD41 tidak merespon di 0x62)");
    }

    // 5. SHT20 chamber temperature and relative humidity
    }

    if (TEST_SHT20) {
        float sht20Temp, sht20Hum;
        if (sht20_read_measurement(sht20Temp, sht20Hum)) {
            sht20_online = true;
            snprintf(hmiSht20Temp, sizeof(hmiSht20Temp), "%.2f C", sht20Temp);
            snprintf(hmiSht20Rh, sizeof(hmiSht20Rh), "%.2f %%", sht20Hum);
            Serial.printf("  [SHT20]  Suhu: %.2f C | RH: %.2f %%\n", sht20Temp, sht20Hum);
        } else {
            sht20_online = false;
            snprintf(hmiSht20Temp, sizeof(hmiSht20Temp), "ERROR");
            snprintf(hmiSht20Rh, sizeof(hmiSht20Rh), "ERROR");
            Serial.println("  [SHT20]  Status: ERROR (Sensor tidak merespon di 0x40 atau CRC salah)");
        }
    }

    // 6. SHT30, if a separate SHT30 is installed
    if (TEST_SHT30) {
    float sht30Temp, sht30Hum;
    if (sht30_read_measurement(sht30Temp, sht30Hum)) {
        sht30_online = true;
        snprintf(hmiSht30Temp, sizeof(hmiSht30Temp), "%.2f C", sht30Temp);
        snprintf(hmiSht30Rh, sizeof(hmiSht30Rh), "%.2f %%", sht30Hum);
        Serial.printf("  [SHT30]  Suhu: %.2f C | RH: %.2f %%\n", sht30Temp, sht30Hum);
    } else {
        sht30_online = false;
        snprintf(hmiSht30Temp, sizeof(hmiSht30Temp), "ERROR");
        snprintf(hmiSht30Rh, sizeof(hmiSht30Rh), "ERROR");
        Serial.println("  [SHT30]  Status: ERROR (Sensor tidak merespon di 0x44 atau CRC salah)");
    }

    }

    nextion_poll();
    motor_update();
    hmiRefreshRequested = true;
    nextion_publish_dashboard();
}
