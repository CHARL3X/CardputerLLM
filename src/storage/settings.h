// Merged NVS-backed settings for the Cardputer firmware.
// Namespace "cardputer" -- not "cardputerllm" or "verbatim", to keep
// the unified firmware's NVS independent of either standalone build.
//
// Carries every key from both prior apps plus a `last_mode` for the
// boot-time picker. All NVS keys must be <= 15 chars.
#pragma once
#include <Arduino.h>

namespace settings {

void begin();

// ---- shared ----
bool welcomed();
void setWelcomed(bool v);

bool bootSound();
void setBootSound(bool v);

// last app picked: 0 = LLM, 1 = Verbatim
uint8_t lastMode();
void setLastMode(uint8_t v);

// ---- LLM mode ----
int  historyDepth();
void setHistoryDepth(int v);

// ---- Verbatim mode ----
int  askDepth();
void setAskDepth(int v);

String txModel();
void   setTxModel(const String& v);

String titleModel();
void   setTitleModel(const String& v);

} // namespace settings
