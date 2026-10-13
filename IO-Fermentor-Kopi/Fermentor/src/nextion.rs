use core::fmt::{self, Write as _};
use esp_hal::{time::Instant, uart::Uart, Blocking};
use fermentor_kopi::{
    nextion_wire::{Event, WireDecoder},
    protocol::Command,
};

use crate::{
    motor::{Direction, Motor},
    sensors::{Reading, Readings, Status},
};

const NORMAL_TEXT: u16 = 63455;
const MOTOR_ON: u16 = 20151;
const MOTOR_OFF: u16 = 36118;
const PAGE_PROBE_MS: u64 = 1_000;
const LINK_TIMEOUT_MS: u64 = 3_000;
const SLIDER_REPLY_MS: u64 = 500;

fn status_color(status: Status) -> u16 {
    match status {
        Status::Ok => NORMAL_TEXT,
        Status::Wait | Status::Warmup(_) => 62922,
        Status::Offline => 64715,
        Status::Error => 60238,
    }
}

fn status_text(status: Status) -> &'static str {
    match status {
        Status::Wait => "WAIT",
        Status::Warmup(_) => "WARMUP",
        Status::Offline => "OFFLINE",
        Status::Error | Status::Ok => "ERROR",
    }
}

#[derive(Clone, Copy)]
enum Unit {
    Temp,
    Percent,
    Ph,
    Co2,
    Pressure,
}

pub struct Nextion<'d> {
    uart: Uart<'d, Blocking>,
    decoder: WireDecoder,
    publish_index: u8,
    active_page: Option<u8>,
    last_probe: Option<Instant>,
    last_page_reply: Instant,
    slider_request: Option<Instant>,
    slider_touched: bool,
    refresh: bool,
    slider_sync: bool,
    readings: Readings,
}

impl<'d> Nextion<'d> {
    pub fn new(uart: Uart<'d, Blocking>) -> Self {
        Self {
            uart,
            decoder: WireDecoder::new(),
            publish_index: 0,
            active_page: None,
            last_probe: None,
            last_page_reply: Instant::now(),
            slider_request: None,
            slider_touched: false,
            refresh: true,
            slider_sync: true,
            readings: Readings::waiting(),
        }
    }

    fn end_command(&mut self) {
        let mut terminator: &[u8] = &[0xff, 0xff, 0xff];
        while !terminator.is_empty() {
            match self.uart.write(terminator) {
                Ok(0) | Err(_) => break,
                Ok(written) => terminator = &terminator[written..],
            }
        }
    }

    fn send_literal(&mut self, command: &str) {
        let _ = self.uart.write_str(command);
        self.end_command();
    }

    fn set_text(&mut self, name: &str, text: fmt::Arguments<'_>, color: u16) {
        let _ = write!(self.uart, "{name}.txt=\"");
        let _ = self.uart.write_fmt(text);
        let _ = self.uart.write_str("\"");
        self.end_command();
        let _ = write!(self.uart, "{name}.pco={color}");
        self.end_command();
    }

    fn show_reading(&mut self, label: &str, reading: Reading, unit: Unit) {
        if reading.status == Status::Ok && reading.value.is_finite() {
            let value = reading.value;
            match unit {
                Unit::Temp => self.set_text(label, format_args!("{value:.2} C"), NORMAL_TEXT),
                Unit::Percent => self.set_text(label, format_args!("{value:.2} %"), NORMAL_TEXT),
                Unit::Ph => self.set_text(label, format_args!("{value:.2}"), NORMAL_TEXT),
                Unit::Co2 => self.set_text(label, format_args!("{value:.0} ppm"), NORMAL_TEXT),
                Unit::Pressure => self.set_text(label, format_args!("{value:.3} bar"), NORMAL_TEXT),
            }
        } else if let Status::Warmup(seconds) = reading.status {
            self.set_text(
                label,
                format_args!("WARMUP {:02}:{:02}", seconds / 60, seconds % 60),
                status_color(reading.status),
            );
        } else {
            self.set_text(
                label,
                format_args!("{}", status_text(reading.status)),
                status_color(reading.status),
            );
        }
    }

    // One widget per tick allows RX/STOP processing between text updates.
    fn publish_step(&mut self, motor: &Motor<'_>) {
        let r = self.readings;
        match self.publish_index {
            0 => self.show_reading("t0", r.process_temp, Unit::Temp),
            1 => self.show_reading("t1", r.ph, Unit::Ph),
            2 => self.show_reading("t2", r.scd_temp, Unit::Temp),
            3 => self.show_reading("t3", r.sht_temp, Unit::Temp),
            4 => self.show_reading("t4", r.pressure, Unit::Pressure),
            5 => self.show_reading("t5", r.oxygen, Unit::Percent),
            6 => self.show_reading("t6", r.co2, Unit::Co2),
            7 => self.show_reading("t7", r.scd_rh, Unit::Percent),
            8 => self.show_reading("t8", r.sht_rh, Unit::Percent),
            9 => {
                let state = if motor.pending() != Direction::Stop {
                    "SWITCHING"
                } else {
                    match motor.direction() {
                        Direction::Forward => "ON FWD",
                        Direction::Reverse => "ON REV",
                        Direction::Stop => "OFF",
                    }
                };
                self.set_text(
                    "t9",
                    format_args!("{state}"),
                    if motor.direction() == Direction::Stop {
                        MOTOR_OFF
                    } else {
                        MOTOR_ON
                    },
                );
            }
            10 => {
                let duty = motor.duty();
                self.set_text(
                    "t10",
                    format_args!("{duty}/255 ({}%)", duty as u16 * 100 / 255),
                    NORMAL_TEXT,
                );
            }
            11 if self.slider_sync && !self.slider_touched && self.slider_request.is_none() => {
                let _ = write!(self.uart, "h0.val={}", motor.duty());
                self.end_command();
                self.slider_sync = false;
            }
            _ => {}
        }
        self.publish_index += 1;
        if self.publish_index >= 12 {
            self.refresh = false;
        }
    }

    fn request_refresh(&mut self) {
        self.publish_index = 0;
        self.refresh = true;
    }

    fn page_received(&mut self, page: u8, motor: &mut Motor<'_>) {
        if page == 0 {
            if self.active_page != Some(0) {
                self.slider_sync = true;
                self.request_refresh();
            }
            self.last_page_reply = Instant::now();
            motor.keepalive();
        } else {
            if motor.direction() != Direction::Stop || motor.pending() != Direction::Stop {
                motor.request_direction(Direction::Stop);
            }
            self.slider_request = None;
            self.slider_touched = false;
        }
        self.active_page = Some(page);
    }

    fn handle_event(&mut self, event: Event, motor: &mut Motor<'_>) {
        match event {
            Event::Ready => {
                motor.request_direction(Direction::Stop);
                self.active_page = None;
                self.last_probe = None;
                self.slider_request = None;
                self.slider_touched = false;
                self.slider_sync = true;
                self.request_refresh();
            }
            Event::Page(page) => self.page_received(page, motor),
            Event::Ascii(Command::Page(page)) => {
                self.page_received(page, motor);
                if page == 0 {
                    self.slider_sync = true;
                    self.request_refresh();
                }
            }
            Event::Ascii(Command::Stop) => motor.request_direction(Direction::Stop),
            Event::Ascii(command) if self.active_page == Some(0) => match command {
                Command::Keepalive => motor.keepalive(),
                Command::Forward => motor.request_direction(Direction::Forward),
                Command::Reverse => motor.request_direction(Direction::Reverse),
                Command::Speed(speed) => {
                    self.slider_request = None;
                    motor.set_speed(speed);
                }
                _ => {}
            },
            Event::Touch {
                page: 0,
                component,
                pressed,
            } if self.active_page == Some(0) => {
                if component == 16 {
                    self.slider_touched = pressed;
                    self.slider_sync = false;
                    if !pressed && self.slider_request.is_none() {
                        self.slider_request = Some(Instant::now());
                        self.send_literal("get h0.val");
                    }
                } else if !pressed {
                    match component {
                        13 => motor.request_direction(Direction::Forward),
                        14 => motor.request_direction(Direction::Reverse),
                        15 => motor.request_direction(Direction::Stop),
                        _ => {}
                    }
                }
            }
            Event::Number(value) => {
                if let Some(started) = self.slider_request.take() {
                    if self.active_page == Some(0)
                        && (Instant::now() - started).as_millis() <= SLIDER_REPLY_MS
                        && value <= 255
                    {
                        motor.set_speed(value as u8);
                    } else {
                        self.slider_sync = true;
                        self.request_refresh();
                    }
                }
            }
            _ => {}
        }
    }

    fn poll(&mut self, motor: &mut Motor<'_>) {
        for _ in 0..128 {
            if !self.uart.read_ready() {
                break;
            }
            let mut byte = [0u8; 1];
            if self.uart.read(&mut byte).ok() != Some(1) {
                self.decoder.discard();
                break;
            }
            if let Some(event) = self.decoder.push(byte[0]) {
                self.handle_event(event, motor);
            }
        }
    }

    pub fn set_readings(&mut self, readings: Readings) {
        self.readings = readings;
        self.request_refresh();
    }

    /// Service RX, link queries and motor timers during sensor conversion waits.
    /// The larger display refresh is only transmitted by the outer loop.
    pub fn service(&mut self, motor: &mut Motor<'_>) {
        self.poll(motor);
        let now = Instant::now();
        if self.active_page == Some(0) && (now - self.last_page_reply).as_millis() > LINK_TIMEOUT_MS
        {
            self.active_page = None;
            self.slider_request = None;
            self.slider_touched = false;
            motor.request_direction(Direction::Stop);
        }
        if self
            .slider_request
            .is_some_and(|started| (now - started).as_millis() > SLIDER_REPLY_MS)
        {
            self.slider_request = None;
            self.slider_sync = true;
            self.request_refresh();
        }
        if self
            .last_probe
            .is_none_or(|last| (now - last).as_millis() >= PAGE_PROBE_MS)
        {
            self.send_literal("sendme");
            self.last_probe = Some(now);
        }
        motor.tick();
        if motor.take_changed() {
            self.request_refresh();
        }
    }

    pub fn tick(&mut self, motor: &mut Motor<'_>) {
        self.service(motor);
        if self.refresh && self.active_page == Some(0) {
            self.publish_step(motor);
        }
    }
}
