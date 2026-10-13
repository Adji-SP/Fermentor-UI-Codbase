//! Motor command/timing policy, independent of GPIO/PWM hardware.

pub const KEEPALIVE_TIMEOUT_MS: u64 = 3_000;
pub const REVERSE_PAUSE_MS: u64 = 250;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Direction {
    Stop,
    Forward,
    Reverse,
}

pub struct MotorControl {
    pub direction: Direction,
    pub pending: Direction,
    pub duty: u8,
    last_control: u64,
    reversal_started: u64,
    changed: bool,
}

impl MotorControl {
    pub const fn new(now: u64) -> Self {
        Self {
            direction: Direction::Stop,
            pending: Direction::Stop,
            duty: 64,
            last_control: now,
            reversal_started: now,
            changed: true,
        }
    }

    pub fn stop(&mut self) {
        self.direction = Direction::Stop;
        self.pending = Direction::Stop;
        self.changed = true;
    }

    pub fn request_direction(&mut self, requested: Direction, now: u64) {
        self.last_control = now;
        if requested == Direction::Stop || self.duty == 0 {
            self.stop();
        } else if self.pending != Direction::Stop {
            // Repeated commands must not bypass the bridge-off reversal pause.
            self.pending = requested;
        } else if self.direction != Direction::Stop && self.direction != requested {
            self.direction = Direction::Stop;
            self.pending = requested;
            self.reversal_started = now;
        } else {
            self.direction = requested;
        }
        self.changed = true;
    }

    pub fn set_speed(&mut self, speed: u8, now: u64) {
        self.last_control = now;
        self.duty = speed;
        if speed == 0 {
            self.stop();
        }
        // Raising speed while OFF never starts the motor automatically.
        self.changed = true;
    }

    pub fn keepalive(&mut self, now: u64) {
        self.last_control = now;
    }

    /// Returns true only when a running/pending motor is stopped by timeout.
    pub fn tick(&mut self, now: u64) -> bool {
        if (self.direction != Direction::Stop || self.pending != Direction::Stop)
            && now.saturating_sub(self.last_control) > KEEPALIVE_TIMEOUT_MS
        {
            self.stop();
            return true;
        }
        if self.pending != Direction::Stop
            && now.saturating_sub(self.reversal_started) >= REVERSE_PAUSE_MS
        {
            self.direction = self.pending;
            self.pending = Direction::Stop;
            self.changed = true;
        }
        false
    }

    pub fn take_changed(&mut self) -> bool {
        core::mem::take(&mut self.changed)
    }
}
