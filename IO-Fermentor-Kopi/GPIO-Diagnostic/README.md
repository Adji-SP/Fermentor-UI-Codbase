# Fermentor Kopi GPIO Diagnostic

Separate PlatformIO firmware for the custom PCB shown in
`WhatsApp Image 2026-08-13 at 14.12.21 (1).jpeg`. It does not replace the main
fermentor firmware.

## What it tests

- Prints the schematic GPIO map and current digital levels.
- Scans I2C on GPIO8/GPIO9 and identifies expected sensor addresses.
- Writes and reads the MAX31865 configuration register over the custom-PCB SPI
  mapping on GPIO35-GPIO38.
- Combines SPI and I2C results to identify a likely disabled/unpowered
  TXS0108E.
- Tests expansion GPIO continuity in pairs using a physical loopback jumper.
- Offers an optional UART/Nextion connector loopback.
- Offers an optional relay pulse, guarded by an explicit warning and typed
  confirmation.

## Build and upload

Open this folder as a PlatformIO project, or run:

```powershell
C:\Users\Karma\.platformio\penv\Scripts\pio.exe run
C:\Users\Karma\.platformio\penv\Scripts\pio.exe run -t upload
C:\Users\Karma\.platformio\penv\Scripts\pio.exe device monitor -b 115200
```

The diagnostic console uses native USB CDC so GPIO43/GPIO44 can be tested as
the Nextion UART. Connect the computer to the ESP32-S3 native USB connector.

## Expected addresses

| Address | Device |
|---|---|
| `0x44` or `0x45` | SHT30 (depends on ADDR pin) |
| `0x48` | ADS1115 |
| `0x62` | SCD41 |
| `0x73` | DFRobot O2 |

Only physically installed and powered devices will appear.

## SPI pin map from the custom PCB schematic

| ESP32 GPIO | U3/MAX31865 net |
|---|---|
| GPIO35 | SDI / MOSI |
| GPIO36 | CLK / SCK |
| GPIO37 | SDO / MISO |
| GPIO38 | CS |

A successful MAX31865 probe must read back configuration `0xD1`.

## GPIO loopback procedure

Firmware cannot determine whether an unused GPIO trace is continuous merely
by reading a floating pin. Use menu option 5:

1. Select two safe expansion GPIOs.
2. Disconnect other devices from both pins.
3. Install a jumper directly between the selected pins.
4. Press ENTER.
5. The output toggles ten times and the input must match all transitions.

Strapping pins GPIO0, GPIO3, GPIO45, and GPIO46, USB GPIO19/GPIO20, bus pins,
UART pins, and relay outputs are excluded from generic loopback testing.

## Important safety notes

- Startup tests never drive relay GPIO16/GPIO17.
- Before menu option 8, physically disconnect solenoids, mains wiring, and
  other relay loads.
- Disconnect the Nextion display before UART loopback and jumper RX5 to TX5.
- Menu option 7 toggles old-wiring GPIO10-GPIO13 and must not be used if those
  expansion pins are attached to another circuit.
- GPIO scanning cannot measure 3.3 V, 5 V, GND, or TXS0108E OE. Use a
  multimeter for those rails.

Required U3 measurements:

```text
pin 2  VCCA = 3.3 V
pin 19 VCCB = 5.0 V
pin 10 OE   = 3.3 V
pin 11 GND  = 0 V
```

If custom SPI fails and I2C finds zero devices simultaneously, the most likely
common fault is U3 OE LOW, missing U3 power, or a solder/continuity problem.

## ADS1115 voltage-domain warning

The supplied schematic appears to power ADS1115 from 3.3 V while connecting
its SDA/SCL to the TXS0108E 5 V side. Before leaving U3 enabled permanently,
verify the final PCB routing and module requirements. A 3.3 V-powered ADS1115
should use a 3.3 V I2C domain, or the module should be powered appropriately
for the 5 V side.
