use embedded_hal::pwm::SetDutyCycle;
use esp_hal::{
    gpio::Output,
    ledc::{channel::Channel, LowSpeed},
    time::Instant,
};
use fermentor_kopi::mixer_schedule::{Action, MixerSchedule, MIXER_PWM_DUTY};
pub use fermentor_kopi::motor_control::Direction;
use fermentor_kopi::motor_control::MotorControl;

pub struct Motor<'d> {
    lpwm: Channel<'d, LowSpeed>,
    rpwm: Channel<'d, LowSpeed>,
    l_en: Output<'d>,
    r_en: Output<'d>,
    control: MotorControl,
    schedule: MixerSchedule,
    applied: (Direction, u8),
    revision: u32,
}

fn now_ms() -> u64 {
    Instant::now().duration_since_epoch().as_millis()
}

impl<'d> Motor<'d> {
    pub fn new(
        mut lpwm: Channel<'d, LowSpeed>,
        mut rpwm: Channel<'d, LowSpeed>,
        mut l_en: Output<'d>,
        mut r_en: Output<'d>,
    ) -> Self {
        l_en.set_low();
        r_en.set_low();
        let _ = lpwm.set_duty_cycle(0);
        let _ = rpwm.set_duty_cycle(0);
        let now = now_ms();
        let mut control = MotorControl::new(now);
        control.set_speed(MIXER_PWM_DUTY, now);
        // There is no UART/Nextion keepalive in this build. Fail closed at boot,
        // rather than silently treating a missing safety interlock as ready.
        let mut schedule = MixerSchedule::new(now);
        schedule.pause();
        Self {
            lpwm,
            rpwm,
            l_en,
            r_en,
            control,
            schedule,
            applied: (Direction::Stop, 0),
            revision: 0,
        }
    }

    fn outputs_off(&mut self) {
        // Disable the bridge before touching PWM, including on a PWM error.
        self.l_en.set_low();
        self.r_en.set_low();
        let _ = self.lpwm.set_duty_cycle(0);
        let _ = self.rpwm.set_duty_cycle(0);
        self.applied = (Direction::Stop, 0);
    }

    fn apply(&mut self) {
        if !self.hmi_connected() {
            self.outputs_off();
            return;
        }
        let direction = self.control.direction;
        let duty = if direction == Direction::Stop {
            0
        } else {
            self.control.duty
        };
        if self.applied == (direction, duty) {
            return;
        }
        self.outputs_off();
        if direction == Direction::Stop || duty == 0 {
            return;
        }

        // Arduino's 8-bit analogWrite maps 255 to full-on, as does this port.
        let pwm = if duty == 255 { 256 } else { u16::from(duty) };
        let result = match direction {
            Direction::Forward => self.lpwm.set_duty_cycle(pwm),
            Direction::Reverse => self.rpwm.set_duty_cycle(pwm),
            Direction::Stop => unreachable!(),
        };
        if result.is_err() {
            self.control.stop();
            self.schedule.pause();
            self.outputs_off();
            self.revision = self.revision.wrapping_add(1);
            esp_println::println!("[MIXER] AUTO PAUSED: PWM output fault");
            return;
        }
        self.l_en.set_high();
        self.r_en.set_high();
        self.applied = (direction, duty);
    }

    pub fn request_direction(&mut self, direction: Direction) {
        match direction {
            Direction::Stop => {
                self.revision = self.revision.wrapping_add(1);
                self.schedule.pause();
                self.control.stop();
                self.apply();
                esp_println::println!("[MIXER] AUTO PAUSED: operator STOP");
            }
            Direction::Forward => {
                if !self.hmi_connected() {
                    esp_println::println!(
                        "[MIXER] RESUME rejected: Nextion disabled; interlock unavailable"
                    );
                    return;
                }
                if !self.schedule.is_armed() {
                    self.revision = self.revision.wrapping_add(1);
                    self.schedule.resume(now_ms());
                    esp_println::println!("[MIXER] AUTO RESUMED: first run in 30 seconds");
                }
            }
            Direction::Reverse => {
                esp_println::println!("[MIXER] Reverse ignored: automatic forward-only schedule");
            }
        }
        self.apply();
    }
    pub fn tick(&mut self) {
        let now = now_ms();
        let hmi_connected = self.hmi_connected();
        let timed_out = self.control.tick(now);
        if timed_out || !hmi_connected {
            self.schedule.pause();
            self.control.stop();
        }
        match self.schedule.tick(now, hmi_connected) {
            Action::Start => {
                self.revision = self.revision.wrapping_add(1);
                self.control.set_speed(MIXER_PWM_DUTY, now);
                self.control.request_direction(Direction::Forward, now);
                esp_println::println!(
                    "[MIXER] AUTO START: forward, PWM {}/255; next scheduled start in 6 hours",
                    MIXER_PWM_DUTY
                );
            }
            Action::Stop => {
                self.revision = self.revision.wrapping_add(1);
                self.control.stop();
                esp_println::println!("[MIXER] {}", self.schedule.status_text());
            }
            Action::None => {}
        }
        self.apply();
    }

    pub fn revision(&self) -> u32 {
        self.revision
    }
    pub fn is_armed(&self) -> bool {
        self.schedule.is_armed()
    }
    pub fn hmi_connected(&self) -> bool {
        // Keep the USB/dashboard contract truthful. Never synthesize a keepalive.
        false
    }
    /// Applied bridge command, not a measurement of shaft motion.
    pub fn output_duty(&self) -> u8 {
        self.applied.1
    }
    pub fn next_start_in_ms(&self) -> u64 {
        self.schedule.next_start_in_ms(now_ms())
    }
    pub fn run_remaining_ms(&self) -> u64 {
        self.schedule.run_remaining_ms(now_ms())
    }
    pub fn status_text(&self) -> &'static str {
        self.schedule.status_text()
    }
    pub fn duty(&self) -> u8 {
        self.control.duty
    }
}
