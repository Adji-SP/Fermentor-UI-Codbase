#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "DFRobot_OxygenSensor.h"
#include "hmi_motor.h"

// Set to the confirmed ESP GPIO carrying the DS18B20 data (DQ) wire.
// -1 safely disables it; a DS18B20 cannot connect through MAX31865 SPI.
#ifndef DS18B20_DATA_PIN
#define DS18B20_DATA_PIN -1
#endif
#if DS18B20_DATA_PIN >= 0
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(DS18B20_DATA_PIN);
DallasTemperature ds18b20(&oneWire);
uint32_t dsRequestMs = 0;
#endif

constexpr uint8_t I2C_SDA = 8;
constexpr uint8_t I2C_SCL = 9;
constexpr uint8_t ADS_ADDR = 0x48;
constexpr uint8_t ADS_CH_PH = 0;
constexpr uint8_t ADS_CH_PRESSURE = 1;
constexpr uint8_t O2_ADDR = 0x70;
constexpr uint8_t O2_SAMPLE = 10;
constexpr uint8_t SCD_ADDR = 0x62;
constexpr uint8_t SHT30_ADDR = 0x44;
constexpr uint32_t SAMPLE_INTERVAL_MS = 5000;

// Replace these example values after calibration with pH 7 and pH 4 buffers.
float PH_MV_NETRAL = 1500.0f;
float PH_MV_ASAM = 2032.44f;
// Pressure transducer on ADS1115 A1: 0.5-4.5 V = 0-1.2 MPa.
float P_V_NOL = 0.5f;
float P_V_PENUH = 4.5f;
float P_MPA_MAX = 1.2f;

Adafruit_ADS1115 ads;
DFRobot_OxygenSensor oxygen;
bool adsOk = false, o2Ok = false, scdOk = false;
uint32_t lastSampleMs = 0;
HmiReadings readings;

void waitWithHmi(uint32_t durationMs)
{
  const uint32_t start = millis();
  while (millis() - start < durationMs)
  {
    hmiMotorTick();
    delay(1);
  }
}

bool i2cPresent(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

uint8_t crc8(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < length; ++i)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

bool writeCommand16(uint8_t address, uint16_t command)
{
  Wire.beginTransmission(address);
  Wire.write(command >> 8);
  Wire.write(command & 0xFF);
  return Wire.endTransmission() == 0;
}

bool scdStart()
{
  if (!i2cPresent(SCD_ADDR))
    return false;
  writeCommand16(SCD_ADDR, 0x3F86);
  waitWithHmi(500);
  return writeCommand16(SCD_ADDR, 0x21B1);
}

enum class ScdResult : uint8_t
{
  WAIT,
  OK,
  ERROR
};

ScdResult scdRead(uint16_t &co2, float &temperature, float &humidity)
{
  uint8_t status[3], data[9];
  if (!writeCommand16(SCD_ADDR, 0xE4B8))
    return ScdResult::ERROR;
  waitWithHmi(2);
  if (Wire.requestFrom(SCD_ADDR, (uint8_t)3) != 3)
    return ScdResult::ERROR;
  for (uint8_t i = 0; i < 3; ++i)
    status[i] = Wire.read();
  if (crc8(status, 2) != status[2])
    return ScdResult::ERROR;
  if (((((uint16_t)status[0] << 8) | status[1]) & 0x07FF) == 0)
    return ScdResult::WAIT;
  if (!writeCommand16(SCD_ADDR, 0xEC05))
    return ScdResult::ERROR;
  waitWithHmi(2);
  if (Wire.requestFrom(SCD_ADDR, (uint8_t)9) != 9)
    return ScdResult::ERROR;
  for (uint8_t i = 0; i < 9; ++i)
    data[i] = Wire.read();
  for (uint8_t i = 0; i < 9; i += 3)
    if (crc8(&data[i], 2) != data[i + 2])
      return ScdResult::ERROR;
  co2 = ((uint16_t)data[0] << 8) | data[1];
  temperature = -45.0f + 175.0f * (((uint16_t)data[3] << 8) | data[4]) / 65535.0f;
  humidity = 100.0f * (((uint16_t)data[6] << 8) | data[7]) / 65535.0f;
  return ScdResult::OK;
}

bool sht30Read(float &temperature, float &humidity)
{
  if (!writeCommand16(SHT30_ADDR, 0x2400))
    return false;
  waitWithHmi(20);
  uint8_t data[6];
  if (Wire.requestFrom(SHT30_ADDR, (uint8_t)6) != 6)
    return false;
  for (uint8_t i = 0; i < 6; ++i)
    data[i] = Wire.read();
  if (crc8(data, 2) != data[2] || crc8(&data[3], 2) != data[5])
    return false;
  temperature = -45.0f + 175.0f * (((uint16_t)data[0] << 8) | data[1]) / 65535.0f;
  humidity = 100.0f * (((uint16_t)data[3] << 8) | data[4]) / 65535.0f;
  return true;
}

bool readVoltage(uint8_t channel, adsGain_t gain, float &voltage,
                 int16_t *averageRaw = nullptr)
{
  ads.setGain(gain);
  int32_t total = 0;
  constexpr uint8_t count = 10;
  for (uint8_t i = 0; i < count; ++i)
  {
    ads.startADCReading(MUX_BY_CHANNEL[channel], false);
    const uint32_t started = millis();
    while (!ads.conversionComplete())
    {
      hmiMotorTick();
      if (millis() - started > 50)
        return false;
      delay(1);
    }
    total += ads.getLastConversionResults();
  }
  const int16_t raw = (int16_t)(total / count);
  if (averageRaw)
    *averageRaw = raw;
  voltage = ads.computeVolts(raw);
  return isfinite(voltage);
}

float calculatePh(float millivolts)
{
  const float span = PH_MV_ASAM - PH_MV_NETRAL;
  if (fabsf(span) < 1.0f)
    return NAN;
  return 7.0f + (4.0f - 7.0f) * (millivolts - PH_MV_NETRAL) / span;
}

void readDs18b20()
{
#if DS18B20_DATA_PIN >= 0
  if (millis() - dsRequestMs < 400)
  {
    readings.processTemp = {NAN, ReadingStatus::WAIT};
    Serial.println("DS18B20   : WAIT (11-bit conversion)");
    return;
  }
  const float temperature = ds18b20.getTempCByIndex(0);
  if (temperature != DEVICE_DISCONNECTED_C && isfinite(temperature) &&
      temperature >= -55.0f && temperature <= 125.0f)
  {
    readings.processTemp = {temperature, ReadingStatus::OK};
    Serial.printf("DS18B20   : %.2f C\n", temperature);
  }
  else
  {
    readings.processTemp = {NAN, ReadingStatus::ERROR};
    Serial.println("DS18B20   : ERROR (check DQ, 3.3 V and pull-up)");
  }
  ds18b20.requestTemperatures();
  dsRequestMs = millis();
#else
  readings.processTemp = {NAN, ReadingStatus::OFFLINE};
  Serial.println("DS18B20   : OFFLINE (set DS18B20_DATA_PIN to the wired GPIO)");
#endif
}

void sampleSensors()
{
  Serial.println("\n================ SENSOR READINGS ================");
  readDs18b20();

  if (!adsOk)
    adsOk = ads.begin(ADS_ADDR, &Wire);
  readings.ph = {NAN, adsOk ? ReadingStatus::ERROR : ReadingStatus::OFFLINE};
  readings.pressure = {NAN, adsOk ? ReadingStatus::ERROR : ReadingStatus::OFFLINE};
  if (adsOk)
  {
    float voltage;
    int16_t phRaw = 0;
    if (readVoltage(ADS_CH_PH, GAIN_ONE, voltage, &phRaw))
    {
      const float ph = calculatePh(voltage * 1000.0f);
      Serial.printf("[pH/ADC] A0 raw=%d | %.1f mV | calculated=%.2f | pH7=%.1f mV pH4=%.1f mV\n",
                    (int)phRaw, voltage * 1000.0f, ph, PH_MV_NETRAL, PH_MV_ASAM);
      if (isfinite(ph) && ph >= 0.0f && ph <= 14.0f)
      {
        readings.ph = {ph, ReadingStatus::OK};
        Serial.printf("pH        : %.2f (%.1f mV)\n", ph, voltage * 1000.0f);
      }
      else
        Serial.printf("pH        : ERROR (raw=%d, %.1f mV -> %.2f pH; out of range or calibration)\n",
                      (int)phRaw, voltage * 1000.0f, ph);
    }
    else
    {
      Serial.println("pH        : ERROR (ADS1115 A0 read failed or conversion timeout)");
    }
    if (readVoltage(ADS_CH_PRESSURE, GAIN_TWOTHIRDS, voltage))
    {
      const float mpa = (voltage - P_V_NOL) * P_MPA_MAX / (P_V_PENUH - P_V_NOL);
      if (voltage >= 0.4f && voltage <= 4.6f)
      {
        readings.pressure = {mpa * 10.0f, ReadingStatus::OK}; // bar
        Serial.printf("Pressure  : %.1f kPa = %.3f bar (%.3f V)\n", mpa * 1000.0f, mpa * 10.0f, voltage);
      }
      else
        Serial.printf("Pressure  : ERROR (%.3f V, check 0.5-4.5 V sensor)\n", voltage);
    }
  }
  else
    Serial.println("pH/ADS    : OFFLINE (0x48)");

  if (!o2Ok)
    o2Ok = oxygen.begin(O2_ADDR);
  readings.oxygen = {NAN, o2Ok ? ReadingStatus::ERROR : ReadingStatus::OFFLINE};
  if (o2Ok)
  {
    const float value = oxygen.getOxygenData(O2_SAMPLE);
    if (isfinite(value) && value >= 0.0f && value <= 30.0f)
    {
      readings.oxygen = {value, ReadingStatus::OK};
      Serial.printf("O2        : %.2f %%Vol\n", value);
    }
    else
      Serial.printf("O2        : ERROR (%.2f %%Vol, inspect sensor/calibration)\n", value);
  }
  else
    Serial.println("O2        : OFFLINE (0x70)");

  if (!scdOk)
    scdOk = scdStart();
  readings.co2 = {NAN, scdOk ? ReadingStatus::WAIT : ReadingStatus::OFFLINE};
  readings.scdTemp = readings.co2;
  readings.scdRh = readings.co2;
  if (scdOk)
  {
    uint16_t co2;
    float temp, rh;
    const ScdResult result = scdRead(co2, temp, rh);
    if (result == ScdResult::OK)
    {
      readings.co2 = {(float)co2, ReadingStatus::OK};
      readings.scdTemp = {temp, ReadingStatus::OK};
      readings.scdRh = {rh, ReadingStatus::OK};
      Serial.printf("SCD41     : CO2 %u ppm, %.2f C, %.1f %%RH\n", co2, temp, rh);
    }
    else if (result == ScdResult::ERROR)
    {
      readings.co2 = readings.scdTemp = readings.scdRh = {NAN, ReadingStatus::ERROR};
      scdOk = false;
      Serial.println("SCD41     : ERROR (I2C/CRC)");
    }
    else
      Serial.println("SCD41     : WAIT (new data every ~5 s)");
  }
  else
    Serial.println("SCD41     : OFFLINE (0x62)");

  readings.shtTemp = readings.shtRh = {NAN, ReadingStatus::OFFLINE};
  if (i2cPresent(SHT30_ADDR))
  {
    float temp, rh;
    if (sht30Read(temp, rh))
    {
      readings.shtTemp = {temp, ReadingStatus::OK};
      readings.shtRh = {rh, ReadingStatus::OK};
      Serial.printf("SHT30     : %.2f C, %.1f %%RH\n", temp, rh);
    }
    else
    {
      readings.shtTemp = readings.shtRh = {NAN, ReadingStatus::ERROR};
      Serial.println("SHT30     : ERROR (I2C/CRC)");
    }
  }
  else
    Serial.println("SHT30     : OFFLINE (0x44)");
  hmiMotorSetReadings(readings);
  hmiMotorTick();
}

void setup()
{
  Serial.begin(115200);
  hmiMotorBegin(); // Motor outputs are disabled before sensor initialization.
  waitWithHmi(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(100);
  Serial.println("=== Fermentor Kopi: DS18B20, ADS1115, O2, SCD41, SHT30 ===");
  Serial.println("I2C: SDA=GPIO8, SCL=GPIO9, 100 kHz, one shared level shifter");
  adsOk = ads.begin(ADS_ADDR, &Wire);
  o2Ok = oxygen.begin(O2_ADDR);
  scdOk = scdStart();
  Serial.println(adsOk ? "[OK] ADS1115 0x48" : "[OFFLINE] ADS1115 0x48");
  Serial.println(o2Ok ? "[OK] O2 0x70" : "[OFFLINE] O2 0x70");
  Serial.println(scdOk ? "[OK] SCD41 0x62" : "[OFFLINE] SCD41 0x62");
  Serial.println(i2cPresent(SHT30_ADDR) ? "[OK] SHT30 0x44" : "[OFFLINE] SHT30 0x44");
#if DS18B20_DATA_PIN >= 0
  ds18b20.begin();
  ds18b20.setResolution(11);
  ds18b20.setWaitForConversion(false);
  ds18b20.requestTemperatures();
  dsRequestMs = millis();
  Serial.printf("[DS18B20] DQ=GPIO%d, 11-bit, %d device(s)\n", DS18B20_DATA_PIN, ds18b20.getDeviceCount());
#else
  Serial.println("[DS18B20] DQ GPIO not configured; probe safely disabled");
#endif
  hmiMotorSetReadings(readings);
  lastSampleMs = millis() - SAMPLE_INTERVAL_MS;
}

void loop()
{
  hmiMotorTick();
  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS)
  {
    lastSampleMs = millis();
    sampleSensors();
  }
  delay(1);
}
