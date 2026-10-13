#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MAX31865.h>
#include "DFRobot_OxygenSensor.h"

// ===================== PIN (sesuai skematik) =====================
#define I2C_SDA   8
#define I2C_SCL   9
#define MAX_CS    10
#define MAX_MOSI  11   // SDI
#define MAX_CLK   12
#define MAX_MISO  13   // SDO

// ===================== ADS1115 =====================
#define ADS_ADDR         0x48
#define ADS_CH_PH        0
#define ADS_CH_PRESSURE  1   // J5 -> A1 di skematik

// Kalibrasi pH 2 titik (mV). Ganti setelah kalibrasi dengan buffer pH 7 & pH 4
float PH_MV_NETRAL = 1500.0;   // tegangan saat pH 7.00
float PH_MV_ASAM   = 2032.44;  // tegangan saat pH 4.00

// Sensor tekanan: 0.5-4.5 V = 0-1.2 MPa
float P_V_NOL   = 0.5;
float P_V_PENUH = 4.5;
float P_MPA_MAX = 1.2;

// ===================== RTD PT100 (MAX31865) =====================
#define RTD_NOMINAL 100.0
#define RTD_REF     430.0

// ===================== Sensor I2C lain =====================
#define O2_ADDR     0x70   // sesuai hasil scan Anda (A0=0, A1=0)
#define O2_SAMPLE   10
#define SCD_ADDR    0x62
#define SHT3X_ADDR  0x44
#define SHT2X_ADDR  0x40

Adafruit_ADS1115 ads;
Adafruit_MAX31865 rtd(MAX_CS, MAX_MOSI, MAX_MISO, MAX_CLK);
DFRobot_OxygenSensor oxygen;

bool adsOk = false, o2Ok = false, scdOk = false;
uint8_t shtAddr = 0;

// ===================== Fungsi bantu I2C =====================
uint8_t crc8(const uint8_t *d, int len, uint8_t init) {
  uint8_t crc = init;
  for (int i = 0; i < len; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

bool tulisCmd16(uint8_t addr, uint16_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  return Wire.endTransmission() == 0;
}

// ===================== SCD40 =====================
bool scdMulai() {
  tulisCmd16(SCD_ADDR, 0x3F86);
  delay(500);
  return tulisCmd16(SCD_ADDR, 0x21B1);
}

bool scdBaca(uint16_t &co2, float &suhu, float &rh) {
  uint8_t s[3], b[9];

  if (!tulisCmd16(SCD_ADDR, 0xE4B8)) return false;
  delay(2);
  if (Wire.requestFrom((uint8_t)SCD_ADDR, (uint8_t)3) != 3) return false;
  for (int i = 0; i < 3; i++) s[i] = Wire.read();
  if (crc8(s, 2, 0xFF) != s[2]) return false;
  if ((((s[0] << 8) | s[1]) & 0x07FF) == 0) return false;

  if (!tulisCmd16(SCD_ADDR, 0xEC05)) return false;
  delay(2);
  if (Wire.requestFrom((uint8_t)SCD_ADDR, (uint8_t)9) != 9) return false;
  for (int i = 0; i < 9; i++) b[i] = Wire.read();
  for (int i = 0; i < 9; i += 3)
    if (crc8(&b[i], 2, 0xFF) != b[i + 2]) return false;

  co2  = (b[0] << 8) | b[1];
  suhu = -45.0 + 175.0 * ((b[3] << 8) | b[4]) / 65535.0;
  rh   = 100.0 * ((b[6] << 8) | b[7]) / 65535.0;
  return true;
}

// ===================== SHT =====================
bool sht3xBaca(float &suhu, float &rh) {
  if (!tulisCmd16(SHT3X_ADDR, 0x2400)) return false;
  delay(20);
  uint8_t b[6];
  if (Wire.requestFrom((uint8_t)SHT3X_ADDR, (uint8_t)6) != 6) return false;
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(&b[0], 2, 0xFF) != b[2] || crc8(&b[3], 2, 0xFF) != b[5]) return false;
  suhu = -45.0 + 175.0 * ((b[0] << 8) | b[1]) / 65535.0;
  rh   = 100.0 * ((b[3] << 8) | b[4]) / 65535.0;
  return true;
}

bool sht20Raw(uint8_t cmd, uint16_t &raw, uint16_t tunggu) {
  Wire.beginTransmission(SHT2X_ADDR);
  Wire.write(cmd);
  if (Wire.endTransmission() != 0) return false;
  delay(tunggu);
  uint8_t b[3];
  if (Wire.requestFrom((uint8_t)SHT2X_ADDR, (uint8_t)3) != 3) return false;
  for (int i = 0; i < 3; i++) b[i] = Wire.read();
  if (crc8(b, 2, 0x00) != b[2]) return false;
  raw = ((b[0] << 8) | b[1]) & 0xFFFC;
  return true;
}

bool sht20Baca(float &suhu, float &rh) {
  uint16_t rt, rr;
  if (!sht20Raw(0xF3, rt, 90)) return false;
  if (!sht20Raw(0xF5, rr, 40)) return false;
  suhu = -46.85 + 175.72 * rt / 65536.0;
  rh   = -6.0 + 125.0 * rr / 65536.0;
  return true;
}

bool shtBaca(float &suhu, float &rh) {
  if (shtAddr == SHT3X_ADDR) return sht3xBaca(suhu, rh);
  if (shtAddr == SHT2X_ADDR) return sht20Baca(suhu, rh);
  return false;
}

// ===================== ADS1115 =====================
float bacaVolt(uint8_t ch, adsGain_t gain, int n = 10) {
  ads.setGain(gain);
  long sum = 0;
  for (int i = 0; i < n; i++) sum += ads.readADC_SingleEnded(ch);
  return ads.computeVolts((int16_t)(sum / n));
}

float hitungPH(float mV) {
  float slope = (4.0 - 7.0) / (PH_MV_ASAM - PH_MV_NETRAL);
  return 7.0 + slope * (mV - PH_MV_NETRAL);
}

// ===================== RTD =====================
float bacaRTD() {
  float t = rtd.temperature(RTD_NOMINAL, RTD_REF);
  uint8_t fault = rtd.readFault();
  if (fault) {
    Serial.printf("  RTD FAULT 0x%02X (cek kabel PT100 / jumper 3-wire)\n", fault);
    rtd.clearFault();
    return NAN;
  }
  return t;
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.println("=== Monitoring Semua Sensor - ESP32-S3 ===");

  adsOk = ads.begin(ADS_ADDR, &Wire);
  Serial.println(adsOk ? "[OK]    ADS1115" : "[GAGAL] ADS1115");

  o2Ok = oxygen.begin(O2_ADDR);
  Serial.println(o2Ok ? "[OK]    Sensor O2" : "[GAGAL] Sensor O2");

  scdOk = scdMulai();
  Serial.println(scdOk ? "[OK]    SCD40" : "[GAGAL] SCD40");

  Wire.beginTransmission(SHT3X_ADDR);
  if (Wire.endTransmission() == 0) {
    shtAddr = SHT3X_ADDR;
    Serial.println("[OK]    SHT3x (0x44)");
  } else {
    Wire.beginTransmission(SHT2X_ADDR);
    if (Wire.endTransmission() == 0) {
      shtAddr = SHT2X_ADDR;
      Serial.println("[OK]    SHT20 (0x40)");
    } else {
      Serial.println("[GAGAL] SHT tidak ditemukan");
    }
  }

  rtd.begin(MAX31865_3WIRE);
  Serial.println("[OK]    MAX31865 (PT100 3-wire)");
}

// ===================== LOOP =====================
void loop() {
  static uint32_t tTerakhir = 0;
  static uint16_t co2 = 0;
  static float scdT = NAN, scdRH = NAN;

  if (millis() - tTerakhir < 5000) return;
  tTerakhir = millis();

  Serial.println("\n================ PEMBACAAN SENSOR ================");

  if (adsOk) {
    float vPH = bacaVolt(ADS_CH_PH, GAIN_ONE);
    float mV = vPH * 1000.0;
    Serial.printf("pH        : %.2f  (%.1f mV)\n", hitungPH(mV), mV);

    float vP = bacaVolt(ADS_CH_PRESSURE, GAIN_TWOTHIRDS);
    float mpa = (vP - P_V_NOL) * P_MPA_MAX / (P_V_PENUH - P_V_NOL);
    Serial.printf("Tekanan   : %.1f kPa = %.3f bar  (%.3f V)\n", mpa * 1000.0, mpa * 10.0, vP);
    if (vP < 0.4 || vP > 4.6)
      Serial.println("  PERINGATAN: tegangan di luar 0.5-4.5 V, cek kabel/sensor tekanan");
  }

  float tRTD = bacaRTD();
  if (!isnan(tRTD)) Serial.printf("RTD PT100 : %.2f C\n", tRTD);

  if (o2Ok) Serial.printf("O2        : %.2f %%Vol\n", oxygen.getOxygenData(O2_SAMPLE));

  if (scdOk) {
    if (scdBaca(co2, scdT, scdRH)) {
      Serial.printf("SCD40     : CO2 %u ppm, %.2f C, %.1f %%RH\n", co2, scdT, scdRH);
    } else if (!isnan(scdT)) {
      Serial.printf("SCD40     : CO2 %u ppm, %.2f C, %.1f %%RH (data lama)\n", co2, scdT, scdRH);
    } else {
      Serial.println("SCD40     : menunggu data pertama (~5 detik)");
    }
  }

  float shtT, shtRH;
  if (shtBaca(shtT, shtRH))
    Serial.printf("SHT       : %.2f C, %.1f %%RH\n", shtT, shtRH);
  else if (shtAddr)
    Serial.println("SHT       : gagal membaca");
}