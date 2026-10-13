# Fermentor Kopi Nextion dashboard

The ESP32-S3 firmware sends live sensor readings and motor status to one
Nextion dashboard page. The same page lets the operator start, stop, reverse,
and adjust the motor. No `.HMI` project file was supplied, so create this page
in Nextion Editor using the component names and event code below. Component
names must match exactly because the firmware addresses them by name.

## Wiring and firmware defaults

The custom PCB's Nextion connector uses `9600` baud:

| Signal | ESP32-S3 pin | PCB connector |
| --- | --- | --- |
| ESP TX to Nextion RX | GPIO43 | RX5 |
| ESP RX from Nextion TX | GPIO44 | TX5 |
| Ground | GND | GND |

Use the PCB's UART level translation and a shared ground. The serial debug
console uses the ESP32-S3 native USB port at `115200` baud. Motor driver pins
remain LPWM GPIO7, RPWM GPIO6, L_EN GPIO5, and R_EN GPIO4.

The default firmware reads the sensors wired in the project diagram:

| Reading | Interface | Address / pins | Default |
| --- | --- | --- | --- |
| PT100 through MAX31865 | SPI | GPIO35/36/37/38 | On |
| DFRobot O2 | I2C | `0x73` | On |
| DFRobot pH through ADS1115 A0 | I2C | `0x48` | On |
| SCD41 CO2, temperature, humidity | I2C | `0x62` | On |
| SHT20 chamber temperature, humidity | I2C | `0x40` | On |
| Optional SHT30 temperature, humidity | I2C | `0x44` | Off |

All I2C sensors share SDA GPIO8 and SCL GPIO9 through the existing PCB wiring.
The logic level translators pass these signals through and have no I2C address.
`TEST_PT100` and `TEST_SHT20` are enabled, and `PH_INPUT_MODE` defaults to
`PH_INPUT_ADS1115` in `src/main.cpp`. The optional SHT30 remains disabled. A
disabled SHT30 appears as `DISABLED` on the dashboard.

## Build one dashboard page

Create a single page, for example `page0`. Arrange the sensor rows above the
motor controls. Adapt component sizes and fonts to your Nextion model. Each
value below is a **Text** component; put a separate static label beside it.
Set each value's maximum text length (`txt_maxl`) to at least 20 characters.

| Label on screen | Text component name | Example value |
| --- | --- | --- |
| PT100 temperature | `tPt` | `25.42 C` |
| Oxygen | `tO2` | `20.90 %` |
| pH | `tPh` | `6.85` |
| CO2 | `tCo2` | `612 ppm` |
| SCD41 temperature | `tScT` | `25.31 C` |
| SCD41 humidity | `tScH` | `60.52 %` |
| SHT20 chamber temperature | `tChT` | `24.98 C` |
| SHT20 chamber humidity | `tChH` | `68.12 %` |
| Optional SHT30 temperature | `tS3T` | `DISABLED` |
| Optional SHT30 humidity | `tS3H` | `DISABLED` |
| Motor state | `tMotor` | `OFF`, `STARTING`, `ON FWD`, or `ON REV` |
| Motor PWM setting | `tDuty` | `64/255 (25%)` |

Suggested visual grouping:

```text
FERMENTOR KOPI
PT100       [tPt]       O2         [tO2]       pH    [tPh]
CO2         [tCo2]      SCD41 T/RH [tScT] [tScH]
CHAMBER     [tChT] [tChH]         SHT30 [tS3T] [tS3H]
MOTOR       [tMotor]    PWM [tDuty]
[ON FWD] [ON REV] [OFF / STOP]
SPEED 0 ─────────────[h0]───────────── 255
```

The firmware updates readings about once per second. SCD41 provides a new CO2
measurement about every five seconds; its last reading remains on the screen
between updates. `WAIT` means no first reading yet, `ERROR` means a read failed,
and `OFFLINE` means the device did not respond. The display receives standard
Nextion assignments such as `tPt.txt="25.42 C"`, each terminated by three
`0xFF` bytes.

## Motor controls on the same page

Add three buttons, one slider, one hidden Text component, and one Timer:

| Component | Name | Setting |
| --- | --- | --- |
| ON Forward button | `bFwd` | Label `ON FWD` |
| ON Reverse button | `bRev` | Label `ON REV` |
| OFF button | `bOff` | Label `OFF / STOP` |
| Speed slider | `h0` | Minimum `0`, maximum `255`, initial value `64` |
| Hidden text for slider conversion | `tSpeed` | `txt_maxl` at least `3` |
| Keepalive timer | `tm0` | Period `1000` ms, enabled |

Put these exact instructions in the components' **Touch Release Event** fields.
The labels `FWD` and `REV` select LPWM and RPWM; actual shaft direction depends
on motor lead wiring.

`bFwd`:

```text
prints "MOTOR:FWD",0
printh 0A
```

`bRev`:

```text
prints "MOTOR:REV",0
printh 0A
```

`bOff`:

```text
prints "MOTOR:STOP",0
printh 0A
```

`h0`:

```text
cov h0.val,tSpeed.txt,0
prints "MOTOR:SPEED:",0
prints tSpeed.txt,0
printh 0A
```

The slider sets PWM duty from `0` to `255`. Moving it to `0` stops the motor.
Moving it to a nonzero value changes the speed but does not start a stopped
motor; press ON Forward or ON Reverse to start. The default duty is `64/255`
(about 25%). `tDuty` displays the duty currently accepted by the ESP32.

Put this in `tm0`'s **Timer Event**:

```text
prints "MOTOR:KEEPALIVE",0
printh 0A
```

Put this in the dashboard page's **Postinitialize Event** so the page requests
the current readings and synchronizes `h0` when the display starts or reloads:

```text
prints "DASH:SYNC",0
printh 0A
```

The motor starts **OFF** after ESP32 reset. It stops if no run command or
keepalive arrives for three seconds, including when this page is left and its
timer stops. The firmware removes both PWM outputs for 250 ms before reversing.
The `tMotor` field reports the actual firmware state, including a timeout stop;
do not treat the button appearance alone as motor state.

## Load and check the screen

1. Compile the Nextion page in Nextion Editor and load it onto the display.
2. Connect the display to the PCB connector and power the ESP32-S3 and display.
3. Check that the dashboard fills with readings or clear `WAIT`/`OFFLINE`/`ERROR`
   statuses. Confirm that `h0` starts at the displayed PWM setting.
4. With the motor driver connected to its intended external motor supply, use
   ON Forward, OFF / STOP, then ON Reverse. Check `tMotor` and `tDuty` as you
   move the slider.
5. Disconnect the display UART while running: the firmware should report a
   heartbeat timeout on USB serial and turn the motor outputs off within three
   seconds.

The firmware build can be checked with `platformio run` in this folder. The
Nextion screen itself must also be compiled and loaded; uploading only the
ESP32 firmware does not create the Nextion page.
