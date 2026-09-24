// CYD-Milkshake: browse public webcams (Windy Webcams API) by region, in a
// paginated thumbnail grid, and view a periodically-refreshed snapshot on
// tap. Not true live video -- this hardware can't decode video streams, so
// "live" means re-fetching the JPEG snapshot on an interval, matching how
// the source webcams themselves typically update.
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

#include "hw/Display.h"
#include "hw/Touch.h"
#include "hw/Wifi.h"
#include "net/Http.h"
#include "net/Webcams.h"
#include "net/Livecams.h"
#include "net/Mjpeg.h"
#include "Secrets.h"

namespace {

LGFX lcd;

constexpr int kMaxWebcams = 40;
constexpr int kPerPage = 4;
webcams::Webcam g_webcams[kMaxWebcams];
int g_webcamCount = 0;
int g_page = 0;

constexpr int kMaxLiveCams = 100;
livecams::Camera g_liveCams[kMaxLiveCams];
int g_liveCamCount = 0;
char g_liveCategories[8][24];
int g_liveCategoryCount = 0;
int g_liveCategoryFilter = -1;  // index into g_liveCategories
int g_liveFilteredIdx[kMaxLiveCams];  // indices into g_liveCams matching the filter
int g_liveFilteredCount = 0;
mjpeg::Client g_mjpegClient;
uint32_t g_lastMjpegFrame = 0;

// --- Power management -----------------------------------------------------
// Soft sleep: dim the panel and pause network polling after 2 min idle,
// instant wake on any touch. Deep sleep: after 5 min idle (independent of
// soft sleep), real hardware deep sleep -- only the BOOT button (GPIO0,
// RTC-capable) can wake it, which means a full reboot back to the Home
// screen, not a resume.
constexpr uint32_t kSoftSleepMs = 2UL * 60 * 1000;
constexpr uint32_t kDeepSleepMs = 5UL * 60 * 1000;
uint32_t g_lastActivity = 0;
bool g_softAsleep = false;

enum class Screen { Home, Regions, Grid, Viewer, LiveCategories, LiveGrid, LiveViewer };
Screen g_screen = Screen::Home;
int g_selected = -1;

// 7 regions must fit in OT_H(240) below the 16px header: (240-16)/7 = 32.
constexpr int kRegionRowHeight = 32;
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

// --- Home screen ---------------------------------------------------------

void enterRegions();  // fwd decl, Windy webcam browser (below)
void enterLiveCategories();  // fwd decl, live nature cams (below)

void drawHome() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(6, 2);
    lcd.println("CYD-Milkshake");
    const char* items[] = {"Live Nature Cams", "Weather Webcams"};
    constexpr int kRowH = (OT_H - 16) / 2;
    for (int i = 0; i < 2; i++) {
        int y = 16 + i * kRowH;
        lcd.drawFastHLine(0, y, OT_W, TFT_DARKGREY);
        lcd.setCursor(10, y + kRowH / 2 - 8);
        lcd.setTextSize(2);
        lcd.println(items[i]);
    }
}

void enterHome() {
    g_screen = Screen::Home;
    drawHome();
}

// --- Regions screen ---------------------------------------------------

void drawRegions() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(6, 2);
    lcd.println("< home  |  select a region");
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

// --- Live nature cams (relay-backed, real motion at ~1fps) ---------------

void buildLiveCategories() {
    g_liveCategoryCount = 0;
    for (int i = 0; i < g_liveCamCount; i++) {
        bool found = false;
        for (int j = 0; j < g_liveCategoryCount; j++) {
            if (strcmp(g_liveCategories[j], g_liveCams[i].category) == 0) {
                found = true;
                break;
            }
        }
        if (!found && g_liveCategoryCount < 8) {
            strlcpy(g_liveCategories[g_liveCategoryCount], g_liveCams[i].category,
                    sizeof(g_liveCategories[0]));
            g_liveCategoryCount++;
        }
    }
}

void drawLiveCategories() {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(6, 2);
    lcd.println("< home  |  live nature cams");
    int rowH = (OT_H - 16) / (g_liveCategoryCount > 0 ? g_liveCategoryCount : 1);
    for (int i = 0; i < g_liveCategoryCount; i++) {
        int y = 16 + i * rowH;
        lcd.drawFastHLine(0, y, OT_W, TFT_DARKGREY);
        lcd.setCursor(10, y + rowH / 2 - 8);
        lcd.setTextSize(2);
        lcd.println(g_liveCategories[i]);
    }
}

void enterLiveCategories() {
    g_screen = Screen::LiveCategories;
    if (g_liveCamCount == 0) {
        lcd.fillScreen(TFT_BLACK);
        lcd.setCursor(10, 10);
        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.println("loading cameras...");
        g_liveCamCount = livecams::fetchList(g_liveCams, kMaxLiveCams);
        if (g_liveCamCount == 0) {
            lcd.setCursor(10, 30);
            lcd.setTextColor(TFT_RED, TFT_BLACK);
            lcd.println("relay unreachable");
            delay(1500);
            enterHome();
            return;
        }
        buildLiveCategories();
    }
    drawLiveCategories();
}

void buildLiveFiltered(int categoryIdx) {
    g_liveCategoryFilter = categoryIdx;
    g_liveFilteredCount = 0;
    for (int i = 0; i < g_liveCamCount; i++) {
        if (strcmp(g_liveCams[i].category, g_liveCategories[categoryIdx]) == 0) {
            g_liveFilteredIdx[g_liveFilteredCount++] = i;
        }
    }
}

int liveGridPageCount() { return (g_liveFilteredCount + kPerPage - 1) / kPerPage; }

void drawLiveGridChrome() {
    lcd.fillScreen(TFT_BLACK);
    lcd.drawFastHLine(0, kGridTop - 1, OT_W, TFT_DARKGREY);
    lcd.drawFastVLine(kCellW, kGridTop, kGridBottom - kGridTop, TFT_DARKGREY);
    lcd.drawFastHLine(0, kGridTop + kCellH, OT_W, TFT_DARKGREY);
    lcd.drawFastHLine(0, kGridBottom, OT_W, TFT_DARKGREY);

    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setCursor(4, 2);
    lcd.print("< categories");

    char buf[16];
    snprintf(buf, sizeof(buf), "page %d/%d", g_page + 1, liveGridPageCount());
    lcd.setCursor(OT_W / 2 - 30, OT_H - kFooterH + 4);
    lcd.print(buf);
    lcd.setCursor(4, OT_H - kFooterH + 4);
    lcd.print(g_page > 0 ? "< prev" : "");
    lcd.setCursor(OT_W - 46, OT_H - kFooterH + 4);
    lcd.print(g_page < liveGridPageCount() - 1 ? "next >" : "");
}

void drawLiveGridCell(int slot, int filteredIdx) {
    int x, y, w, h;
    cellRect(slot, &x, &y, &w, &h);
    if (filteredIdx >= g_liveFilteredCount) return;
    livecams::Camera& cam = g_liveCams[g_liveFilteredIdx[filteredIdx]];

    lcd.setCursor(x + 4, y + 4);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.print("loading...");

    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!livecams::fetchJpeg(cam.id, &buf, &len)) {
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
    lcd.print(cam.title);
}

void drawLiveGrid() {
    drawLiveGridChrome();
    int base = g_page * kPerPage;
    for (int slot = 0; slot < kPerPage; slot++) {
        drawLiveGridCell(slot, base + slot);
    }
}

void enterLiveGrid(int page) {
    g_page = page;
    g_screen = Screen::LiveGrid;
    drawLiveGrid();
}

void selectLiveCategory(int categoryIdx) {
    buildLiveFiltered(categoryIdx);
    enterLiveGrid(0);
}

void enterLiveViewer(int filteredIdx) {
    if (filteredIdx < 0 || filteredIdx >= g_liveFilteredCount) return;
    g_selected = filteredIdx;
    livecams::Camera& cam = g_liveCams[g_liveFilteredIdx[filteredIdx]];

    lcd.fillScreen(TFT_BLACK);
    lcd.setCursor(10, 10);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.println("connecting...");

    char path[64];
    snprintf(path, sizeof(path), "/live/%s.mjpg", cam.id);
    if (!g_mjpegClient.begin(livecams::kRelayHost, livecams::kRelayPort, path)) {
        lcd.setCursor(10, 30);
        lcd.setTextColor(TFT_RED, TFT_BLACK);
        lcd.println("connect failed");
        delay(1500);
        enterLiveGrid(g_page);
        return;
    }
    g_screen = Screen::LiveViewer;
    g_lastMjpegFrame = millis();
}

void exitLiveViewer() {
    g_mjpegClient.end();
    enterLiveGrid(g_page);
}

void enterSoftSleep() {
    g_softAsleep = true;
    lcd.setBrightness(0);
}

void wakeFromSoftSleep() {
    g_softAsleep = false;
    lcd.setBrightness(255);
    g_lastActivity = millis();
}

void enterDeepSleep() {
    lcd.setBrightness(0);
    lcd.fillScreen(TFT_BLACK);
    // BOOT button pull-up needs to be set on the RTC IO mux specifically --
    // the regular pinMode() pull-up doesn't hold through deep sleep.
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // wake on LOW (button pressed)
    esp_deep_sleep_start();
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

    enterHome();
    g_lastActivity = millis();
}

void loop() {
    // BOOT (GPIO0) also wakes from deep sleep, so right after such a wake
    // the user may still be holding it down from that press. Require an
    // observed release before arming the hold-for-calibration timer, so
    // that doesn't also trigger calibration.
    static bool bootArmed = false;
    static uint32_t bootHeldSince = 0;
    if (digitalRead(0) == HIGH) {
        bootArmed = true;
        bootHeldSince = 0;
    } else if (bootArmed) {
        if (bootHeldSince == 0) {
            bootHeldSince = millis();
        } else if (millis() - bootHeldSince > 2000) {
            runCalibration();  // never returns -- ends in ESP.restart()
        }
    }

    touch::Point p = touch::poll();

    if (p.down || p.pressed) g_lastActivity = millis();

    // A touch that wakes from soft sleep is consumed by the wake itself --
    // it shouldn't also act on whatever's underneath (e.g. immediately
    // jumping into a grid cell the user only meant to wake the screen).
    bool consumedByWake = false;
    if (g_softAsleep && p.down) {
        wakeFromSoftSleep();
        consumedByWake = true;
    }

    if (!g_softAsleep && millis() - g_lastActivity > kSoftSleepMs) {
        enterSoftSleep();
    }
    if (millis() - g_lastActivity > kDeepSleepMs) {
        enterDeepSleep();  // never returns -- wakes via full reboot
    }

    if (p.pressed && !consumedByWake && !g_softAsleep) {
        switch (g_screen) {
            case Screen::Home: {
                if (p.y >= 16) {
                    bool topHalf = (p.y - 16) < (OT_H - 16) / 2;
                    if (topHalf) enterLiveCategories();
                    else enterRegions();
                }
                break;
            }
            case Screen::Regions: {
                if (p.y < 16) {
                    enterHome();
                    break;
                }
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
            case Screen::LiveCategories: {
                if (p.y < 16) {
                    enterHome();
                    break;
                }
                int rowH = (OT_H - 16) / (g_liveCategoryCount > 0 ? g_liveCategoryCount : 1);
                int row = (p.y - 16) / rowH;
                if (row >= 0 && row < g_liveCategoryCount) selectLiveCategory(row);
                break;
            }
            case Screen::LiveGrid: {
                if (p.y < kHeaderH) {
                    enterLiveCategories();
                } else if (p.y >= kGridBottom) {
                    if (p.x < OT_W / 3 && g_page > 0) enterLiveGrid(g_page - 1);
                    else if (p.x > 2 * OT_W / 3 && g_page < liveGridPageCount() - 1) enterLiveGrid(g_page + 1);
                } else {
                    int col = p.x < kCellW ? 0 : 1;
                    int row = p.y < kGridTop + kCellH ? 0 : 1;
                    int idx = g_page * kPerPage + row * 2 + col;
                    if (idx < g_liveFilteredCount) enterLiveViewer(idx);
                }
                break;
            }
            case Screen::LiveViewer:
                exitLiveViewer();
                break;
        }
    }

    if (g_screen == Screen::LiveViewer && !g_softAsleep) {
        // Only call the (blocking-ish) nextFrame() once data has actually
        // started arriving -- otherwise, between the relay's ~1fps frames,
        // this loop would sit blocked inside nextFrame() for most of each
        // second and only poll touch once per frame, making taps easy to
        // miss entirely. Checking dataAvailable() first keeps every loop
        // iteration fast (~15ms) so touch::poll() runs at full rate.
        if (g_mjpegClient.dataAvailable()) {
            uint8_t* buf = nullptr;
            size_t len = 0;
            if (g_mjpegClient.nextFrame(&buf, &len)) {
                lcd.drawJpg(buf, len, 0, 0, OT_W, OT_H);
                livecams::Camera& cam = g_liveCams[g_liveFilteredIdx[g_selected]];
                lcd.fillRect(0, OT_H - 14, OT_W, 14, TFT_BLACK);
                lcd.setCursor(2, OT_H - 12);
                lcd.setTextSize(1);
                lcd.setTextColor(TFT_WHITE, TFT_BLACK);
                lcd.println(cam.title);
                g_lastMjpegFrame = millis();
            }
        }
        if (millis() - g_lastMjpegFrame > 20000) {
            // Connection stalled/dropped -- reconnect to the same camera.
            // (20s, not less: a cold-started relay source can take ~10s
            // for its first frame -- yt-dlp resolve + first HLS segment.)
            enterLiveViewer(g_selected);
        }
    }

    if (g_screen == Screen::Viewer && !g_softAsleep && millis() - g_lastRefresh > kRefreshIntervalMs) {
        loadAndShowImage(g_selected);
    }

    delay(15);
}
