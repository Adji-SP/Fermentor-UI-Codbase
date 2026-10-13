#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

// ============================================================
// FERMENTOR KOPI - CUSTOM PCB GPIO / BUS DIAGNOSTIC
//
// This is a separate diagnostic firmware. It does not replace the main
// fermentor application. The pin map follows:
//   "WhatsApp Image 2026-08-13 at 14.12.21 (1).jpeg"
// ============================================================

constexpr uint32_t CONSOLE_BAUD = 115200;

// Custom PCB I2C nets (A side / 3.3 V side of U3 TXS0108E).
constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;

// Custom PCB SPI nets (A side / 3.3 V side of U3 TXS0108E).
constexpr uint8_t PIN_PCB_SPI_MOSI = 35;  // SDI_3.3V
constexpr uint8_t PIN_PCB_SPI_SCK  = 36;  // CLK_3.3V
constexpr uint8_t PIN_PCB_SPI_MISO = 37;  // SDO_3.3V
constexpr uint8_t PIN_PCB_SPI_CS   = 38;  // CS_3.3V

// Old loose-wire diagram, available only as a manual comparison test.
constexpr uint8_t PIN_OLD_SPI_MOSI = 11;
constexpr uint8_t PIN_OLD_SPI_SCK  = 12;
constexpr uint8_t PIN_OLD_SPI_MISO = 13;
constexpr uint8_t PIN_OLD_SPI_CS   = 10;

constexpr uint8_t PIN_RELAY_CH1 = 16;
constexpr uint8_t PIN_RELAY_CH2 = 17;
constexpr bool RELAY_ACTIVE_LOW = true;

// Native USB CDC is used for this diagnostic console, leaving UART0 pins
// available for an optional external loopback test.
constexpr uint8_t PIN_UART_TX = 43;  // ESP TX -> translated RX5 connector net
constexpr uint8_t PIN_UART_RX = 44;  // ESP RX <- translated TX5 connector net
constexpr uint32_t UART_TEST_BAUD = 9600;

constexpr uint8_t MAX_REG_CONFIG = 0x00;
constexpr uint8_t MAX_REG_FAULT  = 0x07;
constexpr uint8_t MAX_CONFIG_3WIRE = 0xD1;

SPISettings max31865SpiSettings(200000, MSBFIRST, SPI_MODE1);

struct PinDescription {
    uint8_t gpio;
    const char *net;
    const char *purpose;
    const char *safety;
    bool readable;
};

// GPIOs explicitly shown or exposed in the supplied custom-PCB schematic.
const PinDescription PIN_MAP[] = {
    {0,  "X8-1",     "Expansion header",          "STRAP/read only", true},
    {1,  "X8-2",     "Expansion header",          "loopback-safe",   true},
    {2,  "X8-3",     "Expansion header",          "loopback-safe",   true},
    {3,  "X8-4",     "Expansion header",          "STRAP/read only", true},
    {4,  "X8-5",     "Expansion header",          "loopback-safe",   true},
    {5,  "X8-6",     "Expansion header",          "loopback-safe",   true},
    {6,  "X9-1",     "Expansion header",          "loopback-safe",   true},
    {7,  "X9-2",     "Expansion header",          "loopback-safe",   true},
    {8,  "SDA_3.3V", "I2C data through U3",       "BUS",             true},
    {9,  "SCL_3.3V", "I2C clock through U3",      "BUS",             true},
    {10, "X9-3",     "Expansion / old SPI CS",    "loopback-safe",   true},
    {11, "X9-4",     "Expansion / old SPI MOSI",  "loopback-safe",   true},
    {12, "X9-5",     "Expansion / old SPI SCK",   "loopback-safe",   true},
    {13, "X9-6",     "Expansion / old SPI MISO",  "loopback-safe",   true},
    {14, "X10-1",    "Expansion header",          "loopback-safe",   true},
    {15, "X10-2",    "Expansion header",          "loopback-safe",   true},
    {16, "CH1",      "Relay channel 1",           "OUTPUT/danger",   true},
    {17, "CH2",      "Relay channel 2",           "OUTPUT/danger",   true},
    {18, "J1-11",    "ESP32 header",              "loopback-safe",   true},
    {19, "USB_D-",   "Native USB",                "DO NOT TOUCH",    false},
    {20, "USB_D+",   "Native USB",                "DO NOT TOUCH",    false},
    {21, "J3-18",    "ESP32 header",              "loopback-safe",   true},
    {35, "SDI_3.3V", "MAX31865 MOSI through U3",  "BUS",             true},
    {36, "CLK_3.3V", "MAX31865 SCK through U3",   "BUS",             true},
    {37, "SDO_3.3V", "MAX31865 MISO through U3",  "BUS",             true},
    {38, "CS_3.3V",  "MAX31865 CS through U3",    "BUS",             true},
    {39, "X10-3",    "Expansion header",          "loopback-safe",   true},
    {40, "X10-4",    "Expansion header",          "loopback-safe",   true},
    {41, "X10-5",    "Expansion header",          "loopback-safe",   true},
    {42, "X10-6",    "Expansion header",          "loopback-safe",   true},
    {43, "RX/RX5",   "UART TX to Nextion RX",     "UART",            true},
    {44, "TX/TX5",   "UART RX from Nextion TX",   "UART",            true},
    {45, "J3-15",    "ESP32 header",              "STRAP/read only", true},
    {46, "J1-14",    "ESP32 input-only/strap",    "STRAP/read only", true},
    {47, "J3-17",    "ESP32 header",              "loopback-safe",   true},
    {48, "J3-16",    "ESP32 header",              "loopback-safe",   true},
};

const uint8_t SAFE_LOOPBACK_PINS[] = {
    1, 2, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 18, 21, 39, 40, 41, 42, 47, 48
};

struct SpiProfile {
    const char *name;
    uint8_t sck;
    uint8_t mosi;
    uint8_t miso;
    uint8_t cs;
};

const SpiProfile PCB_SPI = {
    "CUSTOM PCB GPIO35/36/37/38",
    PIN_PCB_SPI_SCK, PIN_PCB_SPI_MOSI, PIN_PCB_SPI_MISO, PIN_PCB_SPI_CS
};

const SpiProfile OLD_SPI = {
    "OLD WIRING GPIO11/12/13/10",
    PIN_OLD_SPI_SCK, PIN_OLD_SPI_MOSI, PIN_OLD_SPI_MISO, PIN_OLD_SPI_CS
};

uint8_t lastI2cCount = 0;
bool lastPcbSpiOk = false;

void separator() {
    Serial.println("------------------------------------------------------------");
}

String readConsoleLine() {
    while (!Serial.available()) delay(10);
    String value = Serial.readStringUntil('\n');
    value.trim();
    return value;
}

long promptNumber(const char *prompt) {
    Serial.print(prompt);
    String value = readConsoleLine();
    return value.toInt();
}

bool isSafeLoopbackPin(uint8_t pin) {
    for (uint8_t candidate : SAFE_LOOPBACK_PINS) {
        if (candidate == pin) return true;
    }
    return false;
}

const char *i2cDeviceName(uint8_t address) {
    switch (address) {
        case 0x44: return "SHT30";
        case 0x45: return "SHT30 (alternate address)";
        case 0x48: return "ADS1115";
        case 0x62: return "SCD41";
        case 0x73: return "DFRobot O2";
        default:   return "unknown";
    }
}

void prepareSafePins() {
    // Never drive a relay automatically. Input mode leaves the external relay
    // module in control of its own pull resistor until an explicit pulse test.
    pinMode(PIN_RELAY_CH1, INPUT);
    pinMode(PIN_RELAY_CH2, INPUT);

    for (uint8_t pin : SAFE_LOOPBACK_PINS) pinMode(pin, INPUT);
    pinMode(0, INPUT);
    pinMode(3, INPUT);
    pinMode(45, INPUT);
    pinMode(46, INPUT);
}

void printPinMapAndLevels() {
    separator();
    Serial.println("GPIO MAP + CURRENT DIGITAL LEVEL");
    Serial.println("NOTE: a floating unused pin can randomly read 0 or 1; this is not");
    Serial.println("      a continuity test. Use menu 5 with a physical jumper.");
    Serial.println();
    Serial.println("GPIO | LEVEL | NET       | PURPOSE                         | SAFETY");
    for (const PinDescription &item : PIN_MAP) {
        Serial.printf("%4u | ", item.gpio);
        if (item.readable) Serial.printf("  %u   | ", digitalRead(item.gpio));
        else Serial.print(" SKIP | ");
        Serial.printf("%-9s | %-31s | %s\n", item.net, item.purpose, item.safety);
    }
}

uint8_t scanI2cBus() {
    separator();
    Serial.printf("I2C SCAN: SDA=GPIO%u, SCL=GPIO%u, 50 kHz\n", PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.end();
    delay(10);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(50000);
    Wire.setTimeOut(50);

    Serial.printf("A-side line levels before scan: SDA=%u SCL=%u\n",
                  digitalRead(PIN_I2C_SDA), digitalRead(PIN_I2C_SCL));

    uint8_t count = 0;
    for (uint8_t address = 1; address < 0x7F; ++address) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        if (error == 0) {
            Serial.printf("  FOUND 0x%02X : %s\n", address, i2cDeviceName(address));
            ++count;
        } else if (error == 4) {
            Serial.printf("  BUS ERROR while probing 0x%02X\n", address);
        }
    }

    if (count == 0) {
        Serial.println("RESULT: no I2C devices acknowledged.");
    } else {
        Serial.printf("RESULT: %u I2C device(s) found.\n", count);
    }
    lastI2cCount = count;
    return count;
}

uint8_t maxReadRegister(uint8_t cs, uint8_t address) {
    SPI.beginTransaction(max31865SpiSettings);
    digitalWrite(cs, LOW);
    delayMicroseconds(10);
    SPI.transfer(address & 0x7F);
    uint8_t value = SPI.transfer(0x00);
    delayMicroseconds(10);
    digitalWrite(cs, HIGH);
    SPI.endTransaction();
    return value;
}

void maxWriteRegister(uint8_t cs, uint8_t address, uint8_t value) {
    SPI.beginTransaction(max31865SpiSettings);
    digitalWrite(cs, LOW);
    delayMicroseconds(10);
    SPI.transfer(address | 0x80);
    SPI.transfer(value);
    delayMicroseconds(10);
    digitalWrite(cs, HIGH);
    SPI.endTransaction();
}

bool probeMax31865(const SpiProfile &profile) {
    separator();
    Serial.printf("MAX31865 SPI PROBE: %s\n", profile.name);
    Serial.printf("SCK=%u MOSI/SDI=%u MISO/SDO=%u CS=%u\n",
                  profile.sck, profile.mosi, profile.miso, profile.cs);

    SPI.end();
    pinMode(profile.cs, OUTPUT);
    digitalWrite(profile.cs, HIGH);
    SPI.begin(profile.sck, profile.miso, profile.mosi, profile.cs);
    delay(20);

    uint8_t before = maxReadRegister(profile.cs, MAX_REG_CONFIG);
    maxWriteRegister(profile.cs, MAX_REG_CONFIG, MAX_CONFIG_3WIRE);
    delay(100);
    uint8_t after = maxReadRegister(profile.cs, MAX_REG_CONFIG);

    Serial.printf("Config before write : 0x%02X\n", before);
    Serial.printf("Wrote config        : 0x%02X\n", MAX_CONFIG_3WIRE);
    Serial.printf("Config readback     : 0x%02X\n", after);
    Serial.print("Registers 0x00..0x07:");
    for (uint8_t address = 0; address <= MAX_REG_FAULT; ++address) {
        Serial.printf(" %02X", maxReadRegister(profile.cs, address));
    }
    Serial.println();

    bool ok = (after == MAX_CONFIG_3WIRE);
    Serial.println(ok ? "RESULT: MAX31865 SPI communication PASS."
                      : "RESULT: MAX31865 SPI communication FAIL.");

    SPI.end();
    pinMode(profile.sck, INPUT);
    pinMode(profile.mosi, INPUT);
    pinMode(profile.miso, INPUT);
    pinMode(profile.cs, INPUT);

    if (&profile == &PCB_SPI) lastPcbSpiOk = ok;
    return ok;
}

void printBusDiagnosis() {
    separator();
    Serial.println("COMBINED BUS DIAGNOSIS");
    Serial.printf("Custom-PCB MAX31865 SPI : %s\n", lastPcbSpiOk ? "PASS" : "FAIL");
    Serial.printf("I2C acknowledged devices: %u\n", lastI2cCount);

    if (!lastPcbSpiOk && lastI2cCount == 0) {
        Serial.println("LIKELY COMMON CAUSE:");
        Serial.println("  - TXS0108E U3 OE is LOW, or");
        Serial.println("  - U3 VCCA/VCCB/GND is missing, or");
        Serial.println("  - U3 has a solder/continuity fault.");
        Serial.println("Measure U3: pin2 VCCA=3.3V, pin19 VCCB=5V,");
        Serial.println("            pin10 OE=3.3V, pin11 GND=0V.");
    } else if (!lastPcbSpiOk) {
        Serial.println("I2C crosses U3, so OE is probably enabled.");
        Serial.println("Check MAX31865 power, header order, GPIO35-38 routing, and soldering.");
    } else if (lastI2cCount == 0) {
        Serial.println("SPI crosses U3, so OE is probably enabled.");
        Serial.println("Check SDA/SCL continuity, I2C pull-ups, sensor power, and voltage domains.");
    } else {
        Serial.println("Core translated SPI and I2C paths are operational.");
    }
}

void runCoreDiagnostics() {
    printPinMapAndLevels();
    scanI2cBus();
    probeMax31865(PCB_SPI);
    printBusDiagnosis();
}

void runGpioLoopback() {
    separator();
    Serial.println("GPIO PAIR LOOPBACK TEST");
    Serial.println("Allowed GPIOs: 1,2,4,5,6,7,10-15,18,21,39-42,47,48");
    Serial.println("Disconnect other devices, then jumper OUTPUT GPIO directly to INPUT GPIO.");

    long outputValue = promptNumber("Output GPIO: ");
    long inputValue = promptNumber("Input GPIO : ");
    if (outputValue == inputValue || !isSafeLoopbackPin(outputValue) ||
        !isSafeLoopbackPin(inputValue)) {
        Serial.println("REJECTED: use two different GPIOs from the allowed list.");
        return;
    }

    uint8_t outputPin = static_cast<uint8_t>(outputValue);
    uint8_t inputPin = static_cast<uint8_t>(inputValue);
    Serial.printf("Install jumper GPIO%u -> GPIO%u, then press ENTER.\n", outputPin, inputPin);
    readConsoleLine();

    pinMode(inputPin, INPUT_PULLDOWN);
    digitalWrite(outputPin, LOW);
    pinMode(outputPin, OUTPUT);
    delay(20);

    uint8_t matches = 0;
    constexpr uint8_t transitions = 10;
    for (uint8_t index = 0; index < transitions; ++index) {
        uint8_t expected = index & 1;
        digitalWrite(outputPin, expected ? HIGH : LOW);
        delay(10);
        uint8_t observed = digitalRead(inputPin);
        if (observed == expected) ++matches;
        Serial.printf("  drive=%u read=%u %s\n", expected, observed,
                      observed == expected ? "OK" : "MISMATCH");
    }

    digitalWrite(outputPin, LOW);
    pinMode(outputPin, INPUT);
    pinMode(inputPin, INPUT);
    Serial.printf("RESULT: %u/%u transitions matched -> %s\n", matches, transitions,
                  matches == transitions ? "PASS" : "FAIL");
}

void runUartLoopback() {
    separator();
    Serial.println("UART / TXS0108E LOOPBACK TEST");
    Serial.println("Disconnect the Nextion display first.");
    Serial.println("Jumper the translated connector nets RX5 directly to TX5.");
    Serial.println("This diagnostic console must use the ESP32-S3 native USB port.");
    Serial.print("Type LOOP to continue: ");
    if (readConsoleLine() != "LOOP") {
        Serial.println("Cancelled.");
        return;
    }

    Serial1.end();
    Serial1.begin(UART_TEST_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    while (Serial1.available()) Serial1.read();

    const uint8_t pattern[] = {0x55, 0xAA, 0x00, 0xFF, 0x31, 0x7E, 0xC3, 0x5A};
    Serial1.write(pattern, sizeof(pattern));
    Serial1.flush();
    delay(100);

    uint8_t received[sizeof(pattern)] = {};
    size_t count = 0;
    while (Serial1.available() && count < sizeof(received)) {
        received[count++] = Serial1.read();
    }

    bool match = count == sizeof(pattern);
    for (size_t i = 0; i < count && match; ++i) match = received[i] == pattern[i];
    Serial.printf("Received %u/%u bytes -> %s\n", static_cast<unsigned>(count),
                  static_cast<unsigned>(sizeof(pattern)), match ? "PASS" : "FAIL");
    if (!match) Serial.println("Check OE, UART translation channels, connector, and jumper.");

    Serial1.end();
    pinMode(PIN_UART_TX, INPUT);
    pinMode(PIN_UART_RX, INPUT);
}

void runRelayPulse() {
    separator();
    Serial.println("DANGEROUS: RELAY OUTPUT PULSE TEST");
    Serial.println("Disconnect the solenoid, mains wiring, and other loads first.");
    Serial.println("The default assumes an active-LOW HL-52S relay input.");
    long channel = promptNumber("Channel (1 or 2, anything else cancels): ");
    if (channel != 1 && channel != 2) {
        Serial.println("Cancelled.");
        return;
    }
    Serial.printf("Type PULSE CH%ld to confirm: ", channel);
    String expected = "PULSE CH" + String(channel);
    if (readConsoleLine() != expected) {
        Serial.println("Cancelled.");
        return;
    }

    uint8_t pin = channel == 1 ? PIN_RELAY_CH1 : PIN_RELAY_CH2;
    uint8_t inactive = RELAY_ACTIVE_LOW ? HIGH : LOW;
    uint8_t active = RELAY_ACTIVE_LOW ? LOW : HIGH;
    digitalWrite(pin, inactive);
    pinMode(pin, OUTPUT);
    delay(200);
    Serial.printf("GPIO%u ACTIVE for 300 ms...\n", pin);
    digitalWrite(pin, active);
    delay(300);
    digitalWrite(pin, inactive);
    delay(200);
    pinMode(pin, INPUT);
    Serial.println("Pulse complete; GPIO returned to input mode.");
}

void printPowerChecklist() {
    separator();
    Serial.println("POWER / OE MULTIMETER CHECKLIST (cannot be measured by firmware)");
    Serial.println("  U3 pin  2 VCCA : expected 3.3 V to GND");
    Serial.println("  U3 pin 19 VCCB : expected 5.0 V to GND");
    Serial.println("  U3 pin 10 OE   : expected 3.3 V to GND (enabled)");
    Serial.println("  U3 pin 11 GND  : expected 0 V");
    Serial.println("  Every sensor VCC/GND must be checked at its connector.");
    Serial.println("NOTE: TXS0108E OE is not routed to an ESP32 GPIO, so software cannot read it.");
}

void printMenu() {
    Serial.println();
    Serial.println("================ DIAGNOSTIC MENU ================");
    Serial.println("1. Run all non-destructive core diagnostics");
    Serial.println("2. Print GPIO map and current input levels");
    Serial.println("3. Scan I2C bus (GPIO8/9)");
    Serial.println("4. Probe MAX31865 on custom-PCB SPI (GPIO35-38)");
    Serial.println("5. GPIO pair loopback (requires jumper)");
    Serial.println("6. UART/Nextion connector loopback (requires jumper)");
    Serial.println("7. Probe MAX31865 using old wiring GPIO10-13");
    Serial.println("8. Pulse relay output (danger; explicit confirmation)");
    Serial.println("9. Print power/OE multimeter checklist");
    Serial.println("0. Show this menu");
    Serial.print("Select: ");
}

void setup() {
    Serial.begin(CONSOLE_BAUD);
    uint32_t started = millis();
    while (!Serial && millis() - started < 5000) delay(10);
    Serial.setTimeout(60000);

    prepareSafePins();
    Serial.println();
    Serial.println("============================================================");
    Serial.println(" FERMENTOR KOPI - CUSTOM PCB GPIO / BUS DIAGNOSTIC");
    Serial.println("============================================================");
    Serial.println("Non-destructive core tests will run once at startup.");
    runCoreDiagnostics();
    printMenu();
}

void loop() {
    if (!Serial.available()) {
        delay(10);
        return;
    }

    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() == 0) {
        printMenu();
        return;
    }

    switch (command[0]) {
        case '1': runCoreDiagnostics(); break;
        case '2': printPinMapAndLevels(); break;
        case '3': scanI2cBus(); break;
        case '4': probeMax31865(PCB_SPI); printBusDiagnosis(); break;
        case '5': runGpioLoopback(); break;
        case '6': runUartLoopback(); break;
        case '7':
            Serial.println("WARNING: old SPI test toggles expansion GPIO10-13.");
            Serial.print("Type OLD to continue: ");
            if (readConsoleLine() == "OLD") probeMax31865(OLD_SPI);
            else Serial.println("Cancelled.");
            break;
        case '8': runRelayPulse(); break;
        case '9': printPowerChecklist(); break;
        case '0': break;
        default: Serial.println("Unknown selection."); break;
    }
    printMenu();
}
