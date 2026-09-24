// CYD-Milkshake: browse public webcams (Windy Webcams API) by region, in a
// paginated thumbnail grid, and view a periodically-refreshed snapshot on
// tap. Not true live video -- this hardware can't decode video streams, so
// "live" means re-fetching the JPEG snapshot on an interval, matching how
// the source webcams themselves typically update.
#include <Arduino.h>

#include "hw/Display.h"
#include "hw/Touch.h"
#include "hw/Wifi.h"
#include "net/Http.h"
#include "net/Webcams.h"
#include "Secrets.h"

namespace {

LGFX lcd;

constexpr int kMaxWebcams = 40;
constexpr int kPerPage = 4;
webcams::Webcam g_webcams[kMaxWebcams];
int g_webcamCount = 0;
int g_page = 0;

enum class Screen { Regions, Grid, Viewer };
Screen g_screen = Screen::Regions;
int g_selected = -1;

constexpr int kRegionRowHeight = 34;
constexpr int kHeaderH = 16;
constexpr int kFooterH = 20;
constexpr int kGridTop = kHeaderH;
constexpr int kGridBottom = OT_H - kFooterH;
constexpr int kCellW = OT_W / 2;
constexpr int kCellH = (kGridBottom - kGridTop) / 2;
constexpr uint32_t kRefreshIntervalMs = 15000;
uint32_t g_lastRefresh = 0;

int pageCount() { return (g_webcamCount + kPerPage - 1) / kPerPage; }

void showError(const char* msg) {
    lcd.fillRect(0, OT_H - 20, OT_W, 20, TFT_BLACK);
    lcd.setCursor(4, OT_H - 18);
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.println(msg);
}

// --- Regions screen ---------------------------------------------------

void drawRegions() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(6, 2);
    lcd.println("select a region");
    for (int i = 0; i < webcams::kRegionCount; i++) {
        int y = 16 + i * kRegionRowHeight;
        lcd.drawFastHLine(0, y, OT_W, TFT_DARKGREY);
        lcd.setCursor(10, y + 10);
        lcd.setTextSize(2);
        lcd.println(webcams::kRegions[i].name);
    }
}

void enterRegions() {
    g_screen = Screen::Regions;
    g_webcamCount = 0;
    drawRegions();
}

// --- Grid screen --------------------------------------------------------

void cellRect(int slot, int* x, int* y, int* w, int* h) {
    int col = slot % 2, row = slot / 2;
    *x = col * kCellW;
    *y = kGridTop + row * kCellH;
    *w = kCellW;
    *h = kCellH;
}

void drawGridChrome() {
    lcd.fillScreen(TFT_BLACK);
    lcd.drawFastHLine(0, kGridTop - 1, OT_W, TFT_DARKGREY);
    lcd.drawFastVLine(kCellW, kGridTop, kGridBottom - kGridTop, TFT_DARKGREY);
    lcd.drawFastHLine(0, kGridTop + kCellH, OT_W, TFT_DARKGREY);
    lcd.drawFastHLine(0, kGridBottom, OT_W, TFT_DARKGREY);

    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(4, 2);
    lcd.print("< regions");

    char buf[16];
    snprintf(buf, sizeof(buf), "page %d/%d", g_page + 1, pageCount());
    lcd.setCursor(OT_W / 2 - 30, OT_H - kFooterH + 4);
    lcd.print(buf);
    lcd.setCursor(4, OT_H - kFooterH + 4);
    lcd.print(g_page > 0 ? "< prev" : "");
    lcd.setCursor(OT_W - 46, OT_H - kFooterH + 4);
    lcd.print(g_page < pageCount() - 1 ? "next >" : "");
}

void drawGridCell(int slot, int idx) {
    int x, y, w, h;
    cellRect(slot, &x, &y, &w, &h);
    if (idx >= g_webcamCount) return;

    lcd.setCursor(x + 4, y + 4);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.print("loading...");

    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!http::getBuffered(g_webcams[idx].thumbUrl, nullptr, 0, &buf, &len)) {
        lcd.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
        lcd.setCursor(x + 4, y + h / 2);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.print("fetch failed");
        return;
    }
    lcd.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_BLACK);
    lcd.drawJpg(buf, len, x + 1, y + 1, w - 2, h - 14);
    free(buf);

    lcd.fillRect(x + 1, y + h - 13, w - 2, 12, TFT_BLACK);
    lcd.setCursor(x + 3, y + h - 12);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.print(g_webcams[idx].title);
}

void drawGrid() {
    drawGridChrome();
    int base = g_page * kPerPage;
    for (int slot = 0; slot < kPerPage; slot++) {
        drawGridCell(slot, base + slot);
    }
}

void enterGrid(int page) {
    g_page = page;
    g_screen = Screen::Grid;
    drawGrid();
}

void selectRegion(int regionIdx) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setCursor(10, 10);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.println("loading webcams...");
    g_webcamCount = webcams::fetchList(g_webcams, kMaxWebcams, webcams::kRegions[regionIdx].code);
    Serial.printf("loaded %d webcams for %s\n", g_webcamCount, webcams::kRegions[regionIdx].code);
    if (g_webcamCount == 0) {
        lcd.setCursor(10, 30);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("no webcams found");
        delay(1500);
        enterRegions();
        return;
    }
    enterGrid(0);
}

// --- Viewer screen --------------------------------------------------------

void loadAndShowImage(int idx) {
    if (idx < 0 || idx >= g_webcamCount) return;
    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!http::getBuffered(g_webcams[idx].previewUrl, nullptr, 0, &buf, &len)) {
        showError("image fetch failed");
        return;
    }
    lcd.fillScreen(TFT_BLACK);
    lcd.drawJpg(buf, len, 0, 0, OT_W, OT_H);
    free(buf);

    lcd.fillRect(0, OT_H - 14, OT_W, 14, TFT_BLACK);
    lcd.setCursor(2, OT_H - 12);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.println(g_webcams[idx].title);

    g_lastRefresh = millis();
}

void enterViewer(int idx) {
    g_selected = idx;
    g_screen = Screen::Viewer;
    loadAndShowImage(idx);
}

// --- Touch calibration (unchanged) ---------------------------------------

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
    delay(300);

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
    // enter UART download mode rather than running this app at all, so
    // gating on it at cold boot would race against that.
    pinMode(0, INPUT_PULLUP);

    if (!wifi::connect()) {
        lcd.setCursor(10, 60);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("wifi failed");
        return;
    }

    enterRegions();
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
        switch (g_screen) {
            case Screen::Regions: {
                int row = (p.y - 16) / kRegionRowHeight;
                if (row >= 0 && row < webcams::kRegionCount) selectRegion(row);
                break;
            }
            case Screen::Grid: {
                if (p.y < kHeaderH) {
                    enterRegions();
                } else if (p.y >= kGridBottom) {
                    if (p.x < OT_W / 3 && g_page > 0) enterGrid(g_page - 1);
                    else if (p.x > 2 * OT_W / 3 && g_page < pageCount() - 1) enterGrid(g_page + 1);
                } else {
                    int col = p.x < kCellW ? 0 : 1;
                    int row = p.y < kGridTop + kCellH ? 0 : 1;
                    int idx = g_page * kPerPage + row * 2 + col;
                    if (idx < g_webcamCount) enterViewer(idx);
                }
                break;
            }
            case Screen::Viewer:
                enterGrid(g_page);
                break;
        }
    }

    if (g_screen == Screen::Viewer && millis() - g_lastRefresh > kRefreshIntervalMs) {
        loadAndShowImage(g_selected);
    }

    delay(15);
}
