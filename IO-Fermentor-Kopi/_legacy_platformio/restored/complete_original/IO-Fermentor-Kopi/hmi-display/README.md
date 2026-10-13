# Fermentor Kopi — three-page Nextion dashboard

Target: **Nextion NX3224F024**, 320 × 240 landscape, UART 9600 baud.

The legacy single-page dashboard shown in the screenshot is also available as a
[crisp 1280 × 960 preview](background_1280x960.png). It can be regenerated with
`render-assets.ps1 -UpscaleOnly -Scale 4`. This larger PNG is for viewing and
sharing; keep the 320 × 240 assets for the physical Nextion display.

The dashboard represents the five physical sensors in the wiring diagram:

1. PT100 through MAX31865
2. DFRobot oxygen
3. DFRobot pH through ADS1115
4. SCD41 (CO2, temperature and humidity)
5. SHT30 (chamber temperature and humidity)

The SHT30 uses the shared I2C bus at `0x44` or `0x45` and supplies the chamber
temperature and humidity readings on `page1`.

## Pages and assets

| Page | Contents | Clean SVG | Preview SVG | Nextion PNG background |
| --- | --- | --- | --- | --- |
| `page0` | PT100, O2, pH, SCD41 CO2 | [background](page0_process_background.svg) | [preview](page0_process_preview.svg) | [PNG](page0_process_background.png) |
| `page1` | SCD41 temperature/RH, SHT30 temperature/RH | [background](page1_environment_background.svg) | [preview](page1_environment_preview.svg) | [PNG](page1_environment_background.png) |
| `page2` | Motor state, direction and PWM control | [background](page2_control_background.svg) | [preview](page2_control_preview.svg) | [PNG](page2_control_background.png) |

The clean SVG and PNG backgrounds intentionally contain **no gauges, needles,
live readings, touch buttons or slider**. Those objects appear only in the SVG
and PNG previews. In the real Nextion project, add them as native components
over the clean PNG background.

`render-pages.ps1` regenerates the six PNG assets. SVG files are editable design
sources; Nextion Editor should be given the corresponding 320 × 240 PNG because
it does not use the SVG as a picture resource.

No `.HMI` or `.tft` is claimed as compiled or device-tested.

## Resources and fonts

Import the PNG backgrounds in this order:

| Picture ID | File |
| --- | --- |
| 0 | `page0_process_background.png` |
| 1 | `page1_environment_background.png` |
| 2 | `page2_control_background.png` |

Create bold Arial fonts: font 0 = 12 px for readings/PWM, font 1 = 15 px for
motor status, font 2 = 13 px for motor buttons, and font 3 = 10 px for navbar
buttons. Use centered alignment unless a row below says left aligned.

Every visible Text and Gauge component uses local scope, `sta=Crop Image`, and
`picc` matching its page background. Visible Text components use
`txt_maxl=20`. Gauge components start at `val=225`, use `pco=22234` and
`wid=2`. Coordinates are pixels.

## `page0` components

Set `page0.pic=0`.

| Name | Type | x,y,w,h | Font | Initial value |
| --- | --- | --- | --- | --- |
| `gPt` | Gauge | `57,48,48,48` | — | `225` |
| `tPt` | Text | `12,93,138,18` | 0 | `WAIT` |
| `gO2` | Gauge | `214,48,48,48` | — | `225` |
| `tO2` | Text | `169,93,138,18` | 0 | `WAIT` |
| `gPh` | Gauge | `57,136,48,48` | — | `225` |
| `tPh` | Text | `12,181,138,18` | 0 | `WAIT` |
| `gCo2` | Gauge | `214,136,48,48` | — | `225` |
| `tCo2` | Text | `169,181,138,18` | 0 | `WAIT` |

## `page1` components

Set `page1.pic=1`.

| Name | Type | x,y,w,h | Font | Initial value |
| --- | --- | --- | --- | --- |
| `gScT` | Gauge | `57,48,48,48` | — | `225` |
| `tScT` | Text | `12,93,138,18` | 0 | `WAIT` |
| `gScH` | Gauge | `214,48,48,48` | — | `225` |
| `tScH` | Text | `169,93,138,18` | 0 | `WAIT` |
| `gChT` | Gauge | `57,136,48,48` | — | `225` |
| `tChT` | Text | `12,181,138,18` | 0 | `WAIT` |
| `gChH` | Gauge | `214,136,48,48` | — | `225` |
| `tChH` | Text | `169,181,138,18` | 0 | `WAIT` |

## `page2` components

Set `page2.pic=2`.

| Name | Type | x,y,w,h | Font | Initial / colors |
| --- | --- | --- | --- | --- |
| `tMotor` | Text | `14,49,160,22` | 1 | `OFF`, `pco=36118`, left |
| `tDuty` | Text | `205,49,103,22` | 0 | `64/255 (25%)`, `pco=63455`, left |
| `bFwd` | Button | `10,96,96,39` | 2 | `ON FWD`, `bco=20151`, `bco2=13682`, `pco=pco2=197` |
| `bRev` | Button | `112,96,96,39` | 2 | `ON REV`, `bco=23769`, `bco2=15317`, `pco=pco2=197` |
| `bOff` | Button | `214,96,96,39` | 2 | `OFF / STOP`, `bco=60238`, `bco2=47723`, `pco=pco2=197` |
| `h0` | Slider | `16,174,288,23` | — | horizontal, min `0`, max `255`, val `64`, `bco=12940`, `pco=22234`, `wid=13`, `hig=23` |
| `tSpeed` | Text | `0,0,1,1` | 0 | `64`, `txt_maxl=3`; hidden after page load |

Disable “Send Component ID” on controls. The firmware reads newline-terminated
text commands instead of binary touch packets.

## Navbar and timer on every page

Add these local components to all three pages:

| Name | Type | x,y,w,h | Font | Text / setting |
| --- | --- | --- | --- | --- |
| `bNavProcess` | Button | `6,212,98,25` | 3 | `PROCESS` |
| `bNavEnv` | Button | `111,212,98,25` | 3 | `ENVIRONMENT` |
| `bNavControl` | Button | `216,212,98,25` | 3 | `CONTROL` |
| `tm0` | Timer | — | — | enabled, `tim=1000` ms |

Active navbar button: `bco=22234`, `pco=197`. Inactive: `bco=6253`,
`pco=44569`, `bco2=12940`.

Navbar Touch Release events:

```text
// bNavProcess
page page0

// bNavEnv
page page1

// bNavControl
page page2
```

`tm0` Timer Event on every page:

```text
prints "MOTOR:KEEPALIVE",0
printh 0A
```

## Page Postinitialize events

`page0`:

```text
prints "DASH:PAGE:0",0
printh 0A
```

`page1`:

```text
prints "DASH:PAGE:1",0
printh 0A
```

`page2`:

```text
vis tSpeed,0
prints "DASH:PAGE:2",0
printh 0A
```

## Motor Touch Release events

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

## Gauge mapping

| Gauge | Range |
| --- | --- |
| `gPt` | 0–100 °C |
| `gO2` | 0–25 % |
| `gPh` | 0–14 pH |
| `gCo2` | 0–5000 ppm |
| `gScT`, `gChT` | 0–60 °C |
| `gScH`, `gChH` | 0–100 % RH |

Firmware maps these ranges to the 270-degree gauge sweep. Status colors remain:
normal reading `63455`, normal needle `22234`, WAIT `62922`, OFFLINE `64715`,
error/fault `60238`, motor OFF `36118`, and motor running `20151`.

## Verification

1. Compile and load the Nextion page set, then power-cycle both devices.
2. Navigate through all three pages and confirm immediate value synchronization.
3. Verify PT100, O2, pH, SCD41 CO2/temperature/RH, and SHT30 temperature/RH.
4. Test FWD, STOP, REV and speed changes on the control page.
5. Set speed to zero and confirm a later nonzero slider value does not start the
   motor until a direction button is pressed.
6. Disconnect display UART while running and confirm the motor stops within
   three seconds without keepalive.

ESP32 GPIO43 TX connects through the PCB level shifter to Nextion RX; Nextion
TX returns to GPIO44 RX; ground is shared. Nextion UART is 9600 baud and USB
debug remains 115200 baud.
