#pragma once
#include <Arduino.h>

// Small NVS-backed settings store. Keys persist across reboots, survive
// reflash, and are wiped only when the user explicitly clears NVS via
// Launcher's CFG menu.

namespace settings {

void begin();

int  historyDepth();        // default 20 (10 user/assistant pairs)
void setHistoryDepth(int v);

bool welcomed();            // true after the first-run welcome has been shown
void setWelcomed(bool v);

bool bootSound();           // play a boot tone on splash; default true
void setBootSound(bool v);

} // namespace settings
