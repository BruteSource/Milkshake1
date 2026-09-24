// Minimal multipart/x-mixed-replace MJPEG client. Plain HTTP over the LAN
// (no TLS) to a local relay -- see tools/livecam-relay/. The board only
// ever decodes single JPEGs (LGFX::drawJpg), same as every other frame in
// this app; this just keeps pulling a new one from a persistent connection
// instead of doing a fresh HTTPS request per image.
#pragma once
#include <WiFiClient.h>
#include <stddef.h>
#include <stdint.h>

namespace mjpeg {

class Client {
  public:
    ~Client();

    // Connects and sends the GET request; consumes the HTTP status line
    // and headers, leaving the stream positioned at the first frame part.
    bool begin(const char* host, uint16_t port, const char* path);

    // Blocks until the next full JPEG frame arrives. On success, *outBuf
    // points at an internal buffer valid until the next call; do not free
    // it. Returns false on timeout/disconnect -- caller should begin()
    // again.
    bool nextFrame(uint8_t** outBuf, size_t* outLen);

    // Non-blocking: true if a new frame has started arriving (safe to call
    // nextFrame() without a long stall). Use this to gate nextFrame() from
    // a UI loop that also needs to poll input every iteration.
    bool dataAvailable();

    void end();

  private:
    WiFiClient client_;
    uint8_t* buf_ = nullptr;
    size_t bufCap_ = 0;
};

}  // namespace mjpeg
