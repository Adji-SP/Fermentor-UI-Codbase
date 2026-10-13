# Original PlatformIO snapshot: IO-Fermentor-Kopi.zip

Restored all 11 non-generated project files from the
`IO-Fermentor-Kopi/Fermentor/` prefix in `IO-Fermentor-Kopi.zip`.
The original ZIP is unchanged. Text line endings may be normalized.
Generated `.pio` build/dependency caches are omitted and can be rebuilt.

This is the original sensor-only PT100/MAX31865 program (GPIO10/11/12/13),
ADS1115 A0 pH/A1 pressure, O2 0x70, SCD41 0x62, and SHT30 0x44 with SHT20
fallback. It does not implement Nextion motor control. The archived
`NEXTION_HMI.md` describes an interface not implemented by this source.

Do not flash this historical program onto the current DS18B20 wiring.
The current C++ project is two directories above (`../../platformio.ini`).
Open this snapshot directory in PlatformIO and use `pio run` to compile it
independently. Regenerate machine-specific VS Code settings for this location.
