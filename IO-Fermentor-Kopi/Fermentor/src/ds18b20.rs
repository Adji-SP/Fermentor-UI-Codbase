//! Single, externally powered DS18B20 on the confirmed GPIO12 wire.
use esp_hal::{
    gpio::{Flex, InputConfig, OutputConfig},
    time::Instant,
    xtensa_lx,
};
use fermentor_kopi::sensors::{decode_ds18b20, Reading};

const CYCLES_PER_US: u32 = 240; // main explicitly sets CpuClock::max().

fn delay_us(us: u32) {
    xtensa_lx::timer::delay(us * CYCLES_PER_US);
}

pub struct Ds18b20<'d> {
    pin: Flex<'d>,
    requested_at: Instant,
    pending: bool,
}

impl<'d> Ds18b20<'d> {
    pub fn new(pin: impl esp_hal::gpio::Pin + 'd) -> Self {
        let mut pin = Flex::new(pin);
        pin.apply_input_config(&InputConfig::default());
        pin.apply_output_config(&OutputConfig::default());
        pin.set_low();
        pin.set_input_enable(true);
        pin.set_output_enable(false);
        Self {
            pin,
            requested_at: Instant::now(),
            pending: false,
        }
    }

    fn reset(&mut self) -> bool {
        critical_section::with(|_| {
            self.pin.set_low();
            self.pin.set_output_enable(true);
            delay_us(480);
            self.pin.set_output_enable(false);
            delay_us(70);
            let present = self.pin.is_low();
            delay_us(410);
            // A shorted DQ can look like a presence pulse; require release.
            present && self.pin.is_high()
        })
    }

    fn write_bit(&mut self, one: bool) {
        critical_section::with(|_| {
            self.pin.set_low();
            self.pin.set_output_enable(true);
            if one {
                delay_us(6);
            } else {
                delay_us(60);
            }
            self.pin.set_output_enable(false);
            if one {
                delay_us(64);
            } else {
                delay_us(10);
            }
        });
    }

    fn read_bit(&mut self) -> bool {
        critical_section::with(|_| {
            self.pin.set_low();
            self.pin.set_output_enable(true);
            delay_us(3);
            self.pin.set_output_enable(false);
            delay_us(10);
            let high = self.pin.is_high();
            delay_us(53);
            high
        })
    }

    fn write_byte(&mut self, mut byte: u8) {
        for _ in 0..8 {
            self.write_bit(byte & 1 != 0);
            byte >>= 1;
        }
    }

    fn read_byte(&mut self) -> u8 {
        let mut byte = 0u8;
        for bit in 0..8 {
            if self.read_bit() {
                byte |= 1 << bit;
            }
        }
        byte
    }

    pub fn request(&mut self) -> bool {
        if !self.reset() {
            self.pending = false;
            return false;
        }
        self.write_byte(0xCC); // Skip ROM; one probe on this wire.
        self.write_byte(0x4E); // Write volatile scratchpad, 11-bit resolution.
        self.write_byte(0x4B); // TH
        self.write_byte(0x46); // TL
        self.write_byte(0x5F); // R1:R0=10 -> 11-bit
        if !self.reset() {
            self.pending = false;
            return false;
        }
        self.write_byte(0xCC);
        self.write_byte(0x44); // Convert T; externally powered 3-wire probe.
        self.requested_at = Instant::now();
        self.pending = true;
        true
    }

    pub fn sample(&mut self) -> Reading {
        if !self.pending {
            return if self.request() {
                Reading::wait()
            } else {
                Reading::offline()
            };
        }
        if (Instant::now() - self.requested_at).as_millis() < 400 {
            return Reading::wait();
        }
        if !self.reset() {
            self.pending = false;
            return Reading::offline();
        }
        self.write_byte(0xCC);
        self.write_byte(0xBE); // Read scratchpad, including Dallas CRC.
        let mut data = [0u8; 9];
        for byte in &mut data {
            *byte = self.read_byte();
        }
        self.pending = false;
        let result = decode_ds18b20(&data)
            .map(Reading::ok)
            .unwrap_or_else(|_| Reading::error());
        let _ = self.request();
        result
    }
}
