//! Fermentor Kopi production firmware for ESP32-S3.
//! Rust port of the existing IO-Fermentor-Kopi/Fermentor PlatformIO behavior.
//! Test-Fermentor-Kopi is used for Rust build/flash configuration only.

#![no_std]
#![no_main]

mod ds18b20;
mod motor;
mod nextion;

use fermentor_kopi::sensors;

esp_bootloader_esp_idf::esp_app_desc!();

use esp_hal::{
    clock::CpuClock,
    gpio::{DriveMode, Level, Output, OutputConfig},
    i2c::master::{Config as I2cConfig, I2c, SoftwareTimeout},
    ledc::{
        channel::{self, ChannelIFace},
        timer::{self, TimerIFace},
        LSGlobalClkSource, Ledc, LowSpeed,
    },
    main,
    time::{Duration, Instant, Rate},
    uart::{Config as UartConfig, Uart},
};
use esp_println::println;

use ds18b20::Ds18b20;
use motor::Motor;
use nextion::Nextion;
use sensors::{O2Average, O2Warmup, Reading, Readings, ScdSample, SensorError, Status};

const SAMPLE_INTERVAL_MS: u64 = 5_000;
const FIRST_SAMPLE_DELAY_MS: u64 = 400; // DS18B20 11-bit conversion, not O2 warm-up.

/// Sensor conversion waits continue servicing HMI RX and motor safety timers.
/// Display TX is scheduled by the main loop, not nested inside a sensor wait.
struct HmiDelay<'a, 'h, 'm> {
    hmi: &'a mut Nextion<'h>,
    motor: &'a mut Motor<'m>,
}

impl embedded_hal::delay::DelayNs for HmiDelay<'_, '_, '_> {
    fn delay_ns(&mut self, ns: u32) {
        let started = Instant::now();
        let duration = Duration::from_micros(u64::from(ns.div_ceil(1_000)));
        loop {
            self.hmi.service(self.motor);
            if Instant::now() - started >= duration {
                break;
            }
            esp_hal::delay::Delay::new().delay_micros(100);
        }
    }
}

fn sensor_error(name: &str, error: SensorError) -> Reading {
    println!("[{name}] {error:?}");
    error.reading()
}

fn print_reading(name: &str, reading: Reading, unit: &str) {
    match reading.status {
        Status::Ok => println!("{name:<10}: {:.2} {unit}", reading.value),
        Status::Wait => println!("{name:<10}: WAIT"),
        Status::Warmup(seconds) => println!(
            "{name:<10}: WAIT (WARMUP {:02}:{:02})",
            seconds / 60,
            seconds % 60
        ),
        Status::Offline => println!("{name:<10}: OFFLINE"),
        Status::Error => println!("{name:<10}: ERROR"),
    }
}

#[main]
fn main() -> ! {
    // Bit-banged DS18B20 slots use 240 cycles/us; request that clock explicitly.
    let peripherals = esp_hal::init(esp_hal::Config::default().with_cpu_clock(CpuClock::max()));
    let mut o2_warmup = O2Warmup::new(Instant::now().duration_since_epoch().as_millis());

    // Disable the bridge first, before any sensor or HMI initialization.
    let l_en = Output::new(peripherals.GPIO5, Level::Low, OutputConfig::default());
    let r_en = Output::new(peripherals.GPIO4, Level::Low, OutputConfig::default());
    let mut ledc = Ledc::new(peripherals.LEDC);
    ledc.set_global_slow_clock(LSGlobalClkSource::APBClk);
    let mut motor_timer = ledc.timer::<LowSpeed>(timer::Number::Timer0);
    motor_timer
        .configure(timer::config::Config {
            duty: timer::config::Duty::Duty8Bit,
            clock_source: timer::LSClockSource::APBClk,
            frequency: Rate::from_khz(1),
        })
        .unwrap();
    let mut lpwm = ledc.channel(channel::Number::Channel0, peripherals.GPIO7);
    let mut rpwm = ledc.channel(channel::Number::Channel1, peripherals.GPIO6);
    lpwm.configure(channel::config::Config {
        timer: &motor_timer,
        duty_pct: 0,
        drive_mode: DriveMode::PushPull,
    })
    .unwrap();
    rpwm.configure(channel::config::Config {
        timer: &motor_timer,
        duty_pct: 0,
        drive_mode: DriveMode::PushPull,
    })
    .unwrap();
    let mut motor = Motor::new(lpwm, rpwm, l_en, r_en);

    let uart = Uart::new(
        peripherals.UART1,
        UartConfig::default().with_baudrate(9_600),
    )
    .unwrap()
    .with_tx(peripherals.GPIO15)
    .with_rx(peripherals.GPIO16);
    let mut hmi = Nextion::new(uart);

    let i2c_config = I2cConfig::default()
        .with_frequency(Rate::from_khz(100))
        .with_software_timeout(SoftwareTimeout::Transaction(Duration::from_millis(100)));
    let mut i2c = I2c::new(peripherals.I2C0, i2c_config)
        .unwrap()
        .with_sda(peripherals.GPIO8)
        .with_scl(peripherals.GPIO9);
    let mut ds18b20 = Ds18b20::new(peripherals.GPIO12);
    let mut o2_average = O2Average::new();
    let mut scd_online = {
        let mut delay = HmiDelay {
            hmi: &mut hmi,
            motor: &mut motor,
        };
        // SCD41 power-up to idle; HMI RX and motor timers remain serviced.
        // This also exceeds the SHT30's short power-up requirement.
        embedded_hal::delay::DelayNs::delay_ms(&mut delay, sensors::SCD_POWER_UP_MS);
        sensors::scd_start(&mut i2c, &mut delay).is_ok()
    };
    let _ = ds18b20.request();

    println!("=== Fermentor Kopi Rust / ESP32-S3 ===");
    println!("DS18B20 DQ=GPIO12; I2C SDA=GPIO8 SCL=GPIO9 100kHz");
    println!("ADS1115=0x48, O2=0x70, SCD41=0x62, SHT30=0x44");
    println!("Nextion UART1 TX=GPIO15 RX=GPIO16 9600 baud");
    println!("Motor LPWM=GPIO7 RPWM=GPIO6 L_EN=GPIO5 R_EN=GPIO4; OFF at boot");
    println!(
        "SCD41 periodic start: {}",
        if scd_online { "OK" } else { "OFFLINE" }
    );
    println!("[O2] Startup warm-up: 3 minutes; valid readings are shown immediately and may still be settling.");

    let mut readings = Readings::waiting();
    readings.oxygen = Reading::warmup(
        o2_warmup.remaining_seconds(Instant::now().duration_since_epoch().as_millis()),
    );
    let mut last_sample = Instant::now();
    let mut first_sample = true;
    // Other sensors can publish before SCD41's first 5-second data-ready event.
    hmi.set_readings(readings);

    loop {
        if let Status::Warmup(previous_seconds) = readings.oxygen.status {
            let seconds =
                o2_warmup.remaining_seconds(Instant::now().duration_since_epoch().as_millis());
            if seconds != previous_seconds {
                readings.oxygen = if seconds == 0 {
                    Reading::error()
                } else {
                    Reading::warmup(seconds)
                };
                hmi.set_readings(readings);
            }
        }
        hmi.tick(&mut motor);
        let now = Instant::now();
        let sample_interval = if first_sample {
            FIRST_SAMPLE_DELAY_MS
        } else {
            SAMPLE_INTERVAL_MS
        };
        if (now - last_sample).as_millis() >= sample_interval {
            first_sample = false;
            last_sample = now;
            println!("\n================ SENSOR READINGS ================");

            let mut delay = HmiDelay {
                hmi: &mut hmi,
                motor: &mut motor,
            };
            readings.process_temp = ds18b20.sample();
            print_reading("DS18B20", readings.process_temp, "C");

            readings.ph = match sensors::read_ads_voltage(&mut i2c, 0, &mut delay) {
                Ok(voltage) => {
                    println!("pH input   : {:.1} mV", voltage * 1000.0);
                    sensors::ph_from_voltage(voltage)
                        .map(Reading::ok)
                        .unwrap_or_else(Reading::error)
                }
                Err(error) => sensor_error("pH/ADS", error),
            };
            print_reading("pH/ADS", readings.ph, "pH");
            readings.pressure = match sensors::read_ads_voltage(&mut i2c, 1, &mut delay) {
                Ok(voltage) => {
                    println!("Pressure in: {voltage:.3} V");
                    sensors::pressure_from_voltage(voltage)
                        .map(Reading::ok)
                        .unwrap_or_else(Reading::error)
                }
                Err(error) => sensor_error("Pressure/ADS", error),
            };
            print_reading("Pressure", readings.pressure, "bar");

            let oxygen_result = o2_average.read(&mut i2c, &mut delay);
            if let Err(error) = oxygen_result {
                println!("[O2] {error:?}");
            }
            readings.oxygen = o2_warmup.reading(
                oxygen_result,
                Instant::now().duration_since_epoch().as_millis(),
            );
            print_reading("O2", readings.oxygen, "%Vol");

            if !scd_online {
                scd_online = sensors::scd_start(&mut i2c, &mut delay).is_ok();
            }
            readings.co2 = if scd_online {
                Reading::wait()
            } else {
                Reading::offline()
            };
            readings.scd_temp = readings.co2;
            readings.scd_rh = readings.co2;
            if scd_online {
                match sensors::scd_read(&mut i2c, &mut delay) {
                    Ok(ScdSample::Values(co2, temp, rh)) => {
                        readings.co2 = Reading::ok(co2 as f32);
                        readings.scd_temp = Reading::ok(temp);
                        readings.scd_rh = Reading::ok(rh);
                    }
                    Ok(ScdSample::Waiting) => {}
                    Err(error) => {
                        readings.co2 = sensor_error("SCD41", error);
                        readings.scd_temp = readings.co2;
                        readings.scd_rh = readings.co2;
                        scd_online = false;
                    }
                }
            }
            print_reading("CO2", readings.co2, "ppm");
            print_reading("SCD41 T", readings.scd_temp, "C");
            print_reading("SCD41 RH", readings.scd_rh, "%RH");

            match sensors::sht30_read(&mut i2c, &mut delay) {
                Ok((temp, rh)) => {
                    readings.sht_temp = Reading::ok(temp);
                    readings.sht_rh = Reading::ok(rh);
                }
                Err(error) => {
                    readings.sht_temp = sensor_error("SHT30", error);
                    readings.sht_rh = readings.sht_temp;
                }
            }
            print_reading("SHT30 T", readings.sht_temp, "C");
            print_reading("SHT30 RH", readings.sht_rh, "%RH");

            hmi.set_readings(readings);
            hmi.tick(&mut motor);
        }
        esp_hal::delay::Delay::new().delay_millis(1);
    }
}

#[panic_handler]
fn panic_handler(info: &core::panic::PanicInfo) -> ! {
    println!("[PANIC] {info}");
    // A reset stops LEDC and returns all GPIOs to reset state. External
    // pull-downs on both motor enables are required for hardware fail-safe.
    esp_hal::system::software_reset()
}
