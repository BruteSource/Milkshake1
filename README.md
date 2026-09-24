# CYD-Milkshake

Firmware for a black CYD-variant board (ESP32-S3, ILI9341 display, FT6336
capacitive touch, SD, speaker). See `docs/HOSYOND_ESP32S3_TARGET.md` for the
full hardware reference.

Browses ~90 live nature cams (bears, elephants, reefs, owl nests, scenic
world landmarks, etc.) by category, in a paginated thumbnail grid, with a
full-screen viewer showing real ~2fps motion. The board can't decode video
itself, so a local relay (`tools/livecam-relay/`, see its README) transcodes
each YouTube Live source into plain MJPEG the board pulls directly over the
LAN. On-device touch calibration: hold the BOOT button 2s to run it.

## Build

```
pio run -t upload
pio device monitor
```
