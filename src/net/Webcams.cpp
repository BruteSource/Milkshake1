#include "Webcams.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "Http.h"
#include "Secrets.h"

namespace webcams {

const Region kRegions[] = {
    {"NA", "North America"}, {"SA", "South America"}, {"EU", "Europe"},
    {"AS", "Asia"},          {"AF", "Africa"},         {"OC", "Oceania"},
    {"AN", "Antarctica"},
};
const int kRegionCount = sizeof(kRegions) / sizeof(kRegions[0]);

namespace {

struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

bool pickUrl(JsonObjectConst current, const char* const* keysInOrder, int keyCount,
             char* out, size_t outLen) {
    for (int i = 0; i < keyCount; i++) {
        if (current[keysInOrder[i]].is<const char*>()) {
            strlcpy(out, current[keysInOrder[i]], outLen);
            return true;
        }
    }
    return false;
}

// location may be nested ({"location":{"latitude":...}}) or top-level
// ({"latitude":...}) depending on API version -- try both.
bool pickLatLon(JsonObjectConst cam, double* lat, double* lon) {
    JsonObjectConst loc = cam["location"];
    if (!loc.isNull() && loc["latitude"].is<double>()) {
        *lat = loc["latitude"];
        *lon = loc["longitude"];
        return true;
    }
    if (cam["latitude"].is<double>()) {
        *lat = cam["latitude"];
        *lon = cam["longitude"];
        return true;
    }
    return false;
}

}  // namespace

int fetchList(Webcam* out, int maxCount, const char* continentCode) {
    char url[192];
    // include=images,location -- confirmed required (PR #1). continents=
    // param name/codes NOT verified against live docs -- flagging for the
    // local session to confirm with one curl call.
    snprintf(url, sizeof(url),
             "https://api.windy.com/webcams/api/v3/webcams?limit=%d&include=images,location&continents=%s",
             maxCount, continentCode);

    http::Header headers[] = {{"x-windy-api-key", WINDY_API_KEY}};
    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!http::getBuffered(url, headers, 1, &buf, &len)) {
        Serial.println("[webcams] fetch failed");
        return 0;
    }

    SpiRamAllocator alloc;
    JsonDocument doc(&alloc);
    DeserializationError err = deserializeJson(doc, buf, len);
    free(buf);

    if (err) {
        Serial.printf("[webcams] JSON parse failed: %s\n", err.c_str());
        return 0;
    }

    // Response wrapper key varies by API version -- try the documented
    // "webcams" array, then fall back to the document being the array
    // itself.
    JsonArrayConst arr = doc["webcams"];
    if (arr.isNull()) arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        Serial.println("[webcams] unexpected response shape (no webcams array found)");
        return 0;
    }

    static const char* kThumbKeys[] = {"thumbnail", "icon", "preview"};
    static const char* kPreviewKeys[] = {"preview", "thumbnail", "icon"};

    int count = 0;
    for (JsonObjectConst cam : arr) {
        if (count >= maxCount) break;

        Webcam& w = out[count];
        const char* title = cam["title"] | "(untitled)";
        strlcpy(w.title, title, sizeof(w.title));

        if (!pickLatLon(cam, &w.lat, &w.lon)) {
            w.lat = 0;
            w.lon = 0;
        }

        JsonObjectConst images = cam["images"]["current"];
        if (images.isNull()) continue;
        bool haveThumb = pickUrl(images, kThumbKeys, 3, w.thumbUrl, sizeof(w.thumbUrl));
        bool havePreview = pickUrl(images, kPreviewKeys, 3, w.previewUrl, sizeof(w.previewUrl));
        if (!haveThumb || !havePreview) continue;  // skip webcams missing either size

        count++;
    }

    Serial.printf("[webcams] parsed %d webcams for continent=%s\n", count, continentCode);
    return count;
}

}  // namespace webcams
