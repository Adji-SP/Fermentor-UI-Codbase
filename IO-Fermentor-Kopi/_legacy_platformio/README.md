# Fermentor Kopi - complete PlatformIO/C++ project

Open this folder in PlatformIO to build the current Arduino/C++ firmware.
The active Rust project at [`../Fermentor`](../Fermentor/README.md) is unchanged.

For the complete original archive, including HMI resources, GPIO diagnostics,
hidden files and the matching old Nextion guide, open
[`restored/complete_original`](restored/complete_original/README.md).
The two earlier `restored/*_zip` folders below are selected PlatformIO extracts;
the complete recovery also includes the distinct ZIP nested inside the original.

## Available versions

| Location | Firmware version |
| --- | --- |
| This folder | Current DS18B20/SHT30/ADS1115/O2/SCD41, Nextion and motor control |
| [`restored/IO_Fermentor_Kopi_zip`](restored/IO_Fermentor_Kopi_zip/README.md) | Original sensor-only PT100 program from `IO-Fermentor-Kopi.zip` |
| [`restored/Fermentor_zip`](restored/Fermentor_zip/README.md) | Complete older all-in-one PT100/SHT20/Nextion program from `Fermentor.zip` |

The original versions use different sensor/pin settings. Keep them for recovery
and comparison; do not flash them onto the current DS18B20 wiring.
They are independent projects outside this folder's `src/` directory.

## Project files

- [`platformio.ini`](platformio.ini): board, USB console, GPIO12 and library dependencies.
- [`src/main.cpp`](src/main.cpp): sensors, five-second sampling and raw pH ADC diagnostics.
- [`src/hmi_motor.cpp`](src/hmi_motor.cpp) and [`src/hmi_motor.h`](src/hmi_motor.h): Nextion and motor control.
- [`src/nextion_protocol.h`](src/nextion_protocol.h): native Nextion packet decoder.
- [`NEXTION_HMI.md`](NEXTION_HMI.md): current C++ HMI wiring, components and events.
- `include/`, `lib/`, `test/`: original PlatformIO scaffolding.
- `.vscode/`: retained editor files; regenerate machine-specific settings as needed.
- `.gitignore`: generated PlatformIO cache/editor exclusions.
- [`RESTORE_MANIFEST.md`](RESTORE_MANIFEST.md): recovered file list and archive provenance.

The full Nextion screen build sheet and SVG/PNG resources remain in
[`../hmi-display`](../hmi-display/README.md). The GPIO diagnostic project
remains in [`../GPIO-Diagnostic`](../GPIO-Diagnostic/README.md).

The current firmware matches the finished single-page t0-t10 screen:
buttons 13/14/15 and slider 16. Follow
[`NEXTION_SINGLE_PAGE.md`](../hmi-display/NEXTION_SINGLE_PAGE.md) for the
Editor settings; the earlier three-page designs are historical assets.

## Current wiring

| Device | Connection |
| --- | --- |
| DS18B20 | DQ GPIO12, separate 1-Wire |
| Shared I2C | SDA GPIO8 / SCL GPIO9, 100 kHz through the existing level shifter |
| ADS1115 | 0x48; A0 pH, A1 pressure |
| DFRobot O2 | 0x70 |
| SCD41 / SHT30 | 0x62 / 0x44 |
| Nextion | UART1, ESP TX GPIO15 / RX GPIO16, 9600 baud |
| HW-390/BTS7960 | LPWM7, RPWM6, L_EN5, R_EN4 |

pH and pressure calibration values remain unchanged. `[pH/ADC]` reports raw
A0 counts, millivolts and calculated pH; values outside pH 0-14 are rejected.

## Compile without flashing

From this folder:

```powershell
pio run
```

If `pio` is not on PATH, use the installed executable:

```powershell
& 'C:/Users/Karma/.platformio/penv/Scripts/platformio.exe' run
```

Open this folder, not the Rust `Fermentor` folder, in PlatformIO.
Only upload after verifying the intended board/port. Restoration does not
flash hardware. The motor starts OFF and requires HMI commands.

The generated `.pio` cache is rebuildable from `platformio.ini`; it is not
missing source code and does not need to be copied from the ZIP.
Historical metadata is retained in each snapshot; read its README before use.
