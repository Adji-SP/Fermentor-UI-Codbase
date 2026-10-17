//! Hardcoded automatic mixer timing; no hardware, sleeping or RPM feedback.

pub const MIXER_TARGET_RPM: u8 = 3; // Desired shaft speed, not a measured value.
pub const MIXER_PWM_DUTY: u8 = 64; // Existing example setting; calibrate on the mixer.
pub const FIRST_START_DELAY_MS: u64 = 30_000;
pub const RUN_DURATION_MS: u64 = 120_000;
pub const START_INTERVAL_MS: u64 = 6 * 60 * 60 * 1_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    None,
    Start,
    Stop,
}

pub struct MixerSchedule {
    armed: bool,
    next_start_ms: u64,
    run_until_ms: Option<u64>,
    changed: bool,
}

impl MixerSchedule {
    pub const fn new(now_ms: u64) -> Self {
        Self {
            armed: true,
            next_start_ms: now_ms.saturating_add(FIRST_START_DELAY_MS),
            run_until_ms: None,
            changed: true,
        }
    }

    /// A STOP/fault cancels this run and all future runs until explicit resume.
    pub fn pause(&mut self) {
        if self.armed || self.run_until_ms.is_some() {
            self.armed = false;
            self.run_until_ms = None;
            self.changed = true;
        }
    }

    /// Repeated resume commands cannot extend a run or shift an armed schedule.
    pub fn resume(&mut self, now_ms: u64) {
        if !self.armed {
            self.armed = true;
            self.next_start_ms = now_ms.saturating_add(FIRST_START_DELAY_MS);
            self.changed = true;
        }
    }

    pub fn is_armed(&self) -> bool {
        self.armed
    }

    pub fn next_start_in_ms(&self, now: u64) -> u64 {
        if self.armed {
            self.next_start_ms.saturating_sub(now)
        } else {
            0
        }
    }

    pub fn run_remaining_ms(&self, now: u64) -> u64 {
        self.run_until_ms
            .map_or(0, |deadline| deadline.saturating_sub(now))
    }

    pub fn tick(&mut self, now_ms: u64, hmi_connected: bool) -> Action {
        if !self.armed {
            return Action::None;
        }
        if let Some(deadline) = self.run_until_ms {
            if !hmi_connected {
                self.pause();
                return Action::Stop;
            }
            if now_ms >= deadline {
                self.run_until_ms = None;
                self.changed = true;
                return Action::Stop;
            }
            return Action::None;
        }
        if now_ms < self.next_start_ms {
            return Action::None;
        }
        if !hmi_connected {
            self.pause();
            return Action::Stop;
        }

        // Stay on the six-hour start-to-start cadence. Never replay missed runs
        // or extend a run beyond its scheduled two-minute window after a stall.
        let elapsed = now_ms.saturating_sub(self.next_start_ms);
        self.next_start_ms = self
            .next_start_ms
            .saturating_add((elapsed / START_INTERVAL_MS).saturating_mul(START_INTERVAL_MS));
        let run_deadline = self.next_start_ms.saturating_add(RUN_DURATION_MS);
        self.next_start_ms = self.next_start_ms.saturating_add(START_INTERVAL_MS);
        if now_ms >= run_deadline {
            return Action::None;
        }
        self.run_until_ms = Some(run_deadline);
        self.changed = true;
        Action::Start
    }

    pub fn status_text(&self) -> &'static str {
        if !self.armed {
            "AUTO PAUSED"
        } else if self.run_until_ms.is_some() {
            "AUTO FWD"
        } else {
            "AUTO WAIT"
        }
    }

    pub fn take_changed(&mut self) -> bool {
        core::mem::take(&mut self.changed)
    }
}
