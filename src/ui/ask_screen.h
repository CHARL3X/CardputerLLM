// Ask mode: chat over a context of selected voice memos.
//
// Architecture cloned from CardputerLLM/src/ui/chat_screen.{h,cpp} but
// trimmed for ask-mode's needs:
//   - No SD/NVS persistence (conversation is local; exit clears it)
//   - No depth picker (single-session, no message-count cap to expose)
//   - Status row shows "N memos . <model>" instead of just the model
//   - Reduced slash command set: /clear, /help, /diag
//
// Flow: ask_screen::run() builds the system context from the list of
// note filenames, shows a token preflight, then spins up the chat UI
// with an ESPAI OpenAICompatibleProvider for the duration. Returns
// when the user backs out (Backspace from empty input or backtick/tilde).
#pragma once
#include <Arduino.h>
#include <vector>

namespace ask_screen {

void run(const String& apiKey, const std::vector<String>& selectedNotes);

} // namespace ask_screen
