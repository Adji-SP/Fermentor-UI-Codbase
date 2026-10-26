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
| Automatic mixer timing and fixed PWM configuration | [`src/mixer_schedule.rs`](src/mixer_schedule.rs) |
| Website USB commands, acknowledgements and mixer telemetry | [`src/usb_console.rs`](src/usb_console.rs), [`src/usb_control.rs`](src/usb_control.rs) |

## Hardware mapping

| Device | ESP32-S3 connection | Rust behavior |
| --- | --- | --- |
| DS18B20 | DQ GPIO12, external VDD and GND | 1-Wire, 11-bit, CRC-checked; no SPI clock |
| ADS1115 pH + pressure | SDA GPIO8, SCL GPIO9, address 0x48 | A0 pH, A1 pressure, 8 single-shot samples each at 860 SPS |
| DFRobot O2 | same I2C bus, address 0x70 | Reads existing calibration key; no automatic calibration write |
| SCD41 | same I2C bus, address 0x62 | 5-second periodic CO2/temperature/RH measurement, CRC checked |
| SHT30 | same I2C bus, address 0x44 | temperature/RH, CRC checked; no SHT20 fallback |
| Nextion | ESP TX GPIO15 to Nextion RX; ESP RX GPIO16 from Nextion TX | UART1 at 9600 baud; page 0, t0-t10, buttons 13-15, slider 16 |
| HW-390/BTS7960 | LPWM GPIO7, RPWM GPIO6, L_EN GPIO5, R_EN GPIO4 | Automatic forward mixing; fixed example PWM 64/255; OFF at boot |

The four I2C devices share the existing one level shifter at 100 kHz. The
DS18B20 is **not** on I2C and must have a pull-up from DQ to 3.3 V (typically
4.7 kOhm). GPIO12 was the old MAX31865 CLK assignment; for this probe it is
1-Wire data. The code assumes the DS18B20 has a separate 3.3 V VDD connection
(not parasite power) and only one DS18B20 on GPIO12.

The pressure 0.5-4.5 V / 0-1.2 MPa mapping is retained. pH uses the measured
two-buffer calibration below. O2 readings outside 0-30 %vol are never
published as values; the startup policy below decides between a warm-up
indication and an error.

## pH two-buffer calibration (2026-10-25)

`src/sensors.rs` uses points measured with `../PH-Calibration` (filtered
ADS1115 A0 millivolts, saved and exported there):

| Buffer | Voltage |
| --- | --- |
| pH 6.85 | 1742.47 mV |
| pH 4.00 | 1667.72 mV |

```text
pH = 6.85 + (mV - 1742.47) * (4.00 - 6.85) / (1667.72 - 1742.47)
```

- This module's voltage **falls** toward acid (~26 mV/pH). The previous DFRobot
  example points (pH 7 = 1500.00 mV, pH 4 = 2032.44 mV) assumed the opposite
  direction and were removed.
- The temporary image-based -1.10 pH offset was removed; no offset is applied.
- USB `pH/ADS` publishes the calibrated value; `pH input` still prints raw mV.
  Startup prints the active points as a `[pH calibration]` event line.
- Values outside 0-14 report ERROR; they are never clamped.
- The probe drifted 7-20 mV between repeated buffer readings, roughly
  0.3-0.7 pH at this slope. Recheck buffers and recalibrate after probe
  maintenance. This firmware averages 10 conversions per reading, without the
  calibration tool's 64-sample outlier filter, so readings are noisier.
- Firmware must be flashed for any source/build change to affect the ESP32.

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
| ADS1115 pH/pressure | Ready-bit polling (up to ~20 ms) for each of eight samples at 860 SPS (~1.2 ms per conversion); no invented multi-minute delay |

Sensors are read in turn, one per 833 ms slot (DS18B20, pH, pressure, O2,
SCD41, SHT30), so no two I2C jobs overlap and one full cycle takes about 5 s.
The `SENSOR READINGS` header is printed with the DS18B20 slot and each sensor's
lines follow in its own slot, in the same order as before. A failed ADS1115,
O2 or SCD41 read marks that device offline; it is re-initialized on its next
slot at most once every 5 s and shows `OFFLINE` in between. When the SCD41 has
no new data yet, its last valid values are kept.

Timing references: [SCD4x datasheet](https://sensirion.com/media/documents/48C4B7FB/67FE0194/CD_DS_SCD4x_Datasheet_D1.pdf),
[DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf),
[SHT3x-DIS datasheet](https://sensirion.com/media/documents/213E6A3B/63A5A569/Datasheet_SHT3x_DIS.pdf),
and [ADS1115](https://www.ti.com/product/ADS1115).
The pH probe and pressure transmitter may need physical settling; without their
exact models, no additional fixed warm-up time is assumed. Existing calibration
and validity checks remain in use.

## Automatic mixer schedule

The schedule runs in the ESP32 firmware, not in a Nextion timer:

- First run: **30 seconds after firmware motor initialization**.
- Run duration: **2 minutes**, forward direction.
- Repeat interval: **6 hours between scheduled run starts**, not six hours
  after stopping. The first windows are 00:00:30-00:02:30 and
  06:00:30-06:02:30 relative to startup.
- Configured output: **PWM 64/255 (about 25%)**, copied from the existing
  example. **This is NOT a verified 3 RPM setting.** The desired 3 RPM is only
  a target: there is no shaft encoder or speed measurement in this wiring.
  Measure the mixer speed under its real load and adjust `MIXER_PWM_DUTY` in
  [`src/mixer_schedule.rs`](src/mixer_schedule.rs). Do not use PWM=3 as 3 RPM.

The existing Nextion connection safety interlock remains active. Page 0 must
be connected and answering `sendme` for a scheduled run to start. A lost link
(about 3 seconds), display restart after connection, or departure from page 0
stops the motor and **pauses future scheduled runs**. If no HMI is connected at
the first start deadline, it pauses without briefly energizing the motor.
Reconnecting alone does not restart it; explicitly press RESUME.

Nextion controls in this firmware:

| Existing component | Automatic-mode behavior |
| --- | --- |
| b0 / ID 13 (previous ON FWD) | Label becomes RESUME; resumes a paused schedule with a fresh 30-second delay |
| b1 / ID 14 (previous ON REV) | Label becomes LOCKED; touch disabled, reverse commands ignored |
| b2 / ID 15 (OFF / STOP) | Immediately stops and latches AUTO PAUSED, including between runs |
| h0 / ID 16 | Touch disabled; shows the fixed PWM setting, not RPM |
| t9 | AUTO WAIT, AUTO FWD, or AUTO PAUSED |
| t10 | Configured PWM value and percentage, never an unmeasured RPM claim |

Repeated RESUME commands cannot reset an active timer or extend a run. Legacy
`MOTOR:FWD` resumes, `MOTOR:STOP` pauses, and `MOTOR:SPEED:0` also pauses;
nonzero speed commands cannot override the fixed duty. PWM output errors also
pause the schedule. Sensor reads, warm-up handling, display input and motor
timers continue being serviced during automatic operation.

Timing is based on monotonic uptime, not wall-clock time. A power cycle/reset
restarts the schedule with the 30-second delay; STOP is not persisted across
resets. Missed windows are not replayed. A late loop never extends the planned
two-minute window; a partly missed window is shortened rather than made up.
Software timing is subject to loop/I2C latency, not an independent hardware
cutoff. Keep the physical emergency stop and motor-enable pull-downs in place.
**After flashing/resetting, expect automatic motion after 30 seconds when the
HMI is connected. Verify the direction and actual RPM with the machine safe.**

## Build and flash

### PowerShell shortcut

From this folder, disconnect USB in the dashboard and close other serial monitors,
then run:

```powershell
.\flash.ps1 -Port COM9
```

The script builds the **current Rust sources** in release mode with the locked
dependencies, then flashes the ELF using ESP32-S3 / DIO / 80 MHz / **16 MB**.
COM9 and 16 MB match the last detected board; use its current COM port and
`-FlashSize 8mb` only if the connected hardware really has 8 MB flash.
It works from any directory when invoked by its full path. Build failures stop
before flashing. It does not erase the entire flash or open a monitor by default.

```powershell
.\flash.ps1 -Port COM9 -WhatIf          # Preview only: no build, USB access or flash
.\flash.ps1 -BuildOnly -Offline        # Build only, using cached dependencies
.\flash.ps1 -Port COM9 -Offline        # Build and flash without dependency downloads
.\flash.ps1 -Port COM9 -Monitor        # Also open the 115200-baud terminal monitor
```

If local script execution is blocked, allow only this invocation (no persistent
execution-policy change):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\flash.ps1 -Port COM9
```

**Make the mixer physically safe first:** flashing/resetting restarts the
30-second automatic-start delay when the Nextion interlock is ready. Stop any
terminal monitor before reconnecting USB in the website. Adding this script does
not itself flash or reset the board.

### Manual commands

Build the active firmware with the installed ESP Rust toolchain:

```powershell
cd D:\PropertiesProject-D\ELKA\Fermentor\IO-Fermentor-Kopi\Fermentor
cargo build --release
```

Flash the built firmware and open the serial monitor from that same folder:

```powershell
espflash -S flash --chip esp32s3 --port COM9 --flash-mode dio --flash-freq 80mhz --flash-size 16mb --monitor target\xtensa-esp32s3-none-elf\release\fermentor_kopi
```

Replace `COM9` with the board's actual USB port. Disconnect browser USB and close
any serial monitor or legacy dashboard backend holding that port before flashing.

`cargo run --release` flashes and opens the monitor because `.cargo/config.toml`
sets `espflash flash --monitor` as the runner. Use it only with the intended
ESP32-S3 connected. The binary is under
`target/xtensa-esp32s3-none-elf/release/fermentor_kopi`. Debug output uses the
ESP32-S3 USB Serial/JTAG port; it is separate from Nextion UART1.

### Single-file flash image

The image below includes website mixer monitoring/control, the automatic
schedule, t0-t10 interface and O2 warm-up update. Earlier automatic-mixer,
warm-up, single-page and original merged images
remain previous-firmware backups.

The [merged ESP32-S3 firmware](../fermentor_kopi_web_mixer_esp32s3_merged.bin)
contains the bootloader, partition table, and Rust application in one `.bin`.
This existing merged image targets the earlier ESP32-S3-N8R2 configuration
(8 MB flash, DIO, 80 MHz); it is not regenerated by `flash.ps1`. The currently
detected board has 16 MB flash. Prefer the script above for current sources and
that board. Write a compatible merged image at flash address `0x0`:

```powershell
espflash write-bin --chip esp32s3 --port COM8 0x0 ..\fermentor_kopi_web_mixer_esp32s3_merged.bin
```

Replace `COM8` with the port actually connected to this board. To regenerate
the file after source changes, run from this `Fermentor` folder:

```powershell
cargo build --release
espflash save-image --chip esp32s3 --merge --skip-padding --flash-mode dio --flash-freq 80mhz --flash-size 8mb target\xtensa-esp32s3-none-elf\release\fermentor_kopi ..\fermentor_kopi_web_mixer_esp32s3_merged.bin
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

Sensor waits service HMI input, USB commands and motor timers, and display refreshes transmit
one widget per main-loop tick. Individual I2C transactions remain blocking with
a 100 ms timeout; this is not a hard real-time emergency stop. Raw pH millivolts
and pressure volts are printed for calibration. A missing I2C device is reported
as `OFFLINE`, while bad CRC/data and conversion timeout are `ERROR`.

## Website mixer control

The React website in `Monitor_Framework_ALBON` now receives `[MIXER_STATUS]`
JSON lines every second and on control-state changes, separate from sensor
frames. It displays applied PWM, schedule state/countdowns and HMI readiness.
STOP/RESUME commands are received on native USB, with explicit firmware ACKs;
they never replace the Nextion keepalive or change the fixed speed/direction.
RESUME requires a matching boot/state, short uptime-based expiry and live HMI.
The USB RX parser is bounded and nonblocking; `esp-println` remains the sole TX
writer. Existing sensor calibration and I2C timing are unchanged.

See [website setup and safety limits](../../Monitor_Framework_ALBON/MIXER_CONTROL.md).
Close any serial monitor before starting the website backend; both need the
same COM port. A lost website does not stop the autonomous schedule. Software
STOP is not a physical emergency stop. Do not flash with an unsafe mixer load.

## HMI contract

The finished screen is page 0: t0-t10 have IDs 2-12, b0/b1/b2 have IDs
13/14/15, and slider h0 has ID 16. Follow [`NEXTION_HMI.md`](NEXTION_HMI.md)
and the [shared setup guide](../hmi-display/NEXTION_SINGLE_PAGE.md) for the
exact sensor mapping and Editor checkboxes.

Native button releases now implement RESUME and STOP as described above;
reverse and slider changes are locked by the automatic-mode policy. The
ESP32 polls sendme every second, so the HMI needs no heartbeat timer.
Loss of page replies for about three seconds stops and pauses the schedule.

Earlier newline-terminated MOTOR:* commands and DASH:SYNC remain parsed, but
cannot bypass the automatic-mode restrictions. Display fields use t0-t10.
The screenshot/component list does not provide an editable .HMI file;
set the event checkboxes and compile/upload the screen in Nextion Editor.
