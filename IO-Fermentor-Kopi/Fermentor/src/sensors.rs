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
/// Sensors are read in turn, one per slot, so no two I2C jobs ever overlap.
/// Six slots (DS18B20, pH, pressure, O2, SCD41, SHT30) give a ~5 s cycle.
pub const SENSOR_SLOT_MS: u64 = 833;
pub const SENSOR_SLOT_COUNT: u8 = 6;
/// An offline I2C device is re-initialized at most once per this interval.
pub const SENSOR_RETRY_MS: u64 = 5_000;
/// Keep bus failures distinct from invalid samples for HMI and serial diagnostics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SensorError {
    Bus(ErrorKind),
    Crc,
    InvalidData,
    Timeout,
    InvalidChannel,
    /// Device is marked offline and its next re-init is not due yet.
    Offline,
}

impl SensorError {
    pub fn reading(self) -> Reading {
        if matches!(self, Self::Bus(ErrorKind::NoAcknowledge(_)) | Self::Offline) {
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

/// Online/re-init bookkeeping for one I2C device, like the C++ adsOk/o2Ok/scdOk
/// flags. Any failed read marks the device offline; it is probed again on its
/// next slot once SENSOR_RETRY_MS has passed since the previous probe.
pub struct RetryGate {
    online: bool,
    last_probe_ms: Option<u64>,
}

impl Default for RetryGate {
    fn default() -> Self {
        Self::new()
    }
}

impl RetryGate {
    /// Not initialized yet: the first slot probes immediately.
    pub const fn new() -> Self {
        Self {
            online: false,
            last_probe_ms: None,
        }
    }

    /// Already probed during boot.
    pub const fn started(online: bool, now_ms: u64) -> Self {
        Self {
            online,
            last_probe_ms: Some(now_ms),
        }
    }

    pub fn run<I, D, T>(
        &mut self,
        i2c: &mut I,
        delay: &mut D,
        now_ms: u64,
        probe: impl FnOnce(&mut I, &mut D) -> Result<(), SensorError>,
        read: impl FnOnce(&mut I, &mut D) -> Result<T, SensorError>,
    ) -> Result<T, SensorError> {
        if !self.online {
            if self
                .last_probe_ms
                .is_some_and(|last| now_ms.saturating_sub(last) < SENSOR_RETRY_MS)
            {
                return Err(SensorError::Offline);
            }
            self.last_probe_ms = Some(now_ms);
            probe(i2c, delay)?;
            self.online = true;
        }
        let result = read(i2c, delay);
        self.online = result.is_ok();
        result
    }
}

/// ADS1115 init check: the config register must be readable.
pub fn ads_probe<I: embedded_hal::i2c::I2c>(i2c: &mut I) -> Result<(), SensorError> {
    let mut config = [0u8; 2];
    i2c.write_read(ADS_ADDR, &[0x01], &mut config)
        .map_err(|error| SensorError::Bus(error.kind()))
}

/// SEN0322 init check: the device must ACK its key register address.
pub fn o2_probe<I: embedded_hal::i2c::I2c>(i2c: &mut I) -> Result<(), SensorError> {
    i2c.write(O2_ADDR, &[0x0A])
        .map_err(|error| SensorError::Bus(error.kind()))
}

const ADS_SAMPLES: i32 = 8;
const ADS_READY_POLLS: u32 = 80;
const ADS_READY_POLL_US: u32 = 250;

// ADS1115: A0 pH at +/-4.096 V; A1 pressure at +/-6.144 V.
// 860 SPS single-shot conversions (~1.2 ms each); average 8 samples like the
// C++ firmware. Each conversion's ready bit is polled for up to ~20 ms.
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
    let config: u16 = 0x8000 | mux | gain | 0x0100 | 0x00E0 | 0x0003;
    let mut total = 0i32;
    for _ in 0..ADS_SAMPLES {
        i2c.write(ADS_ADDR, &[0x01, (config >> 8) as u8, config as u8])
            .map_err(|error| SensorError::Bus(error.kind()))?;
        let mut state = [0u8; 2];
        let mut ready = false;
        for _ in 0..ADS_READY_POLLS {
            delay.delay_us(ADS_READY_POLL_US);
            i2c.write_read(ADS_ADDR, &[0x01], &mut state)
                .map_err(|error| SensorError::Bus(error.kind()))?;
            if state[0] & 0x80 != 0 {
                ready = true;
                break;
            }
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
    Ok((total / ADS_SAMPLES) as f32 * full_scale / 32_768.0)
}

/// Two-buffer calibration measured with ../PH-Calibration on 2026-10-25
/// (filtered ADS1115 A0 millivolts). This module's voltage FALLS toward acid.
/// Replaces the DFRobot example points (1500.00 / 2032.44 mV) and the
/// temporary -1.10 image-based offset; no offset is applied on top.
pub const PH_NEUTRAL_PH: f32 = 6.85;
pub const PH_NEUTRAL_MV: f32 = 1742.47;
pub const PH_ACID_PH: f32 = 4.00;
pub const PH_ACID_MV: f32 = 1667.72;

pub fn ph_from_voltage(voltage: f32) -> Option<f32> {
    let ph = PH_NEUTRAL_PH
        + (PH_ACID_PH - PH_NEUTRAL_PH) * (voltage * 1_000.0 - PH_NEUTRAL_MV)
            / (PH_ACID_MV - PH_NEUTRAL_MV);
    // Out-of-range values report ERROR; never clamp to conceal invalid data.
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
