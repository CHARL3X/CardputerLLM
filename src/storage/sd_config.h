// Shared SD config for the merged Cardputer firmware.
// Single namespace /Cardputer/ -- LLM and Verbatim modes share
// credentials but keep app-specific data under chats/ and notes/.
#pragma once
#include <Arduino.h>
#include <vector>

struct WiFiCred {
    String ssid;
    String password;
};

namespace sdcfg {

bool begin();

String loadOpenRouterKey();
std::vector<WiFiCred> loadWiFi();
String loadSystemPrompt();

// Ensures /Cardputer/, chats/, notes/, snaps/ exist. Idempotent.
void ensureDirs();

bool saveApiKey(const String& key);
bool appendWiFiCred(const String& ssid, const String& password);

// Counts .md files under /Cardputer/notes/ -- used by Verbatim mode's
// NotesScreen for the status row count.
int countNotes();

} // namespace sdcfg
