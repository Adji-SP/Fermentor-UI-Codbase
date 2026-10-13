//! Framing for native Nextion messages and the existing ASCII event protocol.
use crate::protocol::{parse_command, Command};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event {
    Touch {
        page: u8,
        component: u8,
        pressed: bool,
    },
    Page(u8),
    Number(u32),
    Ready,
    Ascii(Command),
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Mode {
    Idle,
    Ascii,
    Binary,
    DiscardAscii,
    DiscardBinary,
}

pub struct WireDecoder {
    mode: Mode,
    buffer: [u8; 48],
    length: usize,
    expected: usize,
    terminators: u8,
}

impl Default for WireDecoder {
    fn default() -> Self {
        Self::new()
    }
}

impl WireDecoder {
    pub const fn new() -> Self {
        Self {
            mode: Mode::Idle,
            buffer: [0; 48],
            length: 0,
            expected: 0,
            terminators: 0,
        }
    }

    fn reset(&mut self) {
        self.mode = Mode::Idle;
        self.length = 0;
        self.expected = 0;
        self.terminators = 0;
    }

    pub fn discard(&mut self) {
        self.mode = Mode::DiscardBinary;
        self.length = 0;
        self.terminators = 0;
    }

    pub fn push(&mut self, byte: u8) -> Option<Event> {
        if self.mode == Mode::Binary {
            self.buffer[self.length] = byte;
            self.length += 1;
            if self.length < self.expected {
                return None;
            }
            if self.buffer[self.expected - 3..self.expected] != [0xff; 3] {
                self.mode = Mode::DiscardBinary;
                self.terminators = self.buffer[..self.length]
                    .iter()
                    .rev()
                    .take_while(|&&b| b == 0xff)
                    .count() as u8;
                self.length = 0;
                return None;
            }
            let event = match self.buffer[0] {
                0x65 if self.buffer[3] <= 1 => Some(Event::Touch {
                    page: self.buffer[1],
                    component: self.buffer[2],
                    pressed: self.buffer[3] == 1,
                }),
                0x66 => Some(Event::Page(self.buffer[1])),
                0x71 => Some(Event::Number(u32::from_le_bytes([
                    self.buffer[1],
                    self.buffer[2],
                    self.buffer[3],
                    self.buffer[4],
                ]))),
                0x88 => Some(Event::Ready),
                _ => None,
            };
            self.reset();
            return event;
        }

        if matches!(self.mode, Mode::DiscardAscii | Mode::DiscardBinary) {
            if byte == 0xff {
                self.terminators += 1;
                if self.terminators == 3 {
                    self.reset();
                }
            } else {
                self.terminators = 0;
                if byte == b'\n' && self.mode == Mode::DiscardAscii {
                    self.reset();
                }
            }
            return None;
        }

        if self.mode == Mode::Ascii {
            if byte == b'\n' {
                let command = parse_command(&self.buffer[..self.length]).map(Event::Ascii);
                self.reset();
                return command;
            }
            if byte == b'\r' {
                return None;
            }
            if !(32..=126).contains(&byte) || self.length == self.buffer.len() - 1 {
                self.mode = Mode::DiscardAscii;
                self.length = 0;
                self.terminators = u8::from(byte == 0xff);
                return None;
            }
            self.buffer[self.length] = byte;
            self.length += 1;
            return None;
        }

        if byte == b'\r' || byte == b'\n' {
            return None;
        }
        self.expected = match byte {
            0x65 => 7,
            0x66 => 5,
            0x71 => 8,
            0x88 => 4,
            0x67 | 0x68 => 9,
            _ => 0,
        };
        if self.expected != 0 {
            self.mode = Mode::Binary;
            self.buffer[0] = byte;
            self.length = 1;
        } else if byte == b'M' || byte == b'D' {
            self.mode = Mode::Ascii;
            self.buffer[0] = byte;
            self.length = 1;
        } else {
            // String replies cannot execute an ASCII-looking payload.
            self.mode = if (32..=126).contains(&byte) && byte != 0x70 {
                Mode::DiscardAscii
            } else {
                Mode::DiscardBinary
            };
            self.terminators = u8::from(byte == 0xff);
        }
        None
    }
}
