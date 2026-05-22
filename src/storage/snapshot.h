#pragma once
#include <Arduino.h>

namespace snapshot {

// Read the full 240x135 display via readRect() and write it as a 24-bit
// BMP to /CardputerLLM/snaps/<timestamp>.bmp. Returns the filename on
// success, empty String on failure. On panels where readback returns
// garbage, the caller should sanity-check the output.
String captureToBMP();

} // namespace snapshot
