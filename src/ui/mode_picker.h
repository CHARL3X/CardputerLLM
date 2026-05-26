// Boot-time mode picker.
//
// Shown after splash + onboarding + welcome (if first run). The user
// chooses between LLM chat mode and Verbatim voice-notes mode. The
// selection is the user's intent for THIS session; main.cpp records it
// to NVS via settings::setLastMode() so the next boot defaults to the
// same choice.
//
// Blocking. Returns 0 for LLM or 1 for Verbatim.
#pragma once
#include <Arduino.h>

namespace mode_picker {

constexpr uint8_t kLLM      = 0;
constexpr uint8_t kVerbatim = 1;
constexpr uint8_t kTetris   = 2;

uint8_t run(uint8_t defaultMode);

} // namespace mode_picker
