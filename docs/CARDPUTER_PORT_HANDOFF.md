# CYD-Milkshake → M5Stack Cardputer ADV port: handoff notes

Written for: a fresh Claude Code session picking up this port with no prior
context. This document is the complete state of the CYD-Milkshake project as
of 2026-09-24, written so a new session can port the live-nature-cam browser
feature to a different board (M5Stack Cardputer ADV) without re-deriving
anything that was already learned the hard way.

## 1. What this project is

CYD-Milkshake (GitHub: `BruteSource/Milkshake1`) is firmware for a "Cheap
Yellow Display" ESP32-S3 board that browses ~113 live nature/scenic
webcams — bears, African wildlife, ocean reefs, bird nest boxes, kittens
and puppies at rescue/service-dog orgs, and Pacific Northwest scenic/coast
cams (the user is from the PNW) — by category, in a paginated thumbnail
grid, with a full-screen viewer showing real ~6fps motion video.

The ESP32-S3 cannot decode any real video codec (no H.264/HLS support), only
individual JPEGs. So a **relay** — a small Python service running on a LAN
laptop, *not* on the ESP32 — pulls each camera's YouTube Live (or other)
stream, transcodes it with ffmpeg, and re-serves it to the board as
plain-HTTP MJPEG (`multipart/x-mixed-replace`) over the LAN. The board just
pulls MJPEG frames and draws them; it does zero video decoding of its own.

**This relay/server-side design is host-hardware-agnostic and needs no
changes for the Cardputer port.** The only client-side network code is a
plain HTTP GET of a JSON camera list and a plain HTTP MJPEG stream — both
trivially portable to any ESP32 board with WiFi.

## 2. Why a relay exists (read this before trying to simplify the architecture)

The user originally wanted this to run **standalone on the chip**, no
companion device, using ffmpeg directly on the ESP32 like some other
projects (e.g. Evil Cardputer firmware) apparently do. After investigation,
confirmed the ESP32-S3 genuinely has no HLS/H.264 decode capability — those
other projects either use much lower framerate tricks or aren't doing real
H.264 decode. The user accepted a relay-based architecture after this was
demonstrated. **Don't re-litigate this if the new session or user brings it
up again — it was already settled with evidence.**

## 3. Current hardware (source board) vs. target hardware (Cardputer ADV)

| | CYD board (current) | Cardputer ADV (target) |
|---|---|---|
| SoC | ESP32-S3 (16MB flash, PSRAM, qio_opi) | ESP32-S3FN8 (Stamp-S3A module, **8MB flash, likely NO PSRAM** — "N8" naming typically means no PSRAM; **verify this before porting**, since current firmware's buffers may assume PSRAM is available) |
| Display | ILI9341, 320×240, SPI, touchscreen (FT6336 capacitive) | 1.14" IPS LCD, **240×135** (much smaller, no touch mentioned in specs) |
| Input | Capacitive touch (tap-based UI) | **56-key physical keyboard**, no touch — this is the biggest UI rework needed |
| Power | USB-powered dev board | 1,750 mAh battery, BMI270 IMU, ES8311 audio codec, speaker+mic |
| Panel driver | `lgfx::Panel_ILI9341` (LovyanGFX) | Needs its own LovyanGFX panel config — likely ST7789 or similar; **look up the exact panel/pins for Cardputer ADV** (M5Stack usually publishes these; check M5Unified or M5GFX library source for the canonical LovyanGFX config rather than guessing pins) |

**Porting implications, in priority order:**
1. **Input model must be redesigned.** Every screen in `src/main.cpp` is
   driven by `touch::poll()` and tap hit-testing (`listHitTest`,
   `cellRect`-based grid taps, corner-based prev/next paging). None of that
   applies to a keyboard. Plan for arrow-key/WASD navigation + Enter to
   select + Esc/Backspace to go back, replacing every touch call site.
2. **Screen resolution is much smaller and different aspect ratio**
   (320×240, 4:3-ish vs 240×135, ~16:9-ish). The grid layout
   (`kPerPage`, `cellRect`, `kCellW`/`kCellH`), the live viewer's
   scale-to-fit math, and the title-bar-strip approach all assume the old
   dimensions and will need re-tuning, not just a constant swap — a 2×2
   grid of thumbnails may not fit or look good at 240×135 the same way it
   does at 320×240 minus header/footer chrome. Consider whether a list
   view or single-camera-per-page view suits the smaller screen better.
3. **Verify PSRAM availability.** `platformio.ini` currently sets
   `-DBOARD_HAS_PSRAM` and `board_build.arduino.memory_type = qio_opi`.
   If the Cardputer ADV's module genuinely has no PSRAM, JPEG frame buffers
   (which can be tens of KB each) need to live in regular SRAM instead —
   check `src/net/Mjpeg.cpp` and `src/net/Livecams.cpp` for `malloc`/buffer
   sizing assumptions and confirm they don't silently rely on PSRAM's larger
   heap.
4. **No touch driver needed** — `src/hw/Touch.cpp`/`Touch.h` (the whole
   FT6336 I2C touch implementation) is CYD-specific and should be dropped
   entirely for this port, replaced with keyboard input reading (M5Stack
   provides a keyboard library/matrix scan for Cardputer; check M5Unified).

## 4. Firmware architecture (what's portable, what's not)

Entry point: `src/main.cpp` (634 lines). Key structure:

```cpp
enum class Screen { LiveCategories, LiveGrid, LiveViewer };
```

- **LiveCategories**: paginated text list of camera categories (Africa,
  Bears, Birds, Kittens & Puppies, Ocean, PNW, Scenic World). Uses a shared
  `drawPaddedList()`/`listHitTest()` helper (padded rows, big touch targets,
  pagination) — the padding-for-touch-targets part is moot on a keyboard,
  but the list-rendering/pagination logic itself is reusable if you swap the
  hit-test for a keyboard-driven selected-index cursor instead.
- **LiveGrid**: 2×2 (`kPerPage = 4`) paginated thumbnail grid per category.
  Each cell shows a cached JPEG thumbnail (see §5) with the camera title
  overlaid in a bottom strip, or falls back to word-wrapped title-only text
  (`drawWrappedText()`) if the thumbnail fetch fails.
- **LiveViewer**: full-screen ~6fps MJPEG playback of one camera, with a
  black title-bar strip at the bottom showing the camera name.

**Reusable as-is (hardware-independent logic):**
- `src/net/Livecams.h/.cpp` — relay API client (`fetchList()` parses
  `/cameras` JSON via ArduinoJson, `fetchJpeg()` fetches a thumbnail). Pure
  `WiFiClient` HTTP, no display/touch coupling.
- `src/net/Mjpeg.h/.cpp` — persistent MJPEG client (`begin()`, `nextFrame()`,
  `dataAvailable()` non-blocking gate, `end()`). Also pure network code.
- `src/hw/Wifi.h/.cpp` — trivial WiFi connect helper, board-agnostic.
- The category/roster data model and the relay itself (§5) — completely
  unchanged by the client hardware.

**Needs a rewrite for Cardputer:**
- `src/hw/Display.h` — the whole `LGFX` panel class is CYD-specific
  (ILI9341, specific pins, `cfg.invert = true` requirement, etc.). Needs a
  new panel config for Cardputer ADV's actual display.
- `src/hw/Touch.h/.cpp` — drop, replace with keyboard input.
- All touch-hit-testing and tap-based navigation in `main.cpp` — replace
  with keyboard-driven cursor/selection state.
- Grid/list layout constants (`kCellW`, `kCellH`, `kPerPage`,
  `kListTopPad`, etc.) — re-tune for 240×135.
- The `drawJpg` scale-to-fit calls (`0.0f, 0.0f, middle_center` — see §7
  gotcha) will need their target box dimensions updated for the new screen
  size, but the *technique* (explicit `0.0f, 0.0f` scale params) is still
  required and still correct on any LovyanGFX-driven panel.
- Power management (`enterSoftSleep`/`enterDeepSleep`, BOOT-button wake) is
  CYD-board-pin-specific; Cardputer's sleep/wake and button layout differ
  entirely (and it has a battery + IMU, so a smarter power model — e.g.
  IMU-based wake-on-pickup — might be worth considering, though that's a
  new feature, not a straight port).

## 5. The relay (host-side, no porting needed — read this to understand what the board talks to)

Lives in `tools/livecam-relay/` in this repo: `relay_server.py` (396 lines),
`cameras.py` (319 lines, the roster), `livecam-relay.service` (systemd
unit), `README.md` (deploy instructions).

**Deployment:** runs as a systemd service (`livecam-relay.service`) on
`sean@192.168.0.168` (a Linux Mint laptop on the LAN, hostname `DudeDell15`
— NOT the `192.168.0.152` Raspberry Pi, which runs Home Assistant OS with no
normal shell). SSH/sudo creds: `sean`/`robberts`. This session deployed via
a Python `paramiko` SSH client (`sshpass`/interactive `scp` aren't
available in this dev sandbox) — `sftp.put()` the changed file, then
`sudo systemctl restart livecam-relay`. `yt-dlp` (a fresh standalone build;
the distro's apt version was too old for YouTube extraction) and `ffmpeg`
both live at `~/.local/bin/` and `/usr/bin/ffmpeg` respectively — **ffmpeg
is `/usr/bin/ffmpeg` (installed via `apt install ffmpeg`), not the static
build**, because the static johnvansickle.com build crashed (SIGSEGV) on
two separate, unrelated inputs (see §7). A JS runtime (`deno`, installed at
`~/.deno/bin/deno`) was also installed for yt-dlp's YouTube extraction,
though it did not change the outcome for the specific flaky-camera
investigation underway at handoff time (see §8).

**API surface the board consumes** (all plain HTTP, port 8090):
- `GET /cameras` → JSON `{"categories": [...], "cameras": [{"id","title","category"}, ...]}`
- `GET /live/<id>.jpg` → single cached JPEG thumbnail, returns instantly if
  cached (see below), else does a cold pull (up to ~15s) or 503s
- `GET /live/<id>.mjpg` → persistent `multipart/x-mixed-replace` MJPEG stream

**Architecture inside `relay_server.py`:**
- `CameraSource` (one per camera): on-demand — only pulls while a viewer is
  active, via `touch()`/`IDLE_TIMEOUT_S = 90`. A background `_url_warmer`
  thread keeps every camera's HLS URL pre-resolved on a rolling cycle
  regardless of viewer activity, cutting cold-start latency dramatically.
- **Persistent per-camera ffmpeg decoder**: one long-lived `ffmpeg`
  process per active camera (not one-per-HLS-segment — that was an earlier,
  replaced design). A writer thread feeds HLS segment bytes into its stdin
  back-to-back; a reader thread splits the continuous MJPEG stdout into
  frames on JPEG SOI markers and paces publishing to `TARGET_FPS` (currently
  **6fps** — see §7 for why it's not higher).
- **Non-YouTube HLS support**: `CameraSource` accepts an optional static
  `"hls_url"` cfg key (instead of `"youtube_url"`+`"format"`) for cameras
  hosted elsewhere (used for Portland's Pioneer Courthouse Square cam,
  hosted on IPCamLive, found by grepping the embedding page's HTML for
  `<iframe`). `_new_segment_urls()` batch-fetches every newly-listed segment
  per playlist poll (not one at a time) and resolves segment paths with
  `urljoin` to handle both YouTube's always-absolute URLs and other
  providers' relative ones.
- **Thumbnail caching**: `CameraSource.frame` is *never* cleared when a
  camera idles out — the last frame it ever produced stays cached
  indefinitely and `/live/<id>.jpg` serves it instantly, while `touch()`
  still restarts the background puller to freshen it for next time. This
  is what makes grid-thumbnail browsing feel fast on repeat visits.
- **Per-camera `"scale"` cfg key**: caps resolution for non-YouTube sources
  that don't offer a low-res tier (YouTube sources already pick a ≤640px
  itag). Used for the IPCamLive cam, which is native 1920×1080.
- **`MAX_SEGMENTS_PER_BATCH = 8`**: defensive cap on the batch-fetch above,
  added after a real bug (see §8) where an ended-broadcast's giant
  archived-VOD-style manifest (thousands of segments) caused a runaway
  fetch that hung a camera's puller thread forever with no error.

**Roster sourcing ethics — this is a hard constraint, not a preference:**
Every camera must be from an **officially-operated, intentionally-public
source** (explore.org, Cornell Lab of Ornithology, EarthCam, government/
tourism bureaus, news stations, etc.) or an **individually-operated but
knowingly/intentionally public** stream (e.g. a homeowner's nest-box cam
livestream). **Never** build tooling around scraped camera feeds,
default/leaked credentials, or aggregators of other people's private
cameras, even if the user says it's their own risk to take. This was an
explicit, firm line established early in the project and should be treated
as non-negotiable in the port too if the roster is extended further.

## 6. Camera roster snapshot (at handoff time)

113 cameras across 7 categories: Africa, Bears, Birds, Kittens & Puppies,
Ocean, PNW, Scenic World. Full list with provenance comments is in
`tools/livecam-relay/cameras.py`'s header docstring and inline comments —
read that file directly rather than trusting a stale summary here, since
the roster changes frequently. Two known-flaky PNW entries at handoff time
are being actively investigated (§8).

## 7. Hard-won gotchas (read before re-deriving any of these)

- **The ESP32-S3 has no H.264/video decoder** — JPEG only. Don't try to run
  ffmpeg/H.264 decode on-chip; this was already tried and ruled out (§2).
- **LovyanGFX's `drawJpg(data, len, x, y, w, h)` does NOT scale-to-fit by
  default** — it decodes at native resolution and clips to the box. Must
  explicitly pass `scale_x=0.0f, scale_y=0.0f` (and optionally
  `middle_center` as the trailing `datum_t`) to get real scale-to-fit
  centered behavior: full call is
  `drawJpg(buf, len, x, y, w, h, 0, 0, 0.0f, 0.0f, middle_center)`.
- **Scale-to-fit can leave letterbox bars** (black bars where the source
  aspect ratio doesn't match the target box) that a `drawJpg` call never
  touches — any text/UI drawn underneath before the image loads will stay
  visible forever unless explicitly cleared once the first real frame
  lands (see `g_mjpegFirstFrameDrawn` pattern in `main.cpp`).
- **Two sequential draw calls (image, then an overlay like a title bar) are
  visibly non-atomic** — real frame pixels can flash through in the gap.
  Constrain the first draw's box so it physically can't touch the region
  the second draw owns, rather than relying on draw order/speed.
- **A blocking network read in the main loop starves touch/input
  polling.** Always gate a blocking read behind a cheap non-blocking
  "is data ready" check (`WiFiClient::available() > 0`) so every loop
  iteration stays fast and input gets polled every cycle regardless of
  network timing. This applies just as much to keyboard polling on
  Cardputer as it did to touch polling on CYD.
- **GPIO0/BOOT-button state can't be trusted at cold boot** — checking it in
  `setup()` races the ROM bootloader's own download-mode strap check; poll
  it in `loop()` after the app is already running instead. (May not be
  relevant to Cardputer's button layout — verify before assuming.)
- **ffmpeg's static (johnvansickle.com) build crashes (SIGSEGV) on more
  than just HTTPS input** — also crashed on a legitimate, non-corrupt
  MPEG-TS segment from a non-YouTube source. Fixed by switching to the
  distro-packaged `ffmpeg` (`apt install ffmpeg`). This is relay/server-side
  only, irrelevant to the ESP32 client, but keep it in mind if the relay is
  ever redeployed to a different host.
- **A background "URL warmer" thread competes with live on-demand requests
  for the same resource during its first pass** — right after a relay
  restart there's a ~10min window where cold fetches can be briefly
  *slower* than normal, not faster. Don't judge relay performance from
  measurements taken right after a restart.
- **YouTube serves a giant DVR-style archived-VOD manifest (thousands of
  segments) once a "live" stream has actually ended, instead of erroring** —
  looks resolvable via `yt-dlp -F`/`-g`, but breaks any consumer built
  around a small rolling live window. Detect this by checking whether
  formats show a `~`-prefixed *estimated* size (genuinely live) vs. an
  *exact* fixed size (ended/archived) in `yt-dlp -F` output, or by checking
  the channel's `/live` endpoint directly for "not currently live".
- **Small/hobbyist YouTube live channels (not major broadcasters) appear to
  restart under a brand-new video ID periodically** rather than running one
  indefinitely-persistent stream — a camera that worked when added can
  quietly become a dead/archived video weeks or even hours later. This
  bit us three times in one session (Space Needle, PDX/Mt.Hood, Cannon
  Beach — see §8). If a camera goes stale, re-check the source channel's
  current `/live` or `/streams` tab rather than assuming the code is
  broken.

## 8. Open issue at handoff time (unresolved, needs the user's input)

Two PNW roster entries — `portland-pdx-mt-hood` (PDX & Mt. Hood cam) and
`cannon-beach-kgw` (Cannon Beach/Haystack Rock, via KGW news) — are timing
out on the relay (503 after ~15s) despite having previously been verified
live. Investigation found hard technical evidence both specific video IDs
are now YouTube "postLiveDvr" archives (ended broadcasts): every format in
`yt-dlp -F` shows an *exact* file size (only true for finished videos, a
truly live stream shows `~`-estimated sizes), repeated fresh resolves
return byte-identical content, and the PDX channel's `/live` endpoint
explicitly says "The channel is not currently live."

**However**, the user reports watching both of these live and in motion,
right now, directly in a browser — no loop, genuinely live. This directly
contradicts the technical findings above. The most likely explanation
(unconfirmed) is the same pattern documented in §7's last bullet: these
channels restarted under a new video ID, and the user is watching that new
ID while the roster still points at the old, now-ended one (this is
exactly what had already happened with a since-removed `seattle-space-needle`
entry earlier in the same session). **The user was asked to grab the actual
video ID/URL from their browser for these two cams so the roster could be
corrected, and that answer had not yet arrived when this session ended.**
Whoever picks this up next should either wait for that answer or re-run the
same diagnostic (`yt-dlp -F <url>` for exact-vs-estimated file sizes, plus
`yt-dlp -F <channel>/live` for current live status) against fresh URLs the
user provides.

## 9. Suggested porting checklist

1. Confirm exact Cardputer ADV panel driver/pins and PSRAM availability
   (don't guess — check M5Unified/M5GFX source or M5Stack's own docs for
   the canonical LovyanGFX config, the way `docs/HOSYOND_ESP32S3_TARGET.md`
   did for this board).
2. Write a new `src/hw/Display.h` for Cardputer's panel.
3. Replace `src/hw/Touch.*` with a keyboard input module (M5Stack likely
   has a keyboard scan library to build on).
4. Redesign navigation in `main.cpp`: category list → grid → viewer, all
   keyboard-driven (arrow keys + Enter + Esc, or similar). The screen enum
   and overall three-screen structure can probably stay.
5. Re-tune all layout constants for 240×135 — likely fewer thumbnails per
   page, or a list-based grid alternative, given how small that screen is
   compared to 320×240.
6. Verify JPEG frame buffer sizing works without PSRAM if confirmed absent.
7. Leave `src/net/*` and the relay entirely alone — point the new firmware
   at the same relay host/port (`kRelayHost`/`kRelayPort` in
   `src/net/Livecams.h`) and it should work unchanged.
8. Power management will need a full rethink given the battery + IMU —
   treat as a new feature, not a like-for-like port of the CYD board's
   soft/deep-sleep timers.
9. `src/Secrets.h.example` still references a leftover `WINDY_API_KEY` from
   the removed Windy weather-webcam feature (deleted earlier this session)
   — harmless but worth cleaning up if touching that file anyway.

## 10. Where to find more detail

- `README.md` (repo root) — current one-paragraph project summary.
- `docs/HOSYOND_ESP32S3_TARGET.md` — full hardware reference for the
  *current* (CYD) board; not applicable to Cardputer except as a model for
  how thoroughly a new target's hardware reference doc should be written.
- `tools/livecam-relay/README.md` — relay deployment/redeploy instructions
  and camera-roster-refresh instructions in more detail than §5 above.
- `tools/livecam-relay/cameras.py` — the living roster, with per-section
  provenance comments; always more current than any summary of it.
- Git log on `main` — every fix mentioned in §7 has a corresponding commit
  with a detailed message explaining root cause and verification; worth
  reading the actual diffs for anything you're about to touch.
