//! The existing Nextion event protocol: printable ASCII followed by LF.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Command {
    Page(u8),
    Keepalive,
    Forward,
    Reverse,
    Stop,
    Speed(u8),
}

pub(crate) fn parse_command(bytes: &[u8]) -> Option<Command> {
    if bytes == b"DASH:SYNC" {
        return Some(Command::Page(0));
    }
    if let Some(page) = bytes.strip_prefix(b"DASH:PAGE:") {
        return match page {
            b"0" => Some(Command::Page(0)),
            b"1" => Some(Command::Page(1)),
            b"2" => Some(Command::Page(2)),
            _ => None,
        };
    }
    match bytes {
        b"MOTOR:KEEPALIVE" => return Some(Command::Keepalive),
        b"MOTOR:FWD" => return Some(Command::Forward),
        b"MOTOR:REV" => return Some(Command::Reverse),
        b"MOTOR:STOP" => return Some(Command::Stop),
        _ => {}
    }
    let digits = bytes.strip_prefix(b"MOTOR:SPEED:")?;
    if digits.is_empty() || digits.len() > 3 {
        return None;
    }
    let mut speed: u16 = 0;
    for digit in digits {
        if !digit.is_ascii_digit() {
            return None;
        }
        speed = speed * 10 + u16::from(digit - b'0');
    }
    u8::try_from(speed).ok().map(Command::Speed)
}

pub struct Decoder {
    input: [u8; 48],
    length: usize,
    discarding: bool,
    terminators: u8,
}

impl Default for Decoder {
    fn default() -> Self {
        Self::new()
    }
}

impl Decoder {
    pub const fn new() -> Self {
        Self {
            input: [0; 48],
            length: 0,
            discarding: false,
            terminators: 0,
        }
    }

    /// After a UART error, require a complete frame boundary before accepting
    /// another command. Never execute a valid-looking suffix of a damaged line.
    pub fn discard(&mut self) {
        self.length = 0;
        self.discarding = true;
        self.terminators = 0;
    }

    pub fn push(&mut self, byte: u8) -> Option<Command> {
        if byte == 0xFF {
            self.length = 0;
            self.discarding = true;
            self.terminators += 1;
            if self.terminators == 3 {
                // Native Nextion response/touch packets end with FF FF FF.
                self.discarding = false;
                self.terminators = 0;
            }
            return None;
        }
        self.terminators = 0;
        if byte == b'\n' {
            let command = if self.discarding {
                None
            } else {
                parse_command(&self.input[..self.length])
            };
            self.length = 0;
            self.discarding = false;
            return command;
        }
        if self.discarding || byte == b'\r' {
            return None;
        }
        if !byte.is_ascii_graphic() && byte != b' ' || self.length == self.input.len() {
            self.discard();
            return None;
        }
        self.input[self.length] = byte;
        self.length += 1;
        None
    }
}
