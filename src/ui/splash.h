#pragma once
#include <Arduino.h>

namespace splash {

// Cold-boot identity sequence. Blocking, ~1.6s. Plays once before any
// other UI runs. Does not depend on SD or network.
void run();

} // namespace splash
