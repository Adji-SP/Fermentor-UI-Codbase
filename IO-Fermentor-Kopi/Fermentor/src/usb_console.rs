use crate::motor::{Direction, Motor};
use esp_hal::{time::Instant, usb_serial_jtag::UsbSerialJtagRx, Blocking};
use fermentor_kopi::{
    mixer_schedule::{FIRST_START_DELAY_MS, MIXER_TARGET_RPM, RUN_DURATION_MS, START_INTERVAL_MS},
    usb_control::{Action, Decoder},
};

pub struct UsbConsole<'d> {
    rx: UsbSerialJtagRx<'d, Blocking>,
    decoder: Decoder,
    boot_id: u32,
    last_report: u64,
    last_revision: Option<u32>,
}

impl<'d> UsbConsole<'d> {
    pub fn new(rx: UsbSerialJtagRx<'d, Blocking>) -> Self {
        Self {
            rx,
            decoder: Decoder::default(),
            // Reboot marker, not an authentication secret or cryptographic key.
            boot_id: esp_hal::rng::Rng::new().random(),
            last_report: 0,
            last_revision: None,
        }
    }

    pub fn service(&mut self, motor: &mut Motor<'_>) {
        let now = Instant::now().duration_since_epoch().as_millis();
        motor.tick();
        // Bound the work per pass so an incoming stream cannot starve sensors or timers.
        for _ in 0..64 {
            let Ok(byte) = self.rx.read_byte() else {
                break;
            };
            if let Some(command) = self.decoder.push(byte, now) {
                let result =
                    command.validate(self.boot_id, motor.revision(), now, motor.hmi_connected());
                if result.is_ok() {
                    motor.request_direction(match command.action {
                        Action::Stop => Direction::Stop,
                        Action::Resume => Direction::Forward,
                    });
                }
                esp_println::println!(
                    "[MIXER_ACK] {{\"id\":{},\"ok\":{},\"reason\":\"{}\"}}",
                    command.id,
                    result.is_ok(),
                    result.err().unwrap_or("APPLIED")
                );
                self.last_revision = None;
            }
        }
        if self.last_revision != Some(motor.revision())
            || now.saturating_sub(self.last_report) >= 1_000
        {
            self.last_report = now;
            self.last_revision = Some(motor.revision());
            esp_println::println!(
                "[MIXER_STATUS] {{\"version\":1,\"driver\":\"BTS7960\",\"boot_id\":{},\"uptime_ms\":{},\"revision\":{},\"state\":\"{}\",\"armed\":{},\"output_pwm\":{},\"configured_pwm\":{},\"target_rpm\":{},\"measured_rpm\":null,\"hmi_connected\":{},\"next_start_in_ms\":{},\"run_remaining_ms\":{},\"start_delay_ms\":{},\"run_duration_ms\":{},\"interval_ms\":{}}}",
                self.boot_id, now, motor.revision(), motor.status_text(), motor.is_armed(),
                motor.output_duty(), motor.duty(), MIXER_TARGET_RPM, motor.hmi_connected(),
                motor.next_start_in_ms(), motor.run_remaining_ms(), FIRST_START_DELAY_MS,
                RUN_DURATION_MS, START_INTERVAL_MS
            );
        }
    }
}
