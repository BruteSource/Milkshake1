#include "Livecams.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <string.h>

namespace livecams {

namespace {

bool readLine(WiFiClient& c, char* buf, size_t bufLen, uint32_t timeoutMs) {
    size_t i = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (!c.connected() && c.available() == 0) return false;
        int ch = c.read();
        if (ch < 0) {
            delay(1);
            continue;
        }
        if (ch == '\r') continue;
        if (ch == '\n') {
            buf[i] = 0;
            return true;
        }
        if (i < bufLen - 1) buf[i++] = (char)ch;
        t0 = millis();
    }
    return false;
}

}  // namespace

int fetchList(Camera* out, int maxCount) {
    WiFiClient client;
    if (!client.connect(kRelayHost, kRelayPort)) {
        Serial.println("[livecams] relay unreachable");
        return 0;
    }
    client.printf("GET /cameras HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", kRelayHost);

    char line[160];
    size_t contentLen = 0;
    bool sawStatus = false, haveLen = false;
    while (readLine(client, line, sizeof(line), 8000)) {
        if (!sawStatus) {
            sawStatus = true;
            if (strstr(line, "200") == nullptr) {
                Serial.printf("[livecams] bad status: %s\n", line);
                return 0;
            }
            continue;
        }
        if (line[0] == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            contentLen = (size_t)atol(line + 15);
            haveLen = true;
        }
    }
    if (!haveLen || contentLen == 0) {
        Serial.println("[livecams] no content-length");
        return 0;
    }

    uint8_t* buf = (uint8_t*)malloc(contentLen);
    if (!buf) return 0;
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < contentLen && millis() - t0 < 8000) {
        int avail = client.available();
        if (avail <= 0) {
            delay(1);
            continue;
        }
        int r = client.read(buf + got, (int)min((size_t)avail, contentLen - got));
        if (r > 0) {
            got += (size_t)r;
            t0 = millis();
        }
    }
    client.stop();

    if (got != contentLen) {
        free(buf);
        Serial.println("[livecams] short read");
        return 0;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf, contentLen);
    free(buf);
    if (err) {
        Serial.printf("[livecams] JSON parse failed: %s\n", err.c_str());
        return 0;
    }

    JsonArrayConst arr = doc["cameras"];
    int count = 0;
    for (JsonObjectConst cam : arr) {
        if (count >= maxCount) break;
        Camera& c = out[count];
        strlcpy(c.id, cam["id"] | "", sizeof(c.id));
        strlcpy(c.title, cam["title"] | "", sizeof(c.title));
        strlcpy(c.category, cam["category"] | "", sizeof(c.category));
        count++;
    }
    Serial.printf("[livecams] loaded %d cameras\n", count);
    return count;
}

bool fetchJpeg(const char* id, uint8_t** outBuf, size_t* outLen) {
    WiFiClient client;
    if (!client.connect(kRelayHost, kRelayPort)) return false;

    char path[64];
    snprintf(path, sizeof(path), "/live/%s.jpg", id);
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, kRelayHost);

    char line[160];
    size_t contentLen = 0;
    bool sawStatus = false, haveLen = false;
    // Cold-start on the relay can take ~10s (yt-dlp resolve + first
    // segment); give the status line generous time.
    while (readLine(client, line, sizeof(line), 20000)) {
        if (!sawStatus) {
            sawStatus = true;
            if (strstr(line, "200") == nullptr) return false;
            continue;
        }
        if (line[0] == 0) break;
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            contentLen = (size_t)atol(line + 15);
            haveLen = true;
        }
    }
    if (!haveLen || contentLen == 0) return false;

    uint8_t* buf = (uint8_t*)malloc(contentLen);
    if (!buf) return false;
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < contentLen && millis() - t0 < 8000) {
        int avail = client.available();
        if (avail <= 0) {
            delay(1);
            continue;
        }
        int r = client.read(buf + got, (int)min((size_t)avail, contentLen - got));
        if (r > 0) {
            got += (size_t)r;
            t0 = millis();
        }
    }
    client.stop();

    if (got != contentLen) {
        free(buf);
        return false;
    }
    *outBuf = buf;
    *outLen = contentLen;
    return true;
}

}  // namespace livecams
