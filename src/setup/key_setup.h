#pragma once
#include <Arduino.h>

namespace key_setup {

// Spins up a tiny HTTP server on port 80 so the user can paste their
// OpenRouter key from a real keyboard. On submit, the key is validated
// (`sk-or-` prefix), saved to /CardputerLLM/openrouter.txt, the server
// stops, and the function returns.
//
// The Cardputer screen shows the device's IP plus a brief instruction
// while the server is up.
//
// allowCancel=true:  Backspace on the Cardputer cancels and returns false.
//                    Used when invoked from the in-app menu.
// allowCancel=false: cannot cancel; runs until a valid key arrives or
//                    the device is power-cycled. Used at boot when an
//                    API key is a hard prerequisite.
bool run(bool allowCancel = false);

} // namespace key_setup
