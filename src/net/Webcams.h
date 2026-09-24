// Windy Webcams API (https://api.windy.com/webcams/docs) client.
// Response schema confirmed live (2026-09-24): the default response omits
// `images`/`location` entirely unless requested via `include=images,location`
// -- see fetchList().
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
