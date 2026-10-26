# pH 6.86 neutral-buffer capture - 2026-10-25

## Successful capture, RAM only

The user confirmed the buffer is pH **6.86**. The firmware was sent
`cal7 6.86` on COM9 after confirming PH ONLY / FILTER v2, idle capture,
and no pending neutral or acid points.

- Session: 04:46:33 to 04:47:16 Asia/Jakarta.
- USB identity: VID:PID 303A:1001, serial E0:72:A1:F3:AE:98.
- Reference pH supplied by the user: **6.860**.
- Neutral voltage reported by firmware: **1749.963 mV**.
- Capture-window spread at acceptance: **0.969 mV**.
- Half-window drift at acceptance: **+0.467 mV**.
- Confirmation: `CAPTURED (RAM only)`.
- Confirmed state: capture IDLE; neutral CAPTURED; acid MISSING; ready to save NO.
- Active saved calibration remains UNCALIBRATED.

The command name `cal7` selects the neutral point; its explicit argument
sets the reference to 6.86, not 7.00. No source-code change was necessary.

## Persistence and next step

The point is pending in ESP32 RAM, **not NVS**. No `save`, `reset`,
`cancel`, or flash operation was performed. The serial connection was closed
after verification. Keep the ESP32 powered; a reset, power interruption or
`cancel` discards pending points.

Rinse and move the probe to the acidic buffer, confirm its actual reference 
value, and capture that second point. Only after both captures should a complete
two-point calibration be saved. The serial pH stays an uncalibrated estimate
until that complete calibration is saved.

The stated buffer reference and temperature dependence were not independently
verified. Short-term stability passed the programmed checks; this does not
establish accuracy or rule out slower drift. Recheck fresh buffers after saving.

[Full serial transcript](ph686-capture-0447.log)
