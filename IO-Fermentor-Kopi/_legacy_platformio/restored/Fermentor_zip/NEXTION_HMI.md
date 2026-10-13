# Fermentor Kopi Nextion dashboard

The current NX3224F024 interface is a three-page gauge dashboard with a shared
bottom navbar and a dedicated motor-control page.

It displays the five installed sensors (DS18B20, O2, pH, SCD41 and SHT30),
plus pressure from ADS1115 A1. The SHT30 chamber readings appear on the
environment page; pressure appears on the control page.

Use the complete build sheet and generated assets in
[`../hmi-display/README.md`](../hmi-display/README.md).

The Rust firmware in `src/main.rs` and `src/nextion.rs` implements matching
`DASH:PAGE:0`, `DASH:PAGE:1`, and
`DASH:PAGE:2` synchronization commands, native gauge updates, motor feedback,
slider synchronization, and the three-second keepalive safety timeout.

## Rust firmware integration

The display connects to UART1: ESP32 TX GPIO15 to Nextion RX, ESP32 RX GPIO16
from Nextion TX, at 9600 baud. The existing page navigation and event scripts
remain compatible with the Rust firmware; do not rename the native components.

| Nextion page | Component pairs updated by Rust |
| --- | --- |
| `page0` process | DS18B20 `gPt`/`tPt`, O2 `gO2`/`tO2`, pH `gPh`/`tPh`, CO2 `gCo2`/`tCo2` |
| `page1` environment | SCD41 `gScT`/`tScT` and `gScH`/`tScH`; SHT30 `gChT`/`tChT` and `gChH`/`tChH` |
| `page2` controls | Motor `tMotor`, duty `tDuty`, pressure `tPressure`, slider `h0` |

`gPt`/`tPt` are retained for compatibility; they display the **DS18B20**, not
a PT100. SCD41 temperature/RH are outputs of the existing CO2 sensor, not extra
physical sensors. Pressure retains the existing ADS1115 A1 input.

Page events send `DASH:PAGE:0`, `DASH:PAGE:1`, or `DASH:PAGE:2`. Motor buttons
send `MOTOR:FWD`, `MOTOR:REV`, and `MOTOR:STOP`; the slider sends
`MOTOR:SPEED:0` through `MOTOR:SPEED:255`. Every page must keep its one-second
timer sending `MOTOR:KEEPALIVE`. Each message ends with LF (`printh 0A`).
The complete event scripts remain in the linked build sheet.

[`src/protocol.rs`](src/protocol.rs) validates incoming commands;
[`src/nextion.rs`](src/nextion.rs) sends gauge/text values with the required
three `0xFF` terminators. Sensor conversion waits continue processing motor
commands and keepalive. Repeated direction commands cannot skip a pending
250 ms reversal pause. Motor outputs start OFF, and raising speed after STOP
does not start them until a direction button is pressed.

Only the ESP32 firmware is compiled with Cargo. The screen's native events,
pages and image resources are still built in Nextion Editor; the repository
contains the build sheet/assets, not a compiled `.HMI` or `.tft` project.

## Probe wiring reminder

The DS18B20 is 1-Wire, not a PT100/MAX31865 probe. Its DQ wire is on ESP32
GPIO12 in the Rust firmware (`src/main.rs`). GPIO12 was the old MAX31865 CLK assignment, but for
this probe it is 1-Wire data, not a clock. Use a pull-up from DQ to 3.3 V and
ensure the old MAX31865 clock circuit is disconnected. All four I2C devices
share SDA GPIO8 and SCL GPIO9 through the one level shifter.
