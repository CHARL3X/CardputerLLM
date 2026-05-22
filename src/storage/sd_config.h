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

// Reads /CardputerLLM/system.txt and returns the trimmed contents. Empty
// string if the file is missing, in which case the caller should fall
// back to a built-in default.
String loadSystemPrompt();

// Ensures /CardputerLLM/chats/ exists. Idempotent.
void ensureChatsDir();

// Overwrites /CardputerLLM/openrouter.txt with the supplied key (single line).
// Returns false if SD write failed.
bool saveApiKey(const String& key);

// Appends an (ssid, password) pair to /CardputerLLM/wifi.txt. Creates the
// file if missing. Existing pairs are preserved; new pair is added at the
// end so it acts as a fallback to whatever was configured earlier.
bool appendWiFiCred(const String& ssid, const String& password);

} // namespace sdcfg
