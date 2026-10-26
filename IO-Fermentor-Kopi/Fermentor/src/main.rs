//! Fermentor Kopi production firmware for ESP32-S3.
//! Rust port of the existing IO-Fermentor-Kopi/Fermentor PlatformIO behavior.
//! Test-Fermentor-Kopi is used for Rust build/flash configuration only.

#![no_std]
#![no_main]

mod ds18b20;
mod motor;
mod usb_console;

use fermentor_kopi::mixer_schedule::{MIXER_PWM_DUTY, MIXER_TARGET_RPM};
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
    usb_serial_jtag::UsbSerialJtag,
};
use esp_println::println;

use ds18b20::Ds18b20;
use motor::Motor;
use sensors::{
    O2Average, O2Warmup, Reading, Readings, RetryGate, ScdSample, SensorError, Status,
};

fn uptime_ms() -> u64 {
    Instant::now().duration_since_epoch().as_millis()
}

/// Sensor conversion waits keep USB RX, status reporting and motor timers alive.
/// No Nextion UART is initialized or serviced in this build.
struct ServiceDelay<'a, 'm, 'u> {
    motor: &'a mut Motor<'m>,
    usb: &'a mut usb_console::UsbConsole<'u>,
}

impl embedded_hal::delay::DelayNs for ServiceDelay<'_, '_, '_> {
    fn delay_ns(&mut self, ns: u32) {
        let started = Instant::now();
        let duration = Duration::from_micros(u64::from(ns.div_ceil(1_000)));
        loop {
            self.usb.service(self.motor);
            if Instant::now() - started >= duration {
                break;
            }
            esp_hal::delay::Delay::new().delay_micros(100);
        }
    }
}

fn sensor_error(name: &str, error: SensorError) -> Reading {
    // Offline only means the re-init retry is not due; the cause was printed
    // when the device first failed.
    if error != SensorError::Offline {
        println!("[{name}] {error:?}");
    }
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
    let mut o2_warmup = O2Warmup::new(uptime_ms());

    // Disable the bridge first, before any sensor initialization.
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
    // Only use the HAL RX half. esp-println remains the sole USB TX owner;
    // no blocking HAL USB writes/flushes when the dashboard is disconnected.
    let (usb_rx, _usb_tx) = UsbSerialJtag::new(peripherals.USB_DEVICE).split();
    let mut usb = usb_console::UsbConsole::new(usb_rx);

    // UART1 and GPIO15/16 are intentionally left unconfigured.
    // Removing the HMI must not bypass its mixer safety interlock.

    let i2c_config = I2cConfig::default()
        .with_frequency(Rate::from_khz(100))
        .with_software_timeout(SoftwareTimeout::Transaction(Duration::from_millis(100)));
    let mut i2c = I2c::new(peripherals.I2C0, i2c_config)
        .unwrap()
        .with_sda(peripherals.GPIO8)
        .with_scl(peripherals.GPIO9);
    let mut ds18b20 = Ds18b20::new(peripherals.GPIO12);
    let mut o2_average = O2Average::new();
    let scd_online = {
        let mut delay = ServiceDelay {
            motor: &mut motor,
            usb: &mut usb,
        };
        // SCD41 power-up to idle; USB RX and motor timers remain serviced.
        // This also exceeds the SHT30's short power-up requirement.
        embedded_hal::delay::DelayNs::delay_ms(&mut delay, sensors::SCD_POWER_UP_MS);
        sensors::scd_start(&mut i2c, &mut delay).is_ok()
    };
    // ADS1115 and O2 are initialized in their first slot.
    let mut ads = RetryGate::new();
    let mut o2 = RetryGate::new();
    let mut scd = RetryGate::started(scd_online, uptime_ms());
    let _ = ds18b20.request();

    println!("=== Fermentor Kopi Rust / ESP32-S3 ===");
    println!("DS18B20 DQ=GPIO12; I2C SDA=GPIO8 SCL=GPIO9 100kHz");
    println!("ADS1115=0x48, O2=0x70, SCD41=0x62, SHT30=0x44");
    println!("Nextion UART DISABLED; GPIO15/16 unused; sensor output uses native USB");
    println!("Motor LPWM=GPIO7 RPWM=GPIO6 L_EN=GPIO5 R_EN=GPIO4; OFF at boot");
    println!("[MIXER] LOCKED OFF: Nextion removed; required safety interlock is unavailable");
    println!(
        "[MIXER] Target {} RPM; example PWM {}/255 is NOT calibrated RPM",
        MIXER_TARGET_RPM, MIXER_PWM_DUTY
    );
    println!("[MIXER] USB STOP/status available; RESUME rejected while Nextion is disabled");
    println!(
        "SCD41 periodic start: {}",
        if scd_online { "OK" } else { "OFFLINE" }
    );
    println!("[O2] Startup warm-up: 3 minutes; valid readings are shown immediately and may still be settling.");
    println!(
        "[pH calibration] pH {:.2} at {:.2} mV; pH {:.2} at {:.2} mV; no offset; does not fix drift",
        sensors::PH_NEUTRAL_PH,
        sensors::PH_NEUTRAL_MV,
        sensors::PH_ACID_PH,
        sensors::PH_ACID_MV
    );

    let mut readings = Readings::waiting();
    let mut last_slot = Instant::now();
    let mut slot = 0u8;
    // Other sensors can publish before SCD41's first 5-second data-ready event.

    loop {
        usb.service(&mut motor);
        let now = Instant::now();
        if (now - last_slot).as_millis() >= sensors::SENSOR_SLOT_MS {
            last_slot = now;
            let mut delay = ServiceDelay {
                motor: &mut motor,
                usb: &mut usb,
            };
            // One sensor per slot; the print order stays that of the original
            // SENSOR READINGS block, which the dashboard parser relies on.
            match slot {
                0 => {
                    println!("\n================ SENSOR READINGS ================");
                    readings.process_temp = ds18b20.sample();
                    print_reading("DS18B20", readings.process_temp, "C");
                }
                1 => {
                    readings.ph = match ads.run(
                        &mut i2c,
                        &mut delay,
                        uptime_ms(),
                        |i2c, _| sensors::ads_probe(i2c),
                        |i2c, delay| sensors::read_ads_voltage(i2c, 0, delay),
                    ) {
                        Ok(voltage) => {
                            println!("pH input   : {:.1} mV", voltage * 1000.0);
                            sensors::ph_from_voltage(voltage)
                                .map(Reading::ok)
                                .unwrap_or_else(Reading::error)
                        }
                        Err(error) => sensor_error("pH/ADS", error),
                    };
                    print_reading("pH/ADS", readings.ph, "pH");
                }
                2 => {
                    readings.pressure = match ads.run(
                        &mut i2c,
                        &mut delay,
                        uptime_ms(),
                        |i2c, _| sensors::ads_probe(i2c),
                        |i2c, delay| sensors::read_ads_voltage(i2c, 1, delay),
                    ) {
                        Ok(voltage) => {
                            println!("Pressure in: {voltage:.3} V");
                            sensors::pressure_from_voltage(voltage)
                                .map(Reading::ok)
                                .unwrap_or_else(Reading::error)
                        }
                        Err(error) => sensor_error("Pressure/ADS", error),
                    };
                    print_reading("Pressure", readings.pressure, "bar");
                }
                3 => {
                    let oxygen_result = o2.run(
                        &mut i2c,
                        &mut delay,
                        uptime_ms(),
                        |i2c, _| sensors::o2_probe(i2c),
                        |i2c, delay| o2_average.read(i2c, delay),
                    );
                    if let Err(error) = oxygen_result {
                        if error != SensorError::Offline {
                            println!("[O2] {error:?}");
                        }
                    }
                    readings.oxygen = o2_warmup.reading(oxygen_result, uptime_ms());
                    print_reading("O2", readings.oxygen, "%Vol");
                }
                4 => {
                    match scd.run(
                        &mut i2c,
                        &mut delay,
                        uptime_ms(),
                        |i2c, delay| sensors::scd_start(i2c, delay),
                        |i2c, delay| sensors::scd_read(i2c, delay),
                    ) {
                        Ok(ScdSample::Values(co2, temp, rh)) => {
                            readings.co2 = Reading::ok(co2 as f32);
                            readings.scd_temp = Reading::ok(temp);
                            readings.scd_rh = Reading::ok(rh);
                        }
                        // Data not ready yet: keep the last valid values.
                        Ok(ScdSample::Waiting) => {
                            if readings.co2.status != Status::Ok {
                                readings.co2 = Reading::wait();
                                readings.scd_temp = readings.co2;
                                readings.scd_rh = readings.co2;
                            }
                        }
                        Err(error) => {
                            readings.co2 = sensor_error("SCD41", error);
                            readings.scd_temp = readings.co2;
                            readings.scd_rh = readings.co2;
                        }
                    }
                    print_reading("CO2", readings.co2, "ppm");
                    print_reading("SCD41 T", readings.scd_temp, "C");
                    print_reading("SCD41 RH", readings.scd_rh, "%RH");
                }
                _ => {
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
                }
            }
            slot = (slot + 1) % sensors::SENSOR_SLOT_COUNT;
            usb.service(&mut motor);
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
