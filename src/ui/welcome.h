// Adapted from CardputerLLM/src/ui/welcome.h.
#pragma once

namespace welcome {

// One-shot first-run welcome. Direct-draws a friendly intro screen and
// blocks until any key. NVS flag handling is the caller's responsibility.
void run();

} // namespace welcome
