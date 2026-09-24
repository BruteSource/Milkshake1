// CYD-Milkshake: browse public webcams (Windy Webcams API) and view a
// periodically-refreshed snapshot on the display. Not true live video --
// this hardware can't decode video streams, so "live" means re-fetching
// the JPEG snapshot every few seconds, same as the source webcams
// themselves typically update.
#include <Arduino.h>

#include "hw/Display.h"
#include "hw/Touch.h"
#include "hw/Wifi.h"
#include "net/Http.h"
#include "net/Webcams.h"
#include "Secrets.h"

namespace {

LGFX lcd;

constexpr int kMaxWebcams = 10;
webcams::Webcam g_webcams[kMaxWebcams];
int g_webcamCount = 0;

enum class Screen { List, Viewer };
Screen g_screen = Screen::List;
int g_selected = -1;

constexpr int kRowHeight = 22;
constexpr uint32_t kRefreshIntervalMs = 15000;
uint32_t g_lastRefresh = 0;

void drawList() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    if (g_webcamCount == 0) {
        lcd.setCursor(10, 10);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("no webcams loaded");
        return;
    }
    for (int i = 0; i < g_webcamCount; i++) {
        lcd.setCursor(6, 4 + i * kRowHeight);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.println(g_webcams[i].title);
    }
}

void showError(const char* msg) {
    lcd.fillRect(0, OT_H - 20, OT_W, 20, TFT_BLACK);
    lcd.setCursor(4, OT_H - 18);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.println(msg);
}

void loadAndShowImage(int idx) {
    if (idx < 0 || idx >= g_webcamCount) return;
    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!http::getBuffered(g_webcams[idx].imageUrl, nullptr, 0, &buf, &len)) {
        showError("image fetch failed");
        return;
    }
    lcd.fillScreen(TFT_BLACK);
    lcd.drawJpg(buf, len, 0, 0, OT_W, OT_H);
    free(buf);
    g_lastRefresh = millis();
}

void enterViewer(int idx) {
    g_selected = idx;
    g_screen = Screen::Viewer;
    loadAndShowImage(idx);
}

void enterList() {
    g_screen = Screen::List;
    drawList();
}

// Interactive 4-dot calibration. Draws a crosshair at each of 4 inset
// points (avoids the unreliable true bezel edge), waits for a full
// press-and-release on each, and uses the last raw sample seen before
// release (finger is most stable right before lifting). Order: TL, TR,
// BR, BL -- matches the manual PR #1 corner-tap pass this replaces.
// Saves the result to NVS via touch::setCalibration() and reboots so the
// new calibration is in effect from a clean boot.
constexpr int kCalInset = 20;

void drawCalDot(int x, int y) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(10, 10);
    lcd.println("calibration: tap the dot");
    lcd.drawFastHLine(x - 6, y, 13, TFT_GREEN);
    lcd.drawFastVLine(x, y - 6, 13, TFT_GREEN);
    lcd.drawCircle(x, y, 8, TFT_GREEN);
}

// Blocks until a full press-and-release, returns the last raw sample seen
// while touched (just before release).
void captureCalPoint(int16_t* outRx, int16_t* outRy) {
    int16_t lastRx = -1, lastRy = -1;
    bool sawTouch = false;
    while (true) {
        int16_t rx, ry;
        if (touch::rawSample(&rx, &ry)) {
            lastRx = rx;
            lastRy = ry;
            sawTouch = true;
        } else if (sawTouch) {
            break;
        }
        delay(15);
    }
    *outRx = lastRx;
    *outRy = lastRy;
}

void runCalibration() {
    const int x0 = kCalInset, x1 = OT_W - 1 - kCalInset;
    const int y0 = kCalInset, y1 = OT_H - 1 - kCalInset;

    int16_t rxTL, ryTL, rxTR, ryTR, rxBR, ryBR, rxBL, ryBL;

    drawCalDot(x0, y0);
    captureCalPoint(&rxTL, &ryTL);
    delay(300);  // debounce release before the next point

    drawCalDot(x1, y0);
    captureCalPoint(&rxTR, &ryTR);
    delay(300);

    drawCalDot(x1, y1);
    captureCalPoint(&rxBR, &ryBR);
    delay(300);

    drawCalDot(x0, y1);
    captureCalPoint(&rxBL, &ryBL);
    delay(300);

    const int16_t sxLeft = (int16_t)((ryTL + ryBL) / 2);
    const int16_t sxRight = (int16_t)((ryTR + ryBR) / 2);
    const int16_t syTop = (int16_t)((rxTL + rxTR) / 2);
    const int16_t syBottom = (int16_t)((rxBL + rxBR) / 2);

    touch::setCalibration(sxLeft, sxRight, syTop, syBottom);

    Serial.printf("[cal] TL=(%d,%d) TR=(%d,%d) BR=(%d,%d) BL=(%d,%d)\n",
                  rxTL, ryTL, rxTR, ryTR, rxBR, ryBR, rxBL, ryBL);
    Serial.printf("[cal] saved sxLeft=%d sxRight=%d syTop=%d syBottom=%d\n",
                  sxLeft, sxRight, syTop, syBottom);

    lcd.fillScreen(TFT_BLACK);
    lcd.setCursor(10, 10);
    lcd.println("calibration saved");
    lcd.printf("sxL=%d sxR=%d\nsyT=%d syB=%d\n", sxLeft, sxRight, syTop, syBottom);
    lcd.println("restarting...");
    delay(1500);
    ESP.restart();
}

}  // namespace

void setup() {
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1200) delay(10);
    Serial.begin(115200);
    Serial.println("CYD-Milkshake booting");

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
    lcd.println("connecting wifi...");

    touch::begin();

    // Hold the physical BOOT button (GPIO0) for 2s once the app is running
    // to (re)run touch calibration -- checked continuously in loop(), not
    // here. GPIO0 held low across a reset instead makes the ROM bootloader
    // enter UART download mode rather than running this app at all (that's
    // how flashing mode is normally entered), so gating on it at cold boot
    // would race against that; polling it mid-run avoids that entirely.
    pinMode(0, INPUT_PULLUP);

    if (!wifi::connect()) {
        lcd.setCursor(10, 60);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("wifi failed");
        return;
    }

    lcd.setCursor(10, 60);
    lcd.println("loading webcams...");
    g_webcamCount = webcams::fetchList(g_webcams, kMaxWebcams);
    Serial.printf("loaded %d webcams\n", g_webcamCount);

    enterList();
}

void loop() {
    static uint32_t bootHeldSince = 0;
    if (digitalRead(0) == LOW) {
        if (bootHeldSince == 0) {
            bootHeldSince = millis();
        } else if (millis() - bootHeldSince > 2000) {
            runCalibration();  // never returns -- ends in ESP.restart()
        }
    } else {
        bootHeldSince = 0;
    }

    touch::Point p = touch::poll();

    if (p.pressed) {
        if (g_screen == Screen::List) {
            int row = p.y / kRowHeight;
            if (row >= 0 && row < g_webcamCount) enterViewer(row);
        } else {
            enterList();
        }
    }

    if (g_screen == Screen::Viewer && millis() - g_lastRefresh > kRefreshIntervalMs) {
        loadAndShowImage(g_selected);
    }

    delay(15);
}
