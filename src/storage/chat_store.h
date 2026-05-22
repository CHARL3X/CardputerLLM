#pragma once
#include <Arduino.h>
#include <ESPAI.h>

namespace chatstore {

// Build a filename like "20260522T143321Z.json" from the current UTC time.
// If the clock hasn't been NTP-synced yet, falls back to millis-based name.
String newSessionFilename();

// Write the full conversation (including system prompt) to
// /CardputerLLM/chats/<filename>. Overwrites in place. Returns true on
// success, false on any write failure (file create, write, close).
bool saveSession(const String& filename,
                 const ESPAI::Conversation& conv,
                 const String& modelSlug);

} // namespace chatstore
