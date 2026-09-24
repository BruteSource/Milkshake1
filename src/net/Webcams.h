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
    char thumbUrl[224];    // small size (icon/thumbnail), for the grid
    char previewUrl[224];  // larger size (preview/thumbnail), for the full viewer
};

// A continent filter for fetchList(). code is the query-param value sent
// as `continents=<code>` -- NOT verified against live docs (api.windy.com
// blocked from this sandbox), assumed from the REST convention Windy's
// other endpoints use (/webcams/api/v3/continents etc). First live call
// should confirm the param name and codes are right.
struct Region {
    const char* code;
    const char* name;
};
extern const Region kRegions[];
extern const int kRegionCount;

// Fetches up to maxCount webcams for the given continent code into `out`.
// Returns the number actually populated, or 0 on failure.
int fetchList(Webcam* out, int maxCount, const char* continentCode);

}  // namespace webcams
