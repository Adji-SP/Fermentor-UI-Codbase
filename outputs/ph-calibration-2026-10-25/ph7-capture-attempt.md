# pH 7 capture attempt - 2026-10-25 (Asia/Jakarta)

## Result: not accepted, not saved

The user reported that the probe was in pH 7 buffer. The buffer's certified
temperature-dependent value, temperature, preparation and probe condition were
not independently verified.

The ESP32 was detected on COM9, USB VID:PID 303A:1001. Following a restart/
USB reconnection, the device identified itself as the standalone PH ONLY
firmware (ADS1115 0x48 A0, SDA GPIO8, SCL GPIO9, 100 kHz).

`cal7 7.00` was sent once after identification. The firmware acknowledged:

```text
# Capturing pH 7.000. Keep probe in this buffer; wait for CAPTURED. Old saved calibration stays active.
```

During approximately 45 seconds after the command, observed ten-conversion
mean voltages were about **1729.6-1733.6 mV**. Individual conversion bursts
reported **34-38 mV peak-to-peak spread**. These are observed ranges from
serial output, not a saved calibration coefficient or a complete raw-ADC log.

Representative readings:

```text
pH: -- | UNCALIBRATED | 1733.600 mV | ADC=13868.80 | burst spread=38.000 mV | window=0/30 spread=nan mV drift=nan mV | SETTLING/NOISY
pH: -- | UNCALIBRATED | 1729.800 mV | ADC=13838.40 | burst spread=34.000 mV | window=0/30 spread=nan mV drift=nan mV | SETTLING/NOISY
```

The program's configured maximum conversion-burst spread is 5 mV. Every
observed burst exceeded it, clearing the calibration window to 0/30. No
`CAPTURED (RAM only)` confirmation was received. The rule is an engineering
guard in this program, not a manufacturer-specified accuracy limit. The cause
of the varying ADC readings has not been established by this attempt.

The pending capture was explicitly cancelled so it cannot later accept a
different liquid after the user changes buffers. The board confirmed:

```text
# Capture and both pending points cleared. Saved calibration unchanged.
# ACTIVE (saved): UNCALIBRATED
# NVS=OK | capture=IDLE | new pH7=MISSING | new pH4=MISSING | ready to save=NO
```

No `save`, `reset YES`, flash, or calibration-threshold change was performed.
No firmware source was edited during this capture attempt. A successful retry
and a separate pH 4/4.01 capture are still required before saving a two-point
calibration. The observed ~1.73 V must not be treated as a validated neutral
calibration constant yet.
