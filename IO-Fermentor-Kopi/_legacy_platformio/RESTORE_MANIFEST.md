# PlatformIO recovery manifest

## Second crosscheck: complete original recovery

The initial 22-file recovery below was a selected PlatformIO extraction, not a
complete reconstruction of the original archive. It also overlooked the
distinct `Fermentor.zip` nested inside `IO-Fermentor-Kopi.zip`.

The full original archive is now extracted at
[`restored/complete_original/IO-Fermentor-Kopi`](restored/complete_original/README.md),
and its original nested backup at
[`restored/complete_original/nested/Fermentor`](restored/complete_original/nested/Fermentor/platformio.ini).
All files were included, including `.pio`, hidden editor files, HMI PNG/SVG
assets, render scripts, GPIO diagnostics and the nested ZIP itself.

The nested original's C++ source matches the previously recovered all-in-one
program, but its 6,240-byte Nextion guide is the matching single-page C++ guide
using `DASH:SYNC`. The standalone ZIP instead contains a later 3,008-byte Rust
guide. The matching original guide is now available at
[`NEXTION_HMI.md`](restored/complete_original/nested/Fermentor/NEXTION_HMI.md).

SHA-256 verification passed for all 374 outer-archive files and all 120
nested-archive files, with zero missing or altered files. The 43 checked current
firmware, HMI and diagnostic files were unchanged. No `.HMI` or `.tft` file
was present in either original archive. Nothing was flashed.

The following sections record the earlier selected recovery.

The current C++ sensor/HMI source and `platformio.ini` were not replaced.
The active Rust project and both source ZIPs are unchanged. Nothing was flashed.
The previous legacy README is preserved at
[`_restore_backups/README.before_restore.md`](_restore_backups/README.before_restore.md).

## Current PlatformIO project

Added the missing `.gitignore` and a C++-specific `NEXTION_HMI.md`, and
updated `README.md` with source locations, hardware and build instructions.
Existing `src/`, `platformio.ini`, VS Code settings and include/lib/test
scaffolding are unchanged.

## Recovered original snapshots

| Folder | Archive and selected prefix |
| --- | --- |
| `restored/IO_Fermentor_Kopi_zip` | `../../IO-Fermentor-Kopi.zip`: `IO-Fermentor-Kopi/Fermentor/` |
| `restored/Fermentor_zip` | `../Fermentor.zip`: `Fermentor/` (C++/PlatformIO files only) |

Each snapshot has these 11 recovered files:

- `.gitignore`
- `.vscode/c_cpp_properties.json`
- `.vscode/extensions.json`
- `.vscode/launch.json`
- `.vscode/settings.json`
- `NEXTION_HMI.md`
- `include/README`
- `lib/README`
- `platformio.ini`
- `src/main.cpp`
- `test/README`

Each also has a new README explaining its historical hardware assumptions.
Archived source/configuration content is preserved apart from text line-ending
normalization. Generated caches were not restored; the current project's
existing `.pio` cache remains. Nested snapshots are outside the current
project's `src/` and are not compiled into it.

Shared HMI assets and the GPIO diagnostic project remain in their existing
sibling directories.

## Verification

- All 22 recovered archive files match their ZIP entries after line-ending normalization.
- SHA-256 checks confirmed the active Rust project and the existing C++ source,
  configuration, editor files and scaffolding were unchanged. Only the previous
  legacy README changed among the 28 pre-existing files checked.
- PlatformIO release builds passed for the current project and both recovered
  snapshots (Espressif32 6.6.0 / Arduino 2.0.14).
- Historical builds produced the existing Arduino-core UART return warning;
  it did not prevent compilation. Hardware behavior was not tested.
- PlatformIO regenerated build/dependency caches as needed. No upload ran.
