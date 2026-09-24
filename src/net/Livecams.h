// Client for the local livecam-relay (tools/livecam-relay/), which
// re-serves explore.org's officially-operated live nature cam network as
// plain local-network MJPEG. Plain HTTP, not HTTPS -- LAN-only.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace livecams {

constexpr const char* kRelayHost = "192.168.0.168";
constexpr uint16_t kRelayPort = 8090;

struct Camera {
    char id[32];
    char title[64];
    char category[24];
};

// Fetches the relay's /cameras list into `out`. Returns the number
// populated, or 0 on failure (relay unreachable, etc).
int fetchList(Camera* out, int maxCount);

// Plain-HTTP fetch of /live/<id>.jpg (a single current frame -- used for
// grid thumbnails, not the persistent stream; see mjpeg::Client for that).
// Caller must free() *outBuf.
bool fetchJpeg(const char* id, uint8_t** outBuf, size_t* outLen);

}  // namespace livecams
