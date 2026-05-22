#pragma once
#include <Arduino.h>
#include <vector>

struct WiFiCred {
    String ssid;
    String password;
};

namespace sdcfg {

// Mount the SD card on the M5Cardputer pinout. Call once in setup.
// Returns false if the card is missing or the mount failed.
bool begin();

// Reads /openrouter.txt and returns the trimmed first line.
// Empty string if missing or empty.
String loadOpenRouterKey();

// Reads /wifi.txt as line pairs (ssid, password, ssid, password, ...).
// Lines starting with '#' and blank lines are ignored. Returns the list
// in file order; main code should try them in sequence.
std::vector<WiFiCred> loadWiFi();

} // namespace sdcfg
