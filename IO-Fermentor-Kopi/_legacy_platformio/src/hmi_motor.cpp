#include "hmi_motor.h"
#include "nextion_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint8_t NEXTION_RX = 16;
constexpr uint8_t NEXTION_TX = 15;
constexpr uint32_t NEXTION_BAUD = 9600;
constexpr uint8_t MOTOR_LPWM = 7;
constexpr uint8_t MOTOR_RPWM = 6;
constexpr uint8_t MOTOR_L_EN = 5;
constexpr uint8_t MOTOR_R_EN = 4;
constexpr uint32_t MOTOR_TIMEOUT_MS = 3000;
constexpr uint32_t REVERSE_PAUSE_MS = 250;
constexpr uint32_t PAGE_PROBE_MS = 1000;
constexpr uint32_t SLIDER_REPLY_MS = 500;
constexpr uint8_t BUTTON_FWD = 13;
constexpr uint8_t BUTTON_REV = 14;
constexpr uint8_t BUTTON_STOP = 15;
constexpr uint8_t SLIDER = 16;

enum class Direction : uint8_t { STOP, FWD, REV };
Direction direction = Direction::STOP;
Direction pendingDirection = Direction::STOP;
uint8_t duty = 64;
uint32_t lastControlMs = 0;
uint32_t reversalStartedMs = 0;
int16_t activePage = -1;
uint32_t lastPageProbeMs = 0;
uint32_t lastPageReplyMs = 0;
uint32_t sliderRequestMs = 0;
bool sliderPending = false;
bool sliderTouched = false;
bool refreshRequested = true;
bool sliderSyncRequested = true;
uint8_t publishIndex = 0;
HmiReadings currentReadings;
nextion_wire::Decoder decoder;

void requestRefresh() {
    publishIndex = 0;
    refreshRequested = true;
}

void sendCommand(const char *command) {
    Serial1.print(command);
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
    Serial1.write((uint8_t)0xFF);
}

void setText(const char *name, const char *value, uint16_t color) {
    char command[64];
    snprintf(command, sizeof(command), "%s.txt=\"%s\"", name, value);
    sendCommand(command);
    snprintf(command, sizeof(command), "%s.pco=%u", name, color);
    sendCommand(command);
}

uint16_t statusColor(ReadingStatus status) {
    switch (status) {
        case ReadingStatus::OK: return 63455;
        case ReadingStatus::WAIT: return 62922;
        case ReadingStatus::OFFLINE: return 64715;
        case ReadingStatus::ERROR: return 60238;
    }
    return 60238;
}

void showReading(const char *label, const HmiReading &reading, const char *format) {
    char value[24];
    if (reading.status == ReadingStatus::OK && isfinite(reading.value)) {
        snprintf(value, sizeof(value), format, reading.value);
    } else {
        snprintf(value, sizeof(value), "%s",
                 reading.status == ReadingStatus::WAIT ? "WAIT" :
                 reading.status == ReadingStatus::OFFLINE ? "OFFLINE" : "ERROR");
    }
    setText(label, value, statusColor(reading.status));
}

void stopMotor() {
    digitalWrite(MOTOR_L_EN, LOW);
    digitalWrite(MOTOR_R_EN, LOW);
    analogWrite(MOTOR_LPWM, 0);
    analogWrite(MOTOR_RPWM, 0);
    direction = Direction::STOP;
    pendingDirection = Direction::STOP;
    requestRefresh();
}

void runMotor(Direction requested) {
    if (requested == Direction::STOP || duty == 0) {
        stopMotor();
        return;
    }
    digitalWrite(MOTOR_L_EN, LOW);
    digitalWrite(MOTOR_R_EN, LOW);
    analogWrite(MOTOR_LPWM, 0);
    analogWrite(MOTOR_RPWM, 0);
    analogWrite(MOTOR_LPWM, requested == Direction::FWD ? duty : 0);
    analogWrite(MOTOR_RPWM, requested == Direction::REV ? duty : 0);
    digitalWrite(MOTOR_L_EN, HIGH);
    digitalWrite(MOTOR_R_EN, HIGH);
    direction = requested;
    pendingDirection = Direction::STOP;
    requestRefresh();
}

void requestDirection(Direction requested) {
    lastControlMs = millis();
    if (requested == Direction::STOP || duty == 0) {
        stopMotor();
    } else if (pendingDirection != Direction::STOP) {
        // Repeated touch events cannot skip the existing reversal pause.
        pendingDirection = requested;
        requestRefresh();
    } else if (direction != Direction::STOP && direction != requested) {
        stopMotor();
        pendingDirection = requested;
        reversalStartedMs = millis();
    } else {
        runMotor(requested);
    }
}

void setSpeed(uint8_t speed) {
    duty = speed;
    lastControlMs = millis();
    if (duty == 0) stopMotor();
    else if (direction != Direction::STOP) runMotor(direction);
    requestRefresh();
}

void pageReceived(uint8_t page) {
    if (page != 0) {
        if (direction != Direction::STOP || pendingDirection != Direction::STOP)
            stopMotor();
        sliderPending = sliderTouched = false;
    } else {
        if (activePage != 0) {
            sliderSyncRequested = true;
            requestRefresh();
        }
        lastPageReplyMs = lastControlMs = millis();
    }
    activePage = page;
}

void handleAscii(const char *command) {
    if (strcmp(command, "DASH:SYNC") == 0 || strcmp(command, "DASH:PAGE:0") == 0) {
        pageReceived(0);
        sliderSyncRequested = true;
        requestRefresh();
    } else if (strcmp(command, "MOTOR:STOP") == 0) {
        requestDirection(Direction::STOP);
    } else if (activePage == 0) {
        if (strcmp(command, "MOTOR:KEEPALIVE") == 0) lastControlMs = millis();
        else if (strcmp(command, "MOTOR:FWD") == 0) requestDirection(Direction::FWD);
        else if (strcmp(command, "MOTOR:REV") == 0) requestDirection(Direction::REV);
        else if (strncmp(command, "MOTOR:SPEED:", 12) == 0) {
            const char *digits = command + 12;
            const size_t length = strlen(digits);
            if (length == 0 || length > 3) return;
            unsigned speed = 0;
            for (size_t i = 0; i < length; ++i) {
                if (digits[i] < '0' || digits[i] > '9') return;
                speed = speed * 10 + unsigned(digits[i] - '0');
            }
            if (speed <= 255) {
                sliderPending = false;
                setSpeed((uint8_t)speed);
            }
        }
    }
}

void handleEvent(const nextion_wire::Event &event) {
    using nextion_wire::Kind;
    if (event.kind == Kind::READY) {
        stopMotor();
        activePage = -1;
        sliderPending = sliderTouched = false;
        sliderSyncRequested = true;
        lastPageProbeMs = millis() - PAGE_PROBE_MS;
    } else if (event.kind == Kind::PAGE) {
        pageReceived(event.page);
    } else if (event.kind == Kind::ASCII) {
        handleAscii(event.text);
    } else if (event.kind == Kind::TOUCH && event.page == 0 && activePage == 0) {
        if (event.component == SLIDER) {
            sliderTouched = event.pressed;
            sliderSyncRequested = false;
            if (!event.pressed && !sliderPending) {
                sliderPending = true;
                sliderRequestMs = millis();
                sendCommand("get h0.val");
            }
        } else if (!event.pressed) {
            if (event.component == BUTTON_FWD) requestDirection(Direction::FWD);
            else if (event.component == BUTTON_REV) requestDirection(Direction::REV);
            else if (event.component == BUTTON_STOP) requestDirection(Direction::STOP);
        }
    } else if (event.kind == Kind::NUMBER && sliderPending) {
        sliderPending = false;
        if (activePage == 0 && millis() - sliderRequestMs <= SLIDER_REPLY_MS &&
            event.number <= 255) {
            setSpeed((uint8_t)event.number);
        } else {
            sliderSyncRequested = true;
            requestRefresh();
        }
    }
}

void pollSerial() {
    for (unsigned i = 0; i < 128 && Serial1.available(); ++i)
        handleEvent(decoder.push((uint8_t)Serial1.read()));
}

// One widget per tick keeps native STOP handling responsive during refresh.
void publishStep() {
    char value[24];
    switch (publishIndex++) {
        case 0: showReading("t0", currentReadings.processTemp, "%.2f C"); break;
        case 1: showReading("t1", currentReadings.ph, "%.2f"); break;
        case 2: showReading("t2", currentReadings.scdTemp, "%.2f C"); break;
        case 3: showReading("t3", currentReadings.shtTemp, "%.2f C"); break;
        case 4: showReading("t4", currentReadings.pressure, "%.3f bar"); break;
        case 5: showReading("t5", currentReadings.oxygen, "%.2f %%"); break;
        case 6: showReading("t6", currentReadings.co2, "%.0f ppm"); break;
        case 7: showReading("t7", currentReadings.scdRh, "%.2f %%"); break;
        case 8: showReading("t8", currentReadings.shtRh, "%.2f %%"); break;
        case 9:
            setText("t9", pendingDirection != Direction::STOP ? "SWITCHING" :
                    direction == Direction::FWD ? "ON FWD" :
                    direction == Direction::REV ? "ON REV" : "OFF",
                    direction == Direction::STOP ? 36118 : 20151);
            break;
        case 10:
            snprintf(value, sizeof(value), "%u/255 (%u%%)", duty,
                     (unsigned)(duty * 100U / 255U));
            setText("t10", value, 63455);
            break;
        case 11:
            if (sliderSyncRequested && !sliderTouched && !sliderPending) {
                snprintf(value, sizeof(value), "h0.val=%u", duty);
                sendCommand(value);
                sliderSyncRequested = false;
            }
            break;
    }
    if (publishIndex >= 12) refreshRequested = false;
}
} // namespace

void hmiMotorBegin() {
    digitalWrite(MOTOR_LPWM, LOW);
    digitalWrite(MOTOR_RPWM, LOW);
    digitalWrite(MOTOR_L_EN, LOW);
    digitalWrite(MOTOR_R_EN, LOW);
    pinMode(MOTOR_LPWM, OUTPUT);
    pinMode(MOTOR_RPWM, OUTPUT);
    pinMode(MOTOR_L_EN, OUTPUT);
    pinMode(MOTOR_R_EN, OUTPUT);
    duty = 64;
    activePage = -1;
    sliderPending = sliderTouched = false;
    sliderSyncRequested = true;
    decoder.reset();
    lastPageProbeMs = millis() - PAGE_PROBE_MS;
    lastPageReplyMs = lastControlMs = millis();
    stopMotor();
    Serial1.begin(NEXTION_BAUD, SERIAL_8N1, NEXTION_RX, NEXTION_TX);
    Serial.printf("[HMI] Page 0: t0..t10, buttons 13/14/15, slider 16; RX=%u TX=%u, 9600 baud\n",
                  NEXTION_RX, NEXTION_TX);
    Serial.println("[MOTOR] OFF; display polled every second, timeout 3 s");
}

void hmiMotorTick() {
    pollSerial();
    const uint32_t now = millis();
    if (activePage == 0 && now - lastPageReplyMs > MOTOR_TIMEOUT_MS) {
        activePage = -1;
        sliderPending = sliderTouched = false;
        stopMotor();
    }
    if ((direction != Direction::STOP || pendingDirection != Direction::STOP) &&
        now - lastControlMs > MOTOR_TIMEOUT_MS) {
        stopMotor();
        Serial.println("[MOTOR] STOP: Nextion connection timeout");
    }
    if (pendingDirection != Direction::STOP && now - reversalStartedMs >= REVERSE_PAUSE_MS)
        runMotor(pendingDirection);

    if (sliderPending && now - sliderRequestMs > SLIDER_REPLY_MS) {
        sliderPending = false;
        sliderSyncRequested = true;
        requestRefresh();
    }
    if (now - lastPageProbeMs >= PAGE_PROBE_MS) {
        sendCommand("sendme");
        lastPageProbeMs = now;
    }
    if (refreshRequested && activePage == 0 && Serial1.availableForWrite() >= 64)
        publishStep();
}

void hmiMotorSetReadings(const HmiReadings &readings) {
    currentReadings = readings;
    requestRefresh();
}
