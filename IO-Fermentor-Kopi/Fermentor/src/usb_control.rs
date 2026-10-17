//! Bounded, newline-delimited USB commands. No allocation or blocking reads.
//! MIXER <id> <STOP|RESUME> <boot_id> <revision> <expires_ms>\n

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    Stop,
    Resume,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Command {
    pub id: u32,
    pub action: Action,
    pub boot_id: u32,
    pub revision: u32,
    pub expires_ms: u64,
}

impl Command {
    pub fn parse(line: &str) -> Option<Self> {
        let mut words = line.split_ascii_whitespace();
        if words.next()? != "MIXER" {
            return None;
        }
        let command = Self {
            id: words.next()?.parse().ok()?,
            action: match words.next()? {
                "STOP" => Action::Stop,
                "RESUME" => Action::Resume,
                _ => return None,
            },
            boot_id: words.next()?.parse().ok()?,
            revision: words.next()?.parse().ok()?,
            expires_ms: words.next()?.parse().ok()?,
        };
        words.next().is_none().then_some(command)
    }

    /// STOP remains available even when status is stale. RESUME must refer to
    /// this boot/state and expire soon, so buffered/replayed commands fail shut.
    pub fn validate(
        self,
        boot_id: u32,
        revision: u32,
        now: u64,
        hmi: bool,
    ) -> Result<(), &'static str> {
        if self.action == Action::Stop {
            return Ok(());
        }
        if self.boot_id != boot_id || self.revision != revision {
            return Err("STATE_CHANGED");
        }
        if self.expires_ms <= now || self.expires_ms.saturating_sub(now) > 5_000 {
            return Err("EXPIRED");
        }
        if !hmi {
            return Err("HMI_OFFLINE");
        }
        Ok(())
    }
}

pub struct Decoder {
    bytes: [u8; 96],
    len: usize,
    discard: bool,
    last_byte_ms: u64,
}

impl Default for Decoder {
    fn default() -> Self {
        Self {
            bytes: [0; 96],
            len: 0,
            discard: false,
            last_byte_ms: 0,
        }
    }
}

impl Decoder {
    pub fn push(&mut self, byte: u8, now: u64) -> Option<Command> {
        if self.len > 0 && now.saturating_sub(self.last_byte_ms) > 500 {
            self.discard = true;
        }
        self.last_byte_ms = now;
        if byte == b'\n' {
            let command = if self.discard {
                None
            } else {
                core::str::from_utf8(&self.bytes[..self.len])
                    .ok()
                    .and_then(Command::parse)
            };
            self.len = 0;
            self.discard = false;
            return command;
        }
        if byte == b'\r' {
            return None;
        }
        if self.len == self.bytes.len() || (!byte.is_ascii_graphic() && byte != b' ') {
            self.discard = true;
        }
        if !self.discard {
            self.bytes[self.len] = byte;
            self.len += 1;
        }
        None
    }
}
