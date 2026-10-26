# Neutral-buffer voltage observation - reference pH pending

Captured on 2026-10-25, 04:43:44 to 04:44:15 Asia/Jakarta, from COM9.
USB identity: VID:PID 303A:1001, serial E0:72:A1:F3:AE:98.

The user described the liquid as "ph7 or 6.85". The exact certified buffer
value and temperature have not been confirmed. This record deliberately does
not assign either pH value to the observed voltage.

## Result

- 31 consecutive filtered serial readings spanning 30.00 seconds.
- Mean filtered voltage: **1744.8861 mV** (approximately **1.7449 V**).
- Minimum: 1744.469 mV; maximum: 1745.312 mV.
- Peak-to-peak spread of these means: **0.843 mV**.
- Last-half minus first-half mean: **+0.3228 mV** (15 readings in each half;
  center reading excluded from the half comparison).
- Reported within-block raw spread: 36-38 mV.
- All collected readings reported `RAW_NOISE_WARNING` and
  `window=30/30 ... SHORT_TERM_STABLE`.
- Displayed pH remained `ESTIMATE_UNCALIBRATED`, approximately 5.618-5.623.

Statistics were computed from the rounded filtered voltages printed by the
firmware, not from individual raw ADC conversions. The captured means pass
the current 5 mV spread / 2 mV half-drift observation criteria. Short-term
stability does not establish accuracy or rule out slower drift.

## What was and was not changed

This was a **read-only, host-side voltage capture**, not a firmware `cal7`
capture. No serial command was sent, no calibration point was placed into
firmware RAM, and no NVS calibration was saved or overwritten. No firmware
was flashed and no reset pulse was issued. The serial port was closed after
the observation.

Next: confirm the buffer label (and its reference value at the measurement
temperature), then perform a fresh neutral-point capture with that value.
An acidic-buffer capture and validation are also required before saving a
complete two-point calibration. Do not hardcode this unassigned observation
as pH 7.00 or pH 6.85.
