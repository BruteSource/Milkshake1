// Buffered HTTPS GET into a PSRAM heap block. Reading the TLS stream one
// byte at a time pegs the core and trips the watchdog on larger responses
// (see docs/HOSYOND_ESP32S3_TARGET.md gotcha #16), so this buffers in
// chunks with yields instead.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace http {

struct Header {
    const char* name;
    const char* value;
};

// On success, *outBuf is a heap_caps_malloc(MALLOC_CAP_SPIRAM) buffer the
// caller must free() -- *outLen bytes, NOT null-terminated for binary
// (JPEG) responses. Returns false on any failure (outBuf untouched).
bool getBuffered(const char* url, const Header* headers, size_t headerCount,
                  uint8_t** outBuf, size_t* outLen);

}  // namespace http
