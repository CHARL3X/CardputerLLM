#include "settings.h"
#include <Preferences.h>

namespace {
Preferences prefs;
constexpr const char* kNs = "cardputerllm";
// NVS keys must be <= 15 chars.
constexpr const char* kHistoryDepthKey = "histdepth";
constexpr const char* kWelcomedKey     = "welcomed";
constexpr const char* kBootSoundKey    = "bootsound";
} // namespace

namespace settings {

void begin() {
    prefs.begin(kNs, false);
}

int historyDepth() {
    int v = prefs.getInt(kHistoryDepthKey, 20);
    if (v < 2 || v > 200) v = 20;
    return v;
}

void setHistoryDepth(int v) {
    if (v < 2)   v = 2;
    if (v > 200) v = 200;
    prefs.putInt(kHistoryDepthKey, v);
}

bool welcomed() { return prefs.getBool(kWelcomedKey, false); }
void setWelcomed(bool v) { prefs.putBool(kWelcomedKey, v); }

bool bootSound() { return prefs.getBool(kBootSoundKey, true); }
void setBootSound(bool v) { prefs.putBool(kBootSoundKey, v); }

} // namespace settings
