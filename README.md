# CYD-Milkshake

Firmware for a black CYD-variant board (ESP32-S3, ILI9341 display, FT6336
capacitive touch, SD, speaker). See `docs/HOSYOND_ESP32S3_TARGET.md` for the
full hardware reference.

Currently a bring-up smoke test (serial + display + touch) — app goals TBD.

## Dev workflow

- Cloud Claude session: firmware code, repo management.
- Local Claude session (`claude remote-control` from this project folder):
  flashing, serial monitor, on-device testing.

## Build

```
pio run -t upload
pio device monitor
```
