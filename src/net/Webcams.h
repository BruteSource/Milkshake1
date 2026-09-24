// Windy Webcams API (https://api.windy.com/webcams/docs) client.
// NOTE: the exact response schema (nesting of location/images fields) is
// assumed from third-party source, not verified against live docs -- see
// the parsing fallbacks in Webcams.cpp. First live run should confirm/
// correct field names.
#pragma once
#include <stdint.h>

namespace webcams {

struct Webcam {
    char title[80];
    double lat, lon;
    char imageUrl[256];  // JPEG snapshot URL, token-expires in ~10 min (free tier)
};

// Fetches up to maxCount webcams from around the world into `out`. Returns
// the number actually populated, or 0 on failure.
int fetchList(Webcam* out, int maxCount);

}  // namespace webcams
