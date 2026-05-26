#include "settings.h"
#include <Preferences.h>

namespace {
Preferences prefs;
constexpr const char* kNs = "cardputer";

constexpr const char* kWelcomedKey   = "welcomed";
constexpr const char* kBootSoundKey  = "bootsound";
constexpr const char* kLastModeKey   = "last_mode";
constexpr const char* kHistDepthKey  = "histdepth";
constexpr const char* kAskDepthKey   = "askdepth";
constexpr const char* kTxModelKey    = "tx_model";
constexpr const char* kTitleModelKey = "title_model";

constexpr const char* kDefaultTxModel    = "openai/whisper-large-v3";
constexpr const char* kDefaultTitleModel = "google/gemini-2.5-flash";
} // namespace

namespace settings {

void begin() { prefs.begin(kNs, false); }

bool welcomed()           { return prefs.getBool(kWelcomedKey, false); }
void setWelcomed(bool v)  { prefs.putBool(kWelcomedKey, v); }

bool bootSound()          { return prefs.getBool(kBootSoundKey, true); }
void setBootSound(bool v) { prefs.putBool(kBootSoundKey, v); }

uint8_t lastMode() {
    uint8_t v = prefs.getUChar(kLastModeKey, 0);
    if (v > 2) v = 0;
    return v;
}
void setLastMode(uint8_t v) { prefs.putUChar(kLastModeKey, v); }

uint32_t tetrisHighScore() {
    return prefs.getUInt("tetris_hs", 0);
}
void setTetrisHighScore(uint32_t v) {
    prefs.putUInt("tetris_hs", v);
}

int historyDepth() {
    int v = prefs.getInt(kHistDepthKey, 20);
    if (v < 2 || v > 200) v = 20;
    return v;
}
void setHistoryDepth(int v) {
    if (v < 2)   v = 2;
    if (v > 200) v = 200;
    prefs.putInt(kHistDepthKey, v);
}

int askDepth() {
    int v = prefs.getInt(kAskDepthKey, 10);
    if (v < 2 || v > 200) v = 10;
    return v;
}
void setAskDepth(int v) {
    if (v < 2)   v = 2;
    if (v > 200) v = 200;
    prefs.putInt(kAskDepthKey, v);
}

String txModel() {
    return prefs.getString(kTxModelKey, kDefaultTxModel);
}
void setTxModel(const String& v) { prefs.putString(kTxModelKey, v); }

String titleModel() {
    return prefs.getString(kTitleModelKey, kDefaultTitleModel);
}
void setTitleModel(const String& v) { prefs.putString(kTitleModelKey, v); }

} // namespace settings
