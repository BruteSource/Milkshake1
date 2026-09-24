#include "Webcams.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "Http.h"
#include "Secrets.h"

namespace webcams {

namespace {

struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};

// Picks the best available snapshot URL out of whichever size keys exist
// under images.current. Order is largest-useful-for-320x240 first.
bool pickImageUrl(JsonObjectConst current, char* out, size_t outLen) {
    static const char* kSizeKeys[] = {"preview", "thumbnail", "icon"};
    for (const char* key : kSizeKeys) {
        if (current[key].is<const char*>()) {
            strlcpy(out, current[key], outLen);
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

int fetchList(Webcam* out, int maxCount) {
    char url[128];
    snprintf(url, sizeof(url), "https://api.windy.com/webcams/api/v3/webcams?limit=%d", maxCount);

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
        if (images.isNull() || !pickImageUrl(images, w.imageUrl, sizeof(w.imageUrl))) {
            continue;  // skip webcams with no usable image URL
        }

        count++;
    }

    Serial.printf("[webcams] parsed %d webcams\n", count);
    return count;
}

}  // namespace webcams
