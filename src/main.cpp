// Dev-setup smoke test: bring up serial, display, and touch, and prove the
// flash/monitor loop works end to end before any app logic gets written.
#include <Arduino.h>

#include "hw/Display.h"
#include "hw/Touch.h"

static LGFX lcd;

void setup() {
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1200) delay(10);
    Serial.begin(115200);
    Serial.println("CYD-Milkshake smoke test booting");

    lcd.init();
    lcd.setRotation(OT_ROTATION);
    lcd.setBrightness(255);
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(10, 10);
    lcd.println("CYD-Milkshake");
    lcd.setTextSize(1);
    lcd.setCursor(10, 40);
    lcd.println("touch the screen");

    touch::begin();
    touch::diag();

    Serial.println("setup done");
}

void loop() {
    static int16_t lastX = -1, lastY = -1;
    int16_t rx, ry;

    if (touch::rawSample(&rx, &ry)) {
        if (rx != lastX || ry != lastY) {
            lastX = rx;
            lastY = ry;
            lcd.fillRect(0, 60, OT_W, 20, TFT_BLACK);
            lcd.setCursor(10, 60);
            lcd.printf("raw: %4d, %4d", rx, ry);
            Serial.printf("touch raw x=%d y=%d\n", rx, ry);
        }
    }

    delay(15);
}
