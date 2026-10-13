# Fermentor Kopi firmware (ESP32-S3, Rust)

This is now a Rust-only firmware project for the existing IO-Fermentor-Kopi
hardware. Cargo builds the actual sensor, Nextion and motor firmware in Rust;
it is not a wrapper that flashes an Arduino/C++ binary. The previous C++
sources, `platformio.ini`, and PIO-specific
editor settings were moved to
[`../_legacy_platformio`](../_legacy_platformio/README.md) as a recoverable
backup outside this project. The Rust project in `Test-Fermentor-Kopi` was used
only as a toolchain/build example, **not** as the source for sensor wiring or
behavior (it has older PT100 and O2-address assumptions).

## Firmware source map

| Original PlatformIO responsibility | Rust implementation |
| --- | --- |
| `main.cpp`: setup, pins, 5-second sampling loop | [`src/main.rs`](src/main.rs) |
| `main.cpp`: ADS1115, pH/pressure conversion, O2, SCD41, SHT30 | [`src/sensors.rs`](src/sensors.rs) |
| DallasTemperature / OneWire on GPIO12 | [`src/ds18b20.rs`](src/ds18b20.rs) |
| `hmi_motor.cpp`: Nextion single-page t0-t10 values and slider synchronization | [`src/nextion.rs`](src/nextion.rs) |
| Native touch/page/numeric frames and earlier ASCII events | [`src/nextion_wire.rs`](src/nextion_wire.rs), [`src/protocol.rs`](src/protocol.rs) |
| `hmi_motor.cpp`: motor PWM, enables and direction | [`src/motor.rs`](src/motor.rs) |
| Motor stop, reversal pause and keepalive policy | [`src/motor_control.rs`](src/motor_control.rs) |

## Hardware mapping

| Device | ESP32-S3 connection | Rust behavior |
| --- | --- | --- |
| DS18B20 | DQ GPIO12, external VDD and GND | 1-Wire, 11-bit, CRC-checked; no SPI clock |
| ADS1115 pH + pressure | SDA GPIO8, SCL GPIO9, address 0x48 | A0 pH, A1 pressure, 10 single-shot samples each |
| DFRobot O2 | same I2C bus, address 0x70 | Reads existing calibration key; no automatic calibration write |
| SCD41 | same I2C bus, address 0x62 | 5-second periodic CO2/temperature/RH measurement, CRC checked |
| SHT30 | same I2C bus, address 0x44 | temperature/RH, CRC checked; no SHT20 fallback |
| Nextion | ESP TX GPIO15 to Nextion RX; ESP RX GPIO16 from Nextion TX | UART1 at 9600 baud; page 0, t0-t10, buttons 13-15, slider 16 |
| HW-390/BTS7960 | LPWM GPIO7, RPWM GPIO6, L_EN GPIO5, R_EN GPIO4 | HMI direction and 0-255 speed control; OFF at boot |

The four I2C devices share the existing one level shifter at 100 kHz. The
DS18B20 is **not** on I2C and must have a pull-up from DQ to 3.3 V (typically
4.7 kOhm). GPIO12 was the old MAX31865 CLK assignment; for this probe it is
1-Wire data. The code assumes the DS18B20 has a separate 3.3 V VDD connection
(not parasite power) and only one DS18B20 on GPIO12.

The original pH calibration points and pressure 0.5-4.5 V / 0-1.2 MPa mapping
are retained. Calibrate those values on the real hardware before relying on
absolute readings. O2 readings outside 0-30 %vol are never published as values;
the startup policy below decides between a warm-up indication and an error.

## Sensor startup and warm-up

O2 has a **180-second startup window**, measured from ESP32 firmware startup.
There is no three-minute blocking delay: reads continue on the normal sampling
schedule, and Nextion input, slider control and motor safety timers keep running.
The first sampling block starts after the DS18B20's 400 ms conversion wait;
following blocks remain five seconds apart. Other sensors do not wait for O2.

- A valid O2 sample is displayed immediately on Nextion and USB, even before
  three minutes. Once a valid sample arrives, the countdown never replaces it
  again during that boot. Later bad samples are errors, not a new warm-up.
- Before the first valid O2 sample, an out-of-range result shows
  `WARMUP 02:59` on Nextion. The countdown uses minutes/seconds and updates
  approximately once per second, not raw milliseconds.
- USB reports `O2        : WAIT (WARMUP 02:59)` inside the regular sample block.
  The existing web parser therefore treats it as WAIT/null, not as a numeric
  oxygen reading. Valid samples retain `O2        : 20.90 %Vol`.
- I2C disconnections, timeouts and other bus faults stay OFFLINE/ERROR, even
  during warm-up. An unusable O2 sample after 180 seconds is ERROR. Diagnostic
  lines still report the cause during warm-up; no calibration is overwritten.

An early valid sample means it passes the driver's numeric checks, **not** that
the sensor is physically stabilized. DFRobot describes about three minutes of
warm-up and also notes that stabilization can take about ten minutes in its
[SEN0322 example](https://wiki.dfrobot.com/sen0322/docs/19924).
The firmware cannot detect independent sensor power cycles or probe settling.

| Sensor | Startup/conversion timing used |
| --- | --- |
| O2 SEN0322 | 180 s startup display window; valid data wins immediately |
| SCD41 | Explicit 30 ms power-up guard, existing 500 ms stop/start wait, then data-ready polling for 5 s periodic measurements |
| DS18B20 | Existing 400 ms conversion guard for 11-bit operation (375 ms maximum conversion) |
| SHT30 | Existing 20 ms high-repeatability measurement wait and CRC checks; initial setup already exceeds its short power-up time |
| ADS1115 pH/pressure | Existing 9 ms conversion wait plus ready-bit polling for each of ten samples at 128 SPS; no invented multi-minute delay |

Timing references: [SCD4x datasheet](https://sensirion.com/media/documents/48C4B7FB/67FE0194/CD_DS_SCD4x_Datasheet_D1.pdf),
[DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf),
[SHT3x-DIS datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf),
and [ADS1115](https://www.ti.com/product/ADS1115).
The pH probe and pressure transmitter may need physical settling; without their
exact models, no additional fixed warm-up time is assumed. Existing calibration
and validity checks remain in use.

## Build and flash

Build the active firmware with the installed ESP Rust toolchain:

```powershell
cd D:\PropertiesProject-D\ELKA\Fermentor\IO-Fermentor-Kopi\Fermentor
cargo build --release
```

Flash the built firmware and open the serial monitor from that same folder:

```powershell
espflash flash --chip esp32s3 --port COM8 --flash-mode dio --flash-freq 80mhz --flash-size 8mb --monitor target\xtensa-esp32s3-none-elf\release\fermentor_kopi
```

Replace `COM8` with the board's actual USB port. Close any serial monitor or
dashboard backend holding that port before flashing.

`cargo run --release` flashes and opens the monitor because `.cargo/config.toml`
sets `espflash flash --monitor` as the runner. Use it only with the intended
ESP32-S3 connected. The binary is under
`target/xtensa-esp32s3-none-elf/release/fermentor_kopi`. Debug output uses the
ESP32-S3 USB Serial/JTAG port; it is separate from Nextion UART1.

### Single-file flash image

The image below includes the finished t0-t10 interface and the O2 warm-up update.
The older `fermentor_kopi_nextion_singlepage_esp32s3_merged.bin` and
`fermentor_kopi_esp32s3_n8r2_merged.bin` remain previous-firmware backups.

The [merged ESP32-S3 firmware](../fermentor_kopi_nextion_warmup_esp32s3_merged.bin)
contains the bootloader, partition table, and Rust application in one `.bin`.
It targets the project's ESP32-S3-N8R2 board configuration (8 MB flash, DIO,
80 MHz). Write this merged image at flash address `0x0`:

```powershell
espflash write-bin --chip esp32s3 --port COM8 0x0 ..\fermentor_kopi_nextion_warmup_esp32s3_merged.bin
```

Replace `COM8` with the port actually connected to this board. To regenerate
the file after source changes, run from this `Fermentor` folder:

```powershell
cargo build --release
espflash save-image --chip esp32s3 --merge --skip-padding --flash-mode dio --flash-freq 80mhz --flash-size 8mb target\xtensa-esp32s3-none-elf\release\fermentor_kopi ..\fermentor_kopi_nextion_warmup_esp32s3_merged.bin
```

The Rust release build has been checked locally; flashing, sensor response,
Nextion interaction, motor direction and emergency stopping still require
hardware verification. Test motor direction without a load first. Provide
external pull-downs on both motor enable inputs so the bridge stays disabled
during reset, boot and a lost ESP power supply. Keep a physical emergency stop
on the motor power circuit; software timeout is not a substitute.

## Checks without flashing

From this folder on Windows:

```powershell
cargo fmt --all -- --check
cargo clippy --release --offline -- -D warnings
cargo build --release --offline
```

Test-only modules, mock devices, and commented-out tests have been removed
from this firmware project. They were already excluded from release builds;
that cleanup did not change runtime behavior. The checks above
validate formatting, linting, and compilation, not physical hardware behavior.
The `--offline` commands assume the dependencies are already downloaded.

Sensor waits service HMI input and motor timers, and display refreshes transmit
one widget per main-loop tick. Individual I2C transactions remain blocking with
a 100 ms timeout; this is not a hard real-time emergency stop. Raw pH millivolts
and pressure volts are printed for calibration. A missing I2C device is reported
as `OFFLINE`, while bad CRC/data and conversion timeout are `ERROR`.

## HMI contract

The finished screen is page 0: t0-t10 have IDs 2-12, b0/b1/b2 have IDs
13/14/15, and slider h0 has ID 16. Follow [`NEXTION_HMI.md`](NEXTION_HMI.md)
and the [shared setup guide](../hmi-display/NEXTION_SINGLE_PAGE.md) for the
exact sensor mapping and Editor checkboxes.

Native button releases control the motor. Slider release requests h0.val.
The ESP32 polls sendme every second, so the HMI needs no heartbeat timer.
Loss of page replies for about three seconds stops the motor. Reversal has
a 250 ms stop interval; raising speed after STOP does not restart the motor.

Earlier newline-terminated MOTOR:* commands and DASH:SYNC remain accepted
for compatibility. Display fields now use the single-page t0-t10 names.
The screenshot/component list does not provide an editable .HMI file;
set the event checkboxes and compile/upload the screen in Nextion Editor.
