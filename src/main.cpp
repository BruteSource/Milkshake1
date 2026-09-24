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
