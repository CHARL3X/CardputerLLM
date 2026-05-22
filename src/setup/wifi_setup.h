#pragma once
#include <Arduino.h>

namespace wifi_setup {

// Interactive WiFi onboarding: scan, pick from list, enter password,
// connect. On success, the (ssid, password) pair is appended to
// /CardputerLLM/wifi.txt so future boots remember it.
//
// allowCancel=true:  Backspace from the scan list returns false.
//                    Used when invoked from the in-app menu.
// allowCancel=false: cannot cancel; loops scan/pick until something
//                    associates or the device is power-cycled. Used at
//                    boot when WiFi is a hard prerequisite.
bool run(bool allowCancel = false);

} // namespace wifi_setup
