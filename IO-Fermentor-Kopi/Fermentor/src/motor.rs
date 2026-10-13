use embedded_hal::pwm::SetDutyCycle;
use esp_hal::{
    gpio::Output,
    ledc::{channel::Channel, LowSpeed},
    time::Instant,
};
pub use fermentor_kopi::motor_control::Direction;
use fermentor_kopi::motor_control::MotorControl;

pub struct Motor<'d> {
    lpwm: Channel<'d, LowSpeed>,
    rpwm: Channel<'d, LowSpeed>,
    l_en: Output<'d>,
    r_en: Output<'d>,
    control: MotorControl,
    applied: (Direction, u8),
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
        Self {
            lpwm,
            rpwm,
            l_en,
            r_en,
            control: MotorControl::new(now_ms()),
            applied: (Direction::Stop, 0),
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
            self.outputs_off();
            return;
        }
        self.l_en.set_high();
        self.r_en.set_high();
        self.applied = (direction, duty);
    }

    pub fn request_direction(&mut self, direction: Direction) {
        self.control.request_direction(direction, now_ms());
        self.apply();
    }
    pub fn set_speed(&mut self, speed: u8) {
        self.control.set_speed(speed, now_ms());
        self.apply();
    }
    pub fn keepalive(&mut self) {
        self.control.keepalive(now_ms());
    }

    pub fn tick(&mut self) {
        let timed_out = self.control.tick(now_ms());
        self.apply();
        if timed_out {
            esp_println::println!("[MOTOR] STOP: Nextion keepalive timeout");
        }
    }

    pub fn direction(&self) -> Direction {
        self.control.direction
    }
    pub fn pending(&self) -> Direction {
        self.control.pending
    }
    pub fn duty(&self) -> u8 {
        self.control.duty
    }
    pub fn take_changed(&mut self) -> bool {
        self.control.take_changed()
    }
}
