#include "Touch.h"
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t FT_ADDR = 0x38;
constexpr int PIN_SDA = 16;
constexpr int PIN_SCL = 15;
constexpr int PIN_RST = 18;
constexpr int PIN_INT = 17;
}  // namespace

namespace touch {

void begin() {
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, LOW);
    delay(10);
    digitalWrite(PIN_RST, HIGH);
    delay(300);

    Wire.begin(PIN_SDA, PIN_SCL, 400000U);
    pinMode(PIN_INT, INPUT_PULLUP);

    // Raise touch threshold a little to cut phantom taps.
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0x80);
    Wire.write(40);
    Wire.endTransmission();

    // G_MODE = polling: INT pulses low repeatedly while touched.
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0xA4);
    Wire.write(0x00);
    Wire.endTransmission();
}

bool isTouched() {
    return digitalRead(PIN_INT) == LOW;
}

namespace {
uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(FT_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    if (Wire.requestFrom((uint8_t)FT_ADDR, (uint8_t)1) != 1) return 0xFF;
    return Wire.read();
}
}  // namespace

void diag() {
    Serial.println("[touch diag] I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[touch diag]   device at 0x%02X\n", a);
            ++found;
        }
    }
    if (!found) Serial.println("[touch diag]   (none -- bus/wiring problem)");

    Serial.printf("[touch diag] INT pin=%d  chipid(0xA3)=0x%02X  vendor(0xA8)=0x%02X  gmode(0xA4)=0x%02X  threshold(0x80)=0x%02X\n",
                  digitalRead(PIN_INT), readReg(0xA3), readReg(0xA8), readReg(0xA4), readReg(0x80));

    Serial.println("[touch diag] sampling TD_STATUS (reg 0x02 alone) at rest for 2s -- do NOT touch the screen:");
    int nonzero = 0, total = 0, readFail = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
        uint8_t td = readReg(0x02);
        total++;
        if (td == 0xFF) readFail++;
        else if ((td & 0x0F) != 0) nonzero++;
        delay(15);
    }
    Serial.printf("[touch diag] TD_STATUS samples=%d nonzero=%d readFail=%d\n", total, nonzero, readFail);
    Serial.println("[touch diag] if nonzero is high here (untouched), the sensor itself is reporting phantom "
                    "touches -- not an I2C read bug. If readFail is high, the bus read is failing (wiring/pull-ups).");
}

bool rawSample(int16_t* rx, int16_t* ry) {
    uint8_t d[7];
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    if ((int)Wire.requestFrom(FT_ADDR, (uint8_t)7) != 7) return false;
    for (auto& b : d) b = Wire.read();

    if ((d[0] & 0x0F) == 0) return false;  // no finger

    int x = ((uint16_t)(d[1] & 0x0F) << 8) | d[2];
    int y = ((uint16_t)(d[3] & 0x0F) << 8) | d[4];
    if (x > 800 || y > 800) return false;  // 0x0FFF garbage guard

    *rx = (int16_t)x;
    *ry = (int16_t)y;
    return true;
}

}  // namespace touch
