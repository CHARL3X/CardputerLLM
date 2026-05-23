// Adapted from CardputerLLM/src/setup/wifi_setup.h. No interface changes;
// path comments reference /Cardputer/wifi.txt.
#pragma once
#include <Arduino.h>
#include <vector>
#include "../storage/sd_config.h"

namespace wifi_setup {

struct ScanResult {
    String ssid;
    int    rssi;
    bool   secured;
};

// Synchronous WiFi scan. Returns visible networks deduped by SSID,
// strongest signal first. Shows a "scanning" screen during.
std::vector<ScanResult> scanNow(bool showUI = true);

// Filters `saved` down to those that appear in `visible`, ordered by the
// visible RSSI (strongest first). Saved entries with no matching scan
// result are dropped so we don't waste 12s waiting for ghosts.
std::vector<WiFiCred> filterByVisibility(
    const std::vector<WiFiCred>& saved,
    const std::vector<ScanResult>& visible);

// Interactive WiFi onboarding: scan, pick from list, enter password,
// connect. On success, the (ssid, password) pair is appended to
// /Cardputer/wifi.txt so future boots remember it.
//
// allowCancel=true:  Backspace from the scan list returns false.
//                    Used when invoked from the in-app menu.
// allowCancel=false: cannot cancel; loops scan/pick until something
//                    associates or the device is power-cycled. Used at
//                    boot when WiFi is a hard prerequisite.
bool run(bool allowCancel = false);

} // namespace wifi_setup
