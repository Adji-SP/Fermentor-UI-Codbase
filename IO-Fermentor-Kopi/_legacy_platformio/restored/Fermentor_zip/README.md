# Original PlatformIO snapshot: Fermentor.zip

Restored all 11 C++/PlatformIO project files from `Fermentor.zip`, including
the complete original `src/main.cpp`. The ZIP is unchanged; text line
endings may be normalized. Generated `.pio`/Rust `target` caches and the
Rust-specific files from that mixed archive are not duplicated here.

This historical all-in-one program uses PT100/MAX31865, O2 0x73, SCD41,
SHT20 enabled, SHT30 disabled, and Nextion TX GPIO43/RX GPIO44.
These settings do not match the current DS18B20/SHT30 hardware.
The archived `NEXTION_HMI.md` came from a later Rust version: keep it as
historical material, not as a verified interface for this old C++ program.

The second crosscheck recovered the matching original C++ Nextion guide from
the ZIP nested inside the original backup. Read
[`the original single-page guide`](../complete_original/nested/Fermentor/NEXTION_HMI.md)
for this source's `DASH:SYNC` events and GPIO43/44 wiring. The complete original
project and its caches are preserved alongside that guide.

Do not flash this historical program onto the current DS18B20 wiring.
The current C++ project is two directories above (`../../platformio.ini`).
Open this snapshot directory in PlatformIO and use `pio run` to compile it
independently. Regenerate machine-specific VS Code settings for this location.
