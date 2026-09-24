// FT6336 capacitive touch -- raw register reads. Library touch drivers
// misbehave on this panel (see docs/HOSYOND_ESP32S3_TARGET.md gotcha #7),
// so this reads FT6336 registers directly over I2C instead.
#pragma once
#include <stdint.h>

namespace touch {

// Reset pulse, I2C @ 400 kHz, threshold bump, polling INT mode.
void begin();

// Raw panel-frame sample, no calibration applied. false if no finger / a
// garbage 0x0FFF sample.
bool rawSample(int16_t* rx, int16_t* ry);

// "Finger down right now?" -- cheap check, no I2C read of coordinates.
bool isTouched();

// I2C bus scan, chip id/vendor id/G_MODE registers, and a 2s at-rest sample
// of raw TD_STATUS (touch point count) to tell a noisy/misconfigured sensor
// apart from a corrupted I2C read. Prints to Serial.
void diag();

}  // namespace touch
