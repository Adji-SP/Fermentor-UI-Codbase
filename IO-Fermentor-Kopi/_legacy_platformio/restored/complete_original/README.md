# Complete original PlatformIO archive recovery

This directory contains the entire original `IO-Fermentor-Kopi.zip`, including
hidden files, PlatformIO dependency/build caches, GPIO diagnostics, HMI assets,
and the original nested `Fermentor.zip`. That nested archive is also extracted
under `nested/`. These are historical snapshots with their original contents.

## Open the recovered files

| Location | Contents |
| --- | --- |
| [`IO-Fermentor-Kopi/Fermentor`](IO-Fermentor-Kopi/Fermentor/platformio.ini) | Sensor-only PlatformIO project from the outer ZIP |
| [`nested/Fermentor`](nested/Fermentor/platformio.ini) | Original all-in-one C++ sensor, Nextion and motor project |
| [`nested/Fermentor/NEXTION_HMI.md`](nested/Fermentor/NEXTION_HMI.md) | Matching original single-page Nextion component/event guide |
| [`IO-Fermentor-Kopi/hmi-display`](IO-Fermentor-Kopi/hmi-display/README.md) | Original SVG/PNG assets, render scripts and display documentation |
| [`IO-Fermentor-Kopi/GPIO-Diagnostic`](IO-Fermentor-Kopi/GPIO-Diagnostic/README.md) | Original standalone GPIO diagnostic project |

The historical all-in-one source is at
[`nested/Fermentor/src/main.cpp`](nested/Fermentor/src/main.cpp).
It uses PT100, SHT20 and Nextion GPIO43/44; its screen initialization command is
`DASH:SYNC`. The current DS18B20/SHT30 project remains at
[`../../README.md`](../../README.md), using GPIO15/16 and three-page events.
The outer archive's HMI assets represent a different historical interface;
preserving them does not make them compatible with every firmware snapshot.

## What the second crosscheck found

The original ZIP contains a 6,770,614-byte nested `Fermentor.zip`. This differs
from the 180,517,983-byte standalone ZIP with the same filename in the working
project. Their historical C++ `main.cpp` contents are identical, but their
Nextion guides differ: the nested original contains the matching 6,240-byte
C++ guide, while the standalone ZIP contains a later 3,008-byte Rust guide.
The first recovery selected PlatformIO files from the standalone ZIP and
therefore missed that matching guide and the complete original directory tree.

## Verification

- Outer archive: all 374 files match their ZIP entries byte for byte (SHA-256).
- Nested archive: all 120 files match their ZIP entries byte for byte (SHA-256).
- All 43 checked current firmware/HMI/diagnostic files remained unchanged.
- No `.HMI` or `.tft` file exists in either original archive.
- Nothing was flashed. Earlier builds already passed for the same recovered
  C++ source/configuration; these complete snapshots were left unchanged after
  extraction so their archived caches and sources remain verifiable.

Archive SHA-256 values:

```text
IO-Fermentor-Kopi.zip
BCB679A8095F70066492E7FA66432A61DE00628BB102712D0A322F5A776D7F80

Original nested Fermentor.zip
295015E76456DEEB56F894F54DCDFBC82677A214AD70532A5865CD5C505A4FBB
```
