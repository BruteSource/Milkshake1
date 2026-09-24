#include "Http.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

namespace http {

bool getBuffered(const char* url, const Header* headers, size_t headerCount,
                  uint8_t** outBuf, size_t* outLen) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12);

    HTTPClient https;
    https.setConnectTimeout(8000);
    https.setTimeout(12000);
    https.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    https.useHTTP10(true);
    https.setUserAgent(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/122.0 Safari/537.36");

    if (!https.begin(client, url)) return false;
    for (size_t i = 0; i < headerCount; i++) {
        https.addHeader(headers[i].name, headers[i].value);
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[http] GET %s -> %d\n", url, code);
        https.end();
        return false;
    }

    int contentLen = https.getSize();  // -1 if chunked/unknown
    WiFiClient* stream = https.getStreamPtr();

    size_t cap = contentLen > 0 ? (size_t)contentLen : (256 * 1024);
    uint8_t* buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!buf) {
        https.end();
        return false;
    }

    size_t written = 0;
    uint32_t lastData = millis();
    while (https.connected() && (contentLen > 0 ? written < (size_t)contentLen : true)) {
        size_t avail = stream->available();
        if (avail) {
            if (written + avail > cap) {
                // grow (contentLen unknown case) -- double the buffer
                size_t newCap = cap * 2;
                uint8_t* grown = (uint8_t*)heap_caps_realloc(buf, newCap, MALLOC_CAP_SPIRAM);
                if (!grown) break;
                buf = grown;
                cap = newCap;
            }
            int r = stream->readBytes(buf + written, avail);
            if (r > 0) {
                written += r;
                lastData = millis();
            }
        } else {
            if (contentLen < 0 && !https.connected()) break;  // chunked stream ended
            if (millis() - lastData > 15000) break;            // stall timeout
            vTaskDelay(pdMS_TO_TICKS(3));
        }
    }
    https.end();

    if (written == 0) {
        free(buf);
        return false;
    }

    *outBuf = buf;
    *outLen = written;
    return true;
}

}  // namespace http
