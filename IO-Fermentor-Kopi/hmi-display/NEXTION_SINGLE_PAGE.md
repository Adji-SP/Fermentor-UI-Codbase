# Finished Nextion single-page dashboard

This contract matches the user's finished 320x240 screen on page ID 0
(2026-10-09). It applies to the current [PlatformIO/C++](../_legacy_platformio/README.md)
and [Rust](../Fermentor/README.md) projects.

## Exact mapping

Text updates use the component name. Native touch messages use the page and ID.

| Component | ID | Reading or action |
| --- | --- | --- |
| t0 | 2 | DS18B20 temperature, C |
| t1 | 3 | pH |
| t2 | 4 | SCD41 temperature, C |
| t3 | 5 | SHT30 temperature, C |
| t4 | 6 | Pressure, bar |
| t5 | 7 | Oxygen, %Vol |
| t6 | 8 | SCD41 CO2, ppm |
| t7 | 9 | SCD41 relative humidity, % |
| t8 | 10 | SHT30 relative humidity, % |
| t9 | 11 | Motor: OFF, ON FWD, ON REV, SWITCHING |
| t10 | 12 | PWM, for example 64/255 (25%) |
| b0 | 13 | ON FWD |
| b1 | 14 | ON REV |
| b2 | 15 | OFF / STOP |
| h0 | 16 | Speed slider, 0-255 |

All eleven values appear on page 0. Invalid sensor values display WAIT,
OFFLINE, or ERROR. This firmware no longer writes the older gauge names/pages.

## Nextion Editor settings

1. Set txt_maxl to at least 20 on t0-t10. Choose font sizes/widths that fit the
   values, including the full PWM string.
2. On b0, b1 and b2, enable **Touch Release Event > Send Component ID**.
   Leave the Touch Press Event checkbox unchecked.
3. Set h0 to minval=0, maxval=255, val=64. Enable **Send Component ID** on both
   Touch Press Event and Touch Release Event.
4. Leave custom button/slider event code empty for this native-ID setup.
5. Optionally add the following to page 0's Postinitialize Event for immediate
   synchronization. The firmware also polls automatically.

```text
sendme
```

No timer or hidden text component is required, so all existing IDs stay unchanged.
The user's startup code can remain:

```text
int sys0=0,sys1=0,sys2=0
baud=9600
dim=100
recmod=0
printh 00 00 00 ff ff ff 88 ff ff ff
page 0
```

The startup code initializes the screen; touch reporting requires the Editor
checkboxes above. Compile/upload your finished HMI after setting them.
ESP32 firmware cannot enable these project event checkboxes remotely.

## Behavior

The motor starts OFF with PWM setting 64. After page 0 responds, the ESP32
fills t0-t10 and synchronizes h0 with the accepted PWM setting.

- Button releases request forward, reverse, or stop.
- Slider release makes the ESP32 send get h0.val. A numeric reply is accepted
  only for a pending request, within 500 ms, and in the range 0-255.
  Speed changes when the slider is released, not continuously during dragging.
- Speed zero stops the motor. Raising it afterward changes the setting without
  restarting; press a direction button to run.
- Reversal pauses the motor for 250 ms, including when commands repeat.
- The ESP32 sends sendme once per second. Losing page replies for about three
  seconds, a display reboot, or a reply for another page stops the motor.
- Sensor wiring, addresses and calibration are unchanged by this screen update.

The firmware publishes one text field per tick so motor commands can be
processed between field updates.

## Wiring and upload

UART1 remains 9600 baud: ESP32 GPIO15 TX -> Nextion RX, GPIO16 RX <- Nextion TX,
using the existing wiring and common ground.

Upload one firmware implementation to the ESP32. Compile/upload the Nextion
project separately in Nextion Editor.

From IO-Fermentor-Kopi/_legacy_platformio:

```powershell
pio run
pio run --target upload
pio device monitor --baud 115200
```

From IO-Fermentor-Kopi/Fermentor:

```powershell
cargo build --release
cargo run --release
```

No hardware was flashed while implementing this mapping.

## Software checks

The C++ host harness runs the actual hmi_motor.cpp with simulated UART, GPIO
and time. It checks all eleven labels, button IDs, slider replies, zero/stop,
reversal timing, malformed/stale packets and connection loss.

From the PlatformIO folder, with g++ on PATH:

```powershell
New-Item -ItemType Directory -Force -Path .pio/host-tests | Out-Null
g++ -static -std=c++17 -Wall -Wextra -Werror -I test/host test/host/nextion_test.cpp src/hmi_motor.cpp -o .pio/host-tests/nextion_test.exe
.\.pio\host-tests\nextion_test.exe
```

From the Rust folder:

```powershell
cargo +stable test --lib --target x86_64-pc-windows-msvc --offline
```

The active Rust tests verify startup, native IDs, fragmented/corrupt frames,
numeric values containing LF/FF bytes, and ASCII compatibility. These checks
do not verify the physical display or motor.

Native framing follows the [manufacturer's library](https://github.com/itead/ITEADLIB_Arduino_Nextion/blob/master/NexHardware.cpp)
and [instruction reference](https://nextion.ca/nis-2/): touch=0x65, page=0x66,
number=0x71, ready=0x88, followed by FF FF FF. Touch release is event 0.
The [Editor guide](https://nextion.ca/editor-guide/) documents Send Component ID.
