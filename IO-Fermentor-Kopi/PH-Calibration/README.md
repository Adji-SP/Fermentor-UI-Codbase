# Standalone pH measurement and calibration (PlatformIO)

Open **this folder** as a PlatformIO project. It is separate from `../Fermentor`
(Rust) and `../_legacy_platformio`. Those projects are not changed by this tool.
The application entry point is [`src/main.cpp`](src/main.cpp); calibration math
and the stability/capture checks are in [`include/ph_calibration.h`](include/ph_calibration.h).
Block filtering is in [`include/ph_filter.h`](include/ph_filter.h).

Only the pH module is measured. No SHT30, SCD41, oxygen, pressure, DS18B20,
Nextion, Wi-Fi, Bluetooth, dashboard control, or automatic mixing is started.
GPIO5/4 (motor enables) and GPIO7/6 (PWM pins) are only held LOW as a safeguard.
Disconnect motor power before flashing/testing; software cannot control pin
states throughout reset. Keep the existing hardware pull-downs in place.

**Uploading replaces the firmware currently running on the ESP32.** It does not
delete the main project's source files. Reflash the main firmware when finished.
This pH-only serial format is intended for a terminal, not the nine-sensor web
dashboard parser.

## Existing wiring (unchanged)

| Signal | Connection |
| --- | --- |
| pH probe | DFRobot module BNC input |
| DFRobot analog output | ADS1115 A0 (not directly into SDA/SCL) |
| Ground | Common module/ADS1115/ESP32 ground |
| ADS1115 address | `0x48` (ADDR tied to GND) |
| SDA | ESP32 GPIO8 through the existing level shifter to HV2/SDA |
| SCL | ESP32 GPIO9 through the existing level shifter to HV1/SCL |

The existing shared I2C bus remains **100 kHz**. Only ADS1115 A0 is accessed.
Other devices can stay physically connected, but this program neither starts
nor reads them; a device holding the shared bus low can still block the ADS1115.
Preserve the existing correctly rated module power and level-shifter rails.
An ADS gain range is not permission to exceed the ADC supply/input limits.

The ADC uses single-shot conversions, **128 SPS**, **+/-4.096 V**, and
**0.125 mV/count**. Each displayed reading uses **64 conversions over about one
second**, with conversion starts scheduled 15.625 ms apart. Bus and conversion
timeouts remain active. Median/MAD filtering removes isolated outliers, then
averages the retained readings. The same filtered voltage feeds calibration and
normal pH conversion. Raw voltage, raw spread, retained standard deviation,
rejected sample count and filter shift remain visible. There is no long-term
moving average or automatic drift correction.

## Build, upload and monitor

From a PlatformIO terminal:

```powershell
cd D:\PropertiesProject-D\ELKA\Fermentor\IO-Fermentor-Kopi\PH-Calibration
pio run -e ph-esp32s3
```

Disconnect the dashboard's USB session and close other monitors first. Make
the machine safe, then use the board's **current** port. `COM9` was the last
observed port, not a guarantee that Windows will keep that assignment:

```powershell
pio run -e ph-esp32s3 -t upload --upload-port COM9
pio device monitor -e ph-esp32s3 --port COM9 --baud 115200
```

If `pio` is not on PATH, use the installed executable instead. For example:

```powershell
$pioExe = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pioExe run -e ph-esp32s3
& $pioExe run -e ph-esp32s3 -t upload --upload-port COM9
& $pioExe device monitor -e ph-esp32s3 --port COM9 --baud 115200
```

Default console: **native ESP32-S3 USB Serial/JTAG**, not Nextion UART.
For a USB-to-UART bridge connected to UART0, replace the environment with
`ph-esp32s3-uart` in all commands. Only one serial console is enabled per build.
There is no infinite wait for a terminal; if you missed startup text, type `help`.

The platform is pinned to installed Espressif32 **6.6.0 / Arduino 2.0.14**.
Only built-in `Wire` and `Preferences` libraries are used. The conservative
8 MB board layout also fits the previously detected 16 MB board; it does not
use PSRAM. Confirm the actual hardware if using a different board.

Build files:

```text
.pio/build/ph-esp32s3/firmware.bin
.pio/build/ph-esp32s3/firmware.elf
```

`firmware.bin` is the application image, **not a merged image to write at 0x0**.
Use PlatformIO upload so the matching bootloader and partition table are written.

## Calibrate with actual buffers

**The temporary image-based -1.10 pH offset is deliberately NOT used here.**
Without saved buffer calibration, serial output includes a converted pH marked
`ESTIMATE_UNCALIBRATED`. This display-only estimate uses example points of
1500.00 mV at pH 7.00 and 2032.44 mV at pH 4.00. These are **not measured
calibration points for your probe** and are never saved as such. Once both
buffers are captured and saved, conversion uses those measured points and the
label changes to `CALIBRATED`.

For example, 1732.000 mV gives this **illustrative, not live** output:

```text
pH: 5.693 | ESTIMATE_UNCALIBRATED | filtered=1732.000 mV | raw=1732.000 mV ...
```

Typing `cal7 7.00` starts voltage capture; it does **not** force the displayed
value to 7.00. Until a complete calibration is saved, the estimate (or an older
saved calibration) stays active.

1. Clean and condition the probe according to its manufacturer's instructions.
   Use fresh, separate aliquots of pH 7 and pH 4/4.01 buffer. Rinse between
   buffers; do not return used buffer to its bottle.
2. Place the probe's bulb and junction properly in pH 7 buffer. Let it settle
   physically, then type `cal7 7.00` and press Enter. Use the buffer's certified
   value **at the measured temperature**, not necessarily the bottle's 25 C value.
3. Wait for `CAPTURED (RAM only)`. Keep the probe in that buffer until capture
   finishes. Every capture starts a new 30-second window and discards any
   partially acquired block, so samples from the previous liquid are not reused.
4. Rinse, move to the acidic buffer, allow settling, then type `cal4 4.00` or
   `cal4 4.01` (or its temperature-adjusted certified value). Wait for `CAPTURED`.
5. Type `status` to inspect both measured voltages, then `save`. Both newly
   captured points are required. The previous saved calibration stays active
   during capture; a pending point alone does not change measurement conversion.
6. Recheck fresh buffer aliquots and an independent sample against the calibrated
   reference meter. Returning to the first buffer checks repeatability. Watch the
   raw mV over time to see whether the probe still drifts.

Do not paste `cal7`, `cal4`, and `save` together: these commands correspond to
different physical liquids and require successful capture in each.

```text
cal7 7.00       # Enter only after putting the probe in the neutral buffer
cal4 4.01      # Enter later, after moving to the acidic buffer
save           # Enter only after both CAPTURED messages
```

The comments above are explanatory: type only the command and its number.

Calibration formula (mV are the amplified module output measured by ADS1115):

```text
pH = neutralPh + (measuredMv - neutralMv)
                   * (acidPh - neutralPh) / (acidMv - neutralMv)
```

Saved calibration uses your measured voltage points, not the display-only
example points. No fixed -1.10 offset, automatic temperature compensation, or
automatic drift compensation is applied. Because this is pH-only, temperature
must be checked independently. Calibrate and validate near the use temperature.

## Commands

| Command | Effect |
| --- | --- |
| `help` | Print wiring and available commands |
| `status` | Show saved calibration, pending captures and storage status |
| `cal7 [value]` | Capture neutral buffer; default 7.00; allowed 6.5-7.5 |
| `cal4 [value]` | Capture acid buffer; default 4.00; allowed 3.5-4.5 |
| `setcal <pH7> <mV7> <pH4> <mV4>` | Load previously recorded points as pending (no live capture); still needs `save` |
| `save` | Validate and save both pending points, then activate them |
| `cancel` | Discard the capture and both pending points; keep saved calibration |
| `export` | Print the saved points and exact formula for later firmware transfer |
| `csv on` / `csv off` | Switch measurement rows to CSV / readable text |
| `reset YES` | Remove only this program's saved calibration; return to UNCALIBRATED |

Commands are case-sensitive and need LF, CR or CRLF. Storage uses the dedicated
NVS namespace `ph-only-v1`, key `calibration`. A successful `save` persists over
ordinary restarts. Flash erasure, partition changes, or `reset YES` can remove it.
The Rust firmware does **not** automatically load this calibration. Export/copy
the points deliberately later, and remove its temporary offset when doing so.
The filtering revision preserves the existing NVS record format and does not
erase saved points. Recalibrating both buffers with the new filter is recommended.

## Stability is a diagnostic, not an accuracy guarantee

Each independent block uses median absolute deviation (MAD) to identify samples
farther than `max(5 mV, 6 * MAD)` from its median. A block is eligible for capture
only when:

- at most **6 of 64** samples are rejected;
- retained standard deviation is at most **25 mV**;
- filtered mean differs from the raw mean by at most **10 mV**;
- every raw conversion passes the voltage and communication checks;
- no conversion starts more than **4 ms** after its scheduled time.

Raw peak-to-peak spread above **5 mV** now produces `RAW_NOISE_WARNING`, not an
automatic rejection. The previous 34-38 mV raw bursts could never pass the old
5 mV veto even when their averages were steady. Filtering allows stable averages
to be assessed without hiding the raw noise. Sustained noise, too many outliers
or excessive filtering instead produce explicit quality warnings and clear the
capture window. A numerical pH may still be printed for those noisy blocks;
read its quality flag, not just its conversion/calibration label.

Capture additionally requires at least 30 seconds plus 30 consecutive accepted
one-second blocks:

- spread of the filtered block means no more than **5 mV**;
- difference between the first-half and second-half means no more than **2 mV**;
- no intervening I2C failure, invalid voltage, or sampling gap above two seconds.

These are configurable engineering checks in `ph_filter.h` and
`ph_calibration.h`, not manufacturer-certified stability/accuracy limits. The
sampling schedule is not a verified 50/60 Hz notch filter. A 180-second timeout
discards the pending session, never automatically saving a drifting result. If
capture fails, check probe hydration/cleanliness, buffer temperature, grounding
and noise. Filtering cannot fix bias, a damaged probe or slow drift.

The configured voltage guard is 50-3000 mV for this DFRobot-style module. Acid
and neutral voltages must differ by at least 50 mV to reject identical or
near-identical points. Either polarity is accepted: the DFRobot reference
module's voltage rises toward acid, but the installed module was measured
falling (pH 6.86 at ~1750 mV, pH 4.00 at ~1648 mV). The display-only estimate
still uses the rising example points. The span check is only a plausibility
guard, not a measurement of the bare electrode's Nernst slope. A disconnected or bad electrode can still give
a plausible stable module output; software cannot guarantee probe connectivity.
Out-of-range pH is reported as an error, never clamped to 0 or 14.

Readable serial output includes converted pH, calibration status, filtered and
raw voltage, noise diagnostics and window stability. `csv on` prints this header:

```text
uptime_ms,raw_adc_mean,raw_millivolts,filtered_millivolts,ph,ph_estimate,status,raw_spread_mv,retained_sd_mv,rejected,filter_shift_mv,quality,window_spread_mv,window_half_drift_mv,stable
```

The `ph` field is filled **only with saved calibration**. Before calibration,
`ph_estimate` is filled instead, with status `ESTIMATE_UNCALIBRATED`. Invalid pH
leaves both fields empty, never zero or a stale last-good value. Unavailable
voltage diagnostics are `nan`. Lines starting with `#` are command/status
events; filter them out when importing CSV. Hardware error rows are retained.

## Checks without hardware

```powershell
g++ -std=c++11 -Wall -Wextra -Werror -static test/host/calibration_test.cpp -o test/host/calibration_test.exe
.\test\host\calibration_test.exe
g++ -std=c++11 -Wall -Wextra -Werror -static -DPH_USE_NATIVE_USB=1 -Iinclude -Itest/host/stubs test/host/firmware_test.cpp -o test/host/firmware_test.exe
.\test\host\firmware_test.exe
pio run -e ph-esp32s3
pio run -e ph-esp32s3-uart
```

The `-static` option avoids conflicting MinGW runtime DLLs on Windows. The first
test covers two-point math, estimates, 4.01 buffers, absence of the old image
offset, invalid inputs/records, synthetic 38 mV ripple, spikes, persistent noise,
real drift, fresh sessions, timeouts and millis rollover. These are synthetic
signals, not a replay of captured raw hardware samples.

The second test compiles the actual firmware against test-only hardware stubs.
It checks acquisition spacing, pH/CSV output, commands, incomplete calibration,
save failure, ADC faults, cancellation mid-block, timing gaps and micros rollover.
The stubs are not included in the PlatformIO firmware. Firmware builds check both
USB and UART console configurations. Real probe response, sampling jitter under
actual I2C/serial load, NVS persistence across power cycles and reference-meter
agreement still require hardware validation. No board is flashed by build/test.

## References

- [DFRobot SEN0161-V2: signal range and two-point calibration](https://wiki.dfrobot.com/sen0161-v2)
- [TI ADS1115 datasheet: configuration register, PGA and conversion timing](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
- [Espressif Preferences/NVS storage API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html)
- [PlatformIO ESP32-S3 board and configuration overrides](https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html)
