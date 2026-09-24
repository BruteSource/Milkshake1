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
