#include "Touch.h"
#include <Arduino.h>
#include <Wire.h>

#include "Display.h"  // OT_W, OT_H

namespace {
constexpr uint8_t FT_ADDR = 0x38;
constexpr int PIN_SDA = 16;
constexpr int PIN_SCL = 15;
constexpr int PIN_RST = 18;
constexpr int PIN_INT = 17;

// Measured on this specific unit via 4-corner tap (PR #1, commit 9b31b41
// discussion). Screen X is driven by native Y span 289..8 (inverted --
// map() handles that fine); screen Y by native X span 10..227.
constexpr int16_t CAL_SX_AT_LEFT = 289, CAL_SX_AT_RIGHT = 8;
constexpr int16_t CAL_SY_AT_TOP = 10, CAL_SY_AT_BOTTOM = 227;

bool s_down = false;
int16_t s_x0 = 0, s_y0 = 0;
int s_maxMove = 0;
uint32_t s_downMs = 0;
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

namespace {
bool readPoint(int16_t* sx, int16_t* sy) {
    int16_t rx, ry;
    if (!rawSample(&rx, &ry)) return false;

    long x = map(ry, CAL_SX_AT_LEFT, CAL_SX_AT_RIGHT, 0, OT_W);
    long y = map(rx, CAL_SY_AT_TOP, CAL_SY_AT_BOTTOM, 0, OT_H);

    // Edge nudge on the long (X) axis only, per the reference project.
    if (x < 48)             x -= (48 - x) / 4;
    else if (x > OT_W - 49) x += (x - (OT_W - 49)) / 4;

    *sx = (int16_t)constrain(x, 0, OT_W - 1);
    *sy = (int16_t)constrain(y, 0, OT_H - 1);
    return true;
}
}  // namespace

Point poll() {
    Point e{s_x0, s_y0, false, false};
    int16_t x, y;
    const bool touching = readPoint(&x, &y);
    const uint32_t now = millis();

    if (touching) {
        e.down = true;
        e.x = x;
        e.y = y;
        if (!s_down) {
            s_down = true;
            s_x0 = x;
            s_y0 = y;
            s_maxMove = 0;
            s_downMs = now;
        } else {
            int dist = abs(x - s_x0) + abs(y - s_y0);
            if (dist > s_maxMove) s_maxMove = dist;
        }
    } else if (s_down) {
        s_down = false;
        if (s_maxMove < 24 && now - s_downMs < 700) {
            e.pressed = true;
            e.x = s_x0;
            e.y = s_y0;
        }
    }
    return e;
}

}  // namespace touch
