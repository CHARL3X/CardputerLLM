#pragma once
#include <Arduino.h>

// Small NVS-backed settings store. Keys persist across reboots, survive
// reflash, and are wiped only when the user explicitly clears NVS via
// Launcher's CFG menu.

namespace settings {

void begin();

int  historyDepth();        // default 20 (10 user/assistant pairs)
void setHistoryDepth(int v);

} // namespace settings
