use embedded_hal::{
    delay::DelayNs,
    i2c::{Error, ErrorKind},
};

pub const ADS_ADDR: u8 = 0x48;
pub const O2_ADDR: u8 = 0x70;
pub const SCD_ADDR: u8 = 0x62;
pub const SHT30_ADDR: u8 = 0x44;
pub const O2_WARMUP_MS: u64 = 180_000;
pub const SCD_POWER_UP_MS: u32 = 30;
/// Keep bus failures distinct from invalid samples for HMI and serial diagnostics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SensorError {
    Bus(ErrorKind),
    Crc,
    InvalidData,
    Timeout,
    InvalidChannel,
}

impl SensorError {
    pub fn reading(self) -> Reading {
        if matches!(self, Self::Bus(ErrorKind::NoAcknowledge(_))) {
            Reading::offline()
        } else {
            Reading::error()
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Status {
    Wait,
    Warmup(u16), // Remaining whole seconds, never a sensor measurement.
    Ok,
    Offline,
    Error,
}

#[derive(Clone, Copy)]
pub struct Reading {
    pub value: f32,
    pub status: Status,
}

impl Reading {
    pub const fn new(value: f32, status: Status) -> Self {
        Self { value, status }
    }
    pub const fn wait() -> Self {
        Self::new(f32::NAN, Status::Wait)
    }
    pub const fn warmup(seconds: u16) -> Self {
        Self::new(f32::NAN, Status::Warmup(seconds))
    }
    pub const fn offline() -> Self {
        Self::new(f32::NAN, Status::Offline)
    }
    pub const fn error() -> Self {
        Self::new(f32::NAN, Status::Error)
    }
    pub const fn ok(value: f32) -> Self {
        Self::new(value, Status::Ok)
    }
}

/// Startup display policy only; sampling and motor/HMI service never pause.
/// A valid O2 sample permanently takes priority over the startup countdown.
pub struct O2Warmup {
    started_ms: u64,
    received_valid: bool,
}

impl O2Warmup {
    pub const fn new(started_ms: u64) -> Self {
        Self {
            started_ms,
            received_valid: false,
        }
    }

    pub fn remaining_seconds(&self, now_ms: u64) -> u16 {
        if self.received_valid {
            return 0;
        }
        O2_WARMUP_MS
            .saturating_sub(now_ms.saturating_sub(self.started_ms))
            .div_ceil(1_000) as u16
    }

    pub fn reading(&mut self, result: Result<f32, SensorError>, now_ms: u64) -> Reading {
        let error = match result {
            Ok(value) if value.is_finite() && (0.0..=30.0).contains(&value) => {
                self.received_valid = true;
                return Reading::ok(value);
            }
            Ok(_) => SensorError::InvalidData,
            Err(error) => error,
        };
        let seconds = self.remaining_seconds(now_ms);
        // Only an unusable measurement gets startup grace. A disconnected
        // device, I2C fault or timeout must still be visible as a real fault.
        if error == SensorError::InvalidData && seconds > 0 {
            Reading::warmup(seconds)
        } else {
            error.reading()
        }
    }
}

#[derive(Clone, Copy)]
pub struct Readings {
    pub process_temp: Reading,
    pub oxygen: Reading,
    pub ph: Reading,
    pub co2: Reading,
    pub scd_temp: Reading,
    pub scd_rh: Reading,
    pub sht_temp: Reading,
    pub sht_rh: Reading,
    pub pressure: Reading,
}

impl Readings {
    pub const fn waiting() -> Self {
        Self {
            process_temp: Reading::wait(),
            oxygen: Reading::wait(),
            ph: Reading::wait(),
            co2: Reading::wait(),
            scd_temp: Reading::wait(),
            scd_rh: Reading::wait(),
            sht_temp: Reading::wait(),
            sht_rh: Reading::wait(),
            pressure: Reading::wait(),
        }
    }
}

// ADS1115: A0 pH at +/-4.096 V; A1 pressure at +/-6.144 V.
// 128 SPS single-shot conversions; average 10 samples like the C++ firmware.
pub fn read_ads_voltage<I: embedded_hal::i2c::I2c>(
    i2c: &mut I,
    channel: u8,
    delay: &mut impl DelayNs,
) -> Result<f32, SensorError> {
    if channel > 1 {
        return Err(SensorError::InvalidChannel);
    }
    let mux = if channel == 0 { 0x4000 } else { 0x5000 };
    let gain = if channel == 0 { 0x0200 } else { 0x0000 };
    let config: u16 = 0x8000 | mux | gain | 0x0100 | 0x0080 | 0x0003;
    let mut total = 0i32;
    for _ in 0..10 {
        i2c.write(ADS_ADDR, &[0x01, (config >> 8) as u8, config as u8])
            .map_err(|error| SensorError::Bus(error.kind()))?;
        delay.delay_ms(9);
        let mut state = [0u8; 2];
        let mut ready = false;
        for _ in 0..30 {
            i2c.write_read(ADS_ADDR, &[0x01], &mut state)
                .map_err(|error| SensorError::Bus(error.kind()))?;
            if state[0] & 0x80 != 0 {
                ready = true;
                break;
            }
            delay.delay_ms(1);
        }
        if !ready {
            return Err(SensorError::Timeout);
        }
        let mut raw = [0u8; 2];
        i2c.write_read(ADS_ADDR, &[0x00], &mut raw)
            .map_err(|error| SensorError::Bus(error.kind()))?;
        total += i16::from_be_bytes(raw) as i32;
    }
    let full_scale = if channel == 0 { 4.096 } else { 6.144 };
    // Match the C++ int16_t average before Adafruit computeVolts.
    Ok((total / 10) as f32 * full_scale / 32_768.0)
}

pub fn ph_from_voltage(voltage: f32) -> Option<f32> {
    const NEUTRAL_MV: f32 = 1500.0;
    const ACID_MV: f32 = 2032.44;
    let ph = 7.0 + (4.0 - 7.0) * (voltage * 1_000.0 - NEUTRAL_MV) / (ACID_MV - NEUTRAL_MV);
    if ph.is_finite() && (0.0..=14.0).contains(&ph) {
        Some(ph)
    } else {
        None
    }
}

pub fn pressure_from_voltage(voltage: f32) -> Option<f32> {
    if !(0.4..=4.6).contains(&voltage) {
        return None;
    }
    Some(((voltage - 0.5) * 1.2 / (4.5 - 0.5)) * 10.0) // bar
}

pub struct O2Average {
    samples: [f32; 10],
    count: usize,
    next: usize,
}

impl Default for O2Average {
    fn default() -> Self {
        Self::new()
    }
}

impl O2Average {
    pub const fn new() -> Self {
        Self {
            samples: [0.0; 10],
            count: 0,
            next: 0,
        }
    }

    pub fn read<I: embedded_hal::i2c::I2c>(
        &mut self,
        i2c: &mut I,
        delay: &mut impl DelayNs,
    ) -> Result<f32, SensorError> {
        // SEN0322 key and oxygen registers; calibration is read, never overwritten.
        i2c.write(O2_ADDR, &[0x0A])
            .map_err(|error| SensorError::Bus(error.kind()))?;
        delay.delay_ms(50);
        let mut key_bytes = [0u8; 2];
        i2c.read(O2_ADDR, &mut key_bytes)
            .map_err(|error| SensorError::Bus(error.kind()))?;
        let stored_key = u16::from_le_bytes(key_bytes);
        let key = if stored_key == 0 {
            20.9 / 120.0
        } else {
            stored_key as f32 / 1_000.0
        };
        i2c.write(O2_ADDR, &[0x03])
            .map_err(|error| SensorError::Bus(error.kind()))?;
        delay.delay_ms(100);
        let mut data = [0u8; 3];
        i2c.read(O2_ADDR, &mut data)
            .map_err(|error| SensorError::Bus(error.kind()))?;
        let raw = data[0] as f32 + data[1] as f32 / 10.0 + data[2] as f32 / 100.0;
        let value = key * raw;
        if !value.is_finite() || !(0.0..=30.0).contains(&value) {
            return Err(SensorError::InvalidData);
        }
        self.samples[self.next] = value;
        self.next = (self.next + 1) % self.samples.len();
        self.count = (self.count + 1).min(self.samples.len());
        Ok(self.samples[..self.count].iter().sum::<f32>() / self.count as f32)
    }
}

fn sensirion_crc(data: &[u8]) -> u8 {
    let mut crc = 0xFFu8;
    for byte in data {
        crc ^= *byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0x31
            } else {
                crc << 1
            };
        }
    }
    crc
}

pub fn scd_start<I: embedded_hal::i2c::I2c>(
    i2c: &mut I,
    delay: &mut impl DelayNs,
) -> Result<(), SensorError> {
    // Stop can fail when a previous measurement is not active. Like the PIO
    // firmware, still wait before trying start; its ACK determines success.
    let _ = i2c.write(SCD_ADDR, &0x3F86u16.to_be_bytes());
    delay.delay_ms(500);
    i2c.write(SCD_ADDR, &0x21B1u16.to_be_bytes())
        .map_err(|error| SensorError::Bus(error.kind()))
}

#[derive(Debug, PartialEq)]
pub enum ScdSample {
    Waiting,
    Values(u16, f32, f32),
}

pub fn scd_read<I: embedded_hal::i2c::I2c>(
    i2c: &mut I,
    delay: &mut impl DelayNs,
) -> Result<ScdSample, SensorError> {
    i2c.write(SCD_ADDR, &0xE4B8u16.to_be_bytes())
        .map_err(|error| SensorError::Bus(error.kind()))?;
    delay.delay_ms(2);
    let mut state = [0u8; 3];
    i2c.read(SCD_ADDR, &mut state)
        .map_err(|error| SensorError::Bus(error.kind()))?;
    if sensirion_crc(&state[..2]) != state[2] {
        return Err(SensorError::Crc);
    }
    if (u16::from_be_bytes([state[0], state[1]]) & 0x07FF) == 0 {
        return Ok(ScdSample::Waiting);
    }
    i2c.write(SCD_ADDR, &0xEC05u16.to_be_bytes())
        .map_err(|error| SensorError::Bus(error.kind()))?;
    delay.delay_ms(2);
    let mut data = [0u8; 9];
    i2c.read(SCD_ADDR, &mut data)
        .map_err(|error| SensorError::Bus(error.kind()))?;
    for index in [0usize, 3, 6] {
        if sensirion_crc(&data[index..index + 2]) != data[index + 2] {
            return Err(SensorError::Crc);
        }
    }
    let co2 = u16::from_be_bytes([data[0], data[1]]);
    let temp = -45.0 + 175.0 * u16::from_be_bytes([data[3], data[4]]) as f32 / 65_535.0;
    let rh = 100.0 * u16::from_be_bytes([data[6], data[7]]) as f32 / 65_535.0;
    Ok(ScdSample::Values(co2, temp, rh))
}

pub fn sht30_read<I: embedded_hal::i2c::I2c>(
    i2c: &mut I,
    delay: &mut impl DelayNs,
) -> Result<(f32, f32), SensorError> {
    i2c.write(SHT30_ADDR, &0x2400u16.to_be_bytes())
        .map_err(|error| SensorError::Bus(error.kind()))?;
    delay.delay_ms(20);
    let mut data = [0u8; 6];
    i2c.read(SHT30_ADDR, &mut data)
        .map_err(|error| SensorError::Bus(error.kind()))?;
    if sensirion_crc(&data[..2]) != data[2] || sensirion_crc(&data[3..5]) != data[5] {
        return Err(SensorError::Crc);
    }
    let temp = -45.0 + 175.0 * u16::from_be_bytes([data[0], data[1]]) as f32 / 65_535.0;
    let rh = 100.0 * u16::from_be_bytes([data[3], data[4]]) as f32 / 65_535.0;
    Ok((temp, rh))
}

fn dallas_crc(data: &[u8]) -> u8 {
    let mut crc = 0u8;
    for byte in data {
        let mut value = *byte;
        for _ in 0..8 {
            let mix = (crc ^ value) & 1;
            crc >>= 1;
            if mix != 0 {
                crc ^= 0x8C;
            }
            value >>= 1;
        }
    }
    crc
}

/// Validate a DS18B20 scratchpad before exposing a temperature to the HMI.
pub fn decode_ds18b20(data: &[u8; 9]) -> Result<f32, SensorError> {
    if data.iter().all(|&b| b == 0) || data[4] & 0x9F != 0x1F {
        return Err(SensorError::InvalidData);
    }
    if dallas_crc(&data[..8]) != data[8] {
        return Err(SensorError::Crc);
    }
    let mut raw = i16::from_le_bytes([data[0], data[1]]);
    // Low temperature bits are undefined below 12-bit resolution.
    raw &= match data[4] & 0x60 {
        0x00 => !7,
        0x20 => !3,
        0x40 => !1,
        _ => !0,
    };
    let value = raw as f32 / 16.0;
    if (-55.0..=125.0).contains(&value) {
        Ok(value)
    } else {
        Err(SensorError::InvalidData)
    }
}
