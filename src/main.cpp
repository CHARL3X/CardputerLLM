// CardputerLLM boot orchestration with onboarding fallbacks.
//
// Backwards compatible: if /CardputerLLM/wifi.txt and openrouter.txt
// already have valid contents, boot is identical to prior cycles.
// First-run path (or after wifi.txt is empty / all entries fail) drops
// into wifi_setup; missing openrouter.txt drops into key_setup (web form).

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>
#include <time.h>

#include "storage/sd_config.h"
#include "storage/settings.h"
#include "setup/wifi_setup.h"
#include "setup/key_setup.h"
#include "ui/boot_ui.h"
#include "ui/chat_screen.h"
#include "ui/splash.h"

using namespace ESPAI;

static constexpr const char* kBaseUrl = "https://openrouter.ai/api/v1/chat/completions";

static const std::vector<ModelChoice> kModels = {
    {"openai/gpt-5",                "gpt-5"},
    {"anthropic/claude-sonnet-4.5", "sonnet-4.5"},
    {"google/gemini-2.5-pro",       "gemini-2.5"},
};
static constexpr int kInitialModelIdx = 1;

static constexpr const char* kDefaultSystemPrompt =
    "You are running on a Cardputer, a credit-card-sized pocket "
    "terminal with a 30-column display and a real QWERTY keyboard. "
    "Be concise. Plain text only, no markdown.";

static bool tryWiFi(const String& ssid, const String& pw, uint32_t timeoutMs) {
    boot_ui::header(String("wifi: ") + ssid);
    Serial.printf("[wifi] try '%s'\n", ssid.c_str());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pw.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool connectWiFiFromList(const std::vector<WiFiCred>& creds) {
    for (auto& c : creds) {
        if (tryWiFi(c.ssid, c.password, 12000)) {
            String ip = WiFi.localIP().toString();
            boot_ui::header(String("wifi ok: ") + ip);
            Serial.printf("[wifi] connected on '%s' ip=%s\n",
                          c.ssid.c_str(), ip.c_str());
            return true;
        }
        Serial.printf("[wifi] timeout on '%s'\n", c.ssid.c_str());
    }
    return false;
}

static OpenAICompatibleProvider* makeProvider(const String& apiKey,
                                              const char* modelSlug) {
    OpenAICompatibleConfig cfg;
    cfg.name    = "OpenRouter";
    cfg.baseUrl = kBaseUrl;
    cfg.apiKey  = apiKey;
    cfg.model   = modelSlug;
    return new OpenAICompatibleProvider(cfg);
}

static void halt(const String& head, const String& foot) {
    boot_ui::clear();
    boot_ui::header(head, 0x8800);  // dark red
    boot_ui::footer(foot, 0xF800);
    Serial.printf("[halt] %s | %s\n", head.c_str(), foot.c_str());
    while (true) delay(1000);
}

static void syncTime() {
    boot_ui::footer("ntp sync...");
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    uint32_t t0 = millis();
    time_t now;
    while ((now = time(nullptr)) < 1577836800 && (millis() - t0) < 5000) {
        delay(200);
    }
    if (now < 1577836800) {
        Serial.println("[ntp] not synced; filenames will be boot-millis");
        return;
    }
    Serial.printf("[ntp] synced: %lu\n", (unsigned long)now);
}

static ChatScreen* g_chat = nullptr;

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    uint32_t serialDeadline = millis() + 2000;
    while (!Serial && millis() < serialDeadline) delay(10);
    Serial.println();
    Serial.println("[boot] cardputerllm phase 8.3 (splash + empty state)");

    splash::run();

    boot_ui::header("boot");
    boot_ui::footer("mounting sd...");

    if (!sdcfg::begin()) halt("sd: mount failed", "insert sd card");
    sdcfg::ensureChatsDir();

    // ---- WiFi: try existing creds first, fall back to setup UI ----
    auto creds = sdcfg::loadWiFi();
    bool connected = false;
    if (!creds.empty()) {
        Serial.printf("[sd] %u wifi candidates\n", (unsigned)creds.size());
        connected = connectWiFiFromList(creds);
    } else {
        Serial.println("[sd] no wifi.txt or empty");
    }
    if (!connected) {
        Serial.println("[boot] entering wifi setup");
        if (!wifi_setup::run(/*allowCancel=*/false)) {
            halt("wifi setup cancelled", "");
        }
    }

    syncTime();

    // ---- System prompt ----
    String sys = sdcfg::loadSystemPrompt();
    if (sys.length() == 0) {
        sys = kDefaultSystemPrompt;
        Serial.println("[sys] using built-in default prompt");
    } else {
        Serial.printf("[sys] loaded /CardputerLLM/system.txt (%u chars)\n",
                      (unsigned)sys.length());
    }

    // ---- API key: load from SD, or run web setup ----
    String apiKey = sdcfg::loadOpenRouterKey();
    if (apiKey.length() == 0) {
        Serial.println("[boot] no api key; entering web setup");
        if (!key_setup::run(/*allowCancel=*/false)) {
            halt("key setup cancelled", "");
        }
        apiKey = sdcfg::loadOpenRouterKey();
        if (apiKey.length() == 0) halt("key save inconsistent", "");
    }

    // ---- Settings + chat ----
    settings::begin();
    int depth = settings::historyDepth();
    Serial.printf("[settings] history depth: %d\n", depth);

    auto* ai = makeProvider(apiKey, kModels[kInitialModelIdx].slug);
    g_chat = new ChatScreen(ai, sys, kModels, kInitialModelIdx, depth);
    g_chat->begin();
    Serial.println("[chat] ready");
}

void loop() {
    g_chat->tick();
}
