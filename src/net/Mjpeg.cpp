#include "Mjpeg.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdlib.h>

namespace mjpeg {

namespace {

// Reads one line (up to \n, stripping \r), blocking with a timeout.
// Returns false on timeout or disconnect.
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

bool readExact(WiFiClient& c, uint8_t* buf, size_t len, uint32_t timeoutMs) {
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < len) {
        if (millis() - t0 > timeoutMs) return false;
        if (!c.connected() && c.available() == 0) return false;
        int avail = c.available();
        if (avail <= 0) {
            delay(1);
            continue;
        }
        int toRead = (int)min((size_t)avail, len - got);
        int r = c.read(buf + got, toRead);
        if (r > 0) {
            got += (size_t)r;
            t0 = millis();
        }
    }
    return true;
}

}  // namespace

Client::~Client() {
    end();
    if (buf_) heap_caps_free(buf_);
}

bool Client::begin(const char* host, uint16_t port, const char* path) {
    end();
    if (!client_.connect(host, port)) return false;

    client_.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n", path, host);

    char line[160];
    bool sawStatus = false;
    while (readLine(client_, line, sizeof(line), 8000)) {
        if (!sawStatus) {
            sawStatus = true;
            if (strstr(line, "200") == nullptr) {
                end();
                return false;
            }
            continue;
        }
        if (line[0] == 0) break;  // blank line -> end of headers
    }
    return sawStatus && client_.connected();
}

bool Client::nextFrame(uint8_t** outBuf, size_t* outLen) {
    if (!client_.connected()) return false;

    char line[160];
    size_t contentLen = 0;
    bool haveLen = false;

    // Skip the boundary marker line(s), read this part's headers.
    while (readLine(client_, line, sizeof(line), 15000)) {
        if (line[0] == 0) {
            if (haveLen) break;  // blank line after headers -> body follows
            continue;            // stray blank line before boundary marker
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            contentLen = (size_t)atol(line + 15);
            haveLen = true;
        }
    }
    if (!haveLen || contentLen == 0 || contentLen > 200000) return false;

    if (contentLen > bufCap_) {
        uint8_t* grown = (uint8_t*)heap_caps_realloc(buf_, contentLen, MALLOC_CAP_SPIRAM);
        if (!grown) return false;
        buf_ = grown;
        bufCap_ = contentLen;
    }
    if (!readExact(client_, buf_, contentLen, 8000)) return false;

    // Trailing \r\n after this part's body, before the next boundary.
    char trail[4];
    readLine(client_, trail, sizeof(trail), 2000);

    *outBuf = buf_;
    *outLen = contentLen;
    return true;
}

bool Client::dataAvailable() {
    return client_.connected() && client_.available() > 0;
}

void Client::end() {
    client_.stop();
}

}  // namespace mjpeg
