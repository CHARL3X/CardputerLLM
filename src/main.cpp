// CardputerLLM phase 6+7 cram:
//   - Conversation history (cap 20, auto-prune oldest pair)
//   - System prompt from /CardputerLLM/system.txt or built-in default
//   - JSON persistence to /CardputerLLM/chats/<timestamp>.json after each
//     completed exchange
//   - Model picker (Fn+M) over the curated trio (GPT-5, Sonnet 4.5,
//     Gemini 2.5 Pro), switching clears chat with confirm
//   - New chat (Fn+N)
//   - Scroll up/down (Fn+',' or Fn+';' up; Fn+'.' or Fn+'/' down) with
//     hold-to-repeat
//   - Hold-to-repeat for backspace
//   - Status row top-right with model label, no slabby chrome
//   - NTP sync after WiFi for timestamped chat filenames

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>
#include <time.h>

#include "storage/sd_config.h"
#include "storage/settings.h"
#include "ui/chat_screen.h"

using namespace ESPAI;

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;
static constexpr int kHeaderH = 14;
static constexpr int kFooterH = 14;

static constexpr const char* kBaseUrl = "https://openrouter.ai/api/v1/chat/completions";

static const std::vector<ModelChoice> kModels = {
    {"openai/gpt-5",                "gpt-5"},
    {"anthropic/claude-sonnet-4.5", "sonnet-4.5"},
    {"google/gemini-2.5-pro",       "gemini-2.5"},
};
static constexpr int kInitialModelIdx = 1; // start on sonnet 4.5

static constexpr const char* kDefaultSystemPrompt =
    "You are running on a Cardputer, a credit-card-sized pocket "
    "terminal with a 30-column display and a real QWERTY keyboard. "
    "Be concise. Plain text only, no markdown.";

static void bootHeader(const String& msg, uint16_t bg = TFT_NAVY) {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kHeaderH, bg);
    M5Cardputer.Display.setTextColor(TFT_WHITE, bg);
    M5Cardputer.Display.setCursor(4, 3);
    M5Cardputer.Display.print(msg);
}
static void bootFooter(const String& msg, uint16_t bg = TFT_DARKGREY) {
    M5Cardputer.Display.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, bg);
    M5Cardputer.Display.setTextColor(TFT_BLACK, bg);
    M5Cardputer.Display.setCursor(4, kScreenH - kFooterH + 3);
    M5Cardputer.Display.print(msg);
}

static bool tryWiFi(const String& ssid, const String& pw, uint32_t timeoutMs) {
    bootHeader(String("wifi: ") + ssid);
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
            bootHeader(String("wifi ok: ") + ip);
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
    bootHeader(head, TFT_MAROON);
    bootFooter(foot, TFT_RED);
    Serial.printf("[halt] %s | %s\n", head.c_str(), foot.c_str());
    while (true) delay(1000);
}

static void syncTime() {
    bootFooter("ntp sync...");
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    uint32_t t0 = millis();
    time_t now;
    while ((now = time(nullptr)) < 1577836800 && (millis() - t0) < 5000) {
        delay(200);
    }
    if (now < 1577836800) {
        Serial.println("[ntp] not synced; chat filenames will be boot-millis");
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
    Serial.println("[boot] cardputerllm phase 6+7 (history, picker, scroll)");

    bootHeader("boot");
    bootFooter("mounting sd...");

    if (!sdcfg::begin()) halt("sd: mount failed", "insert sd card");
    sdcfg::ensureChatsDir();

    String apiKey = sdcfg::loadOpenRouterKey();
    if (apiKey.length() == 0) halt("missing key file", "/CardputerLLM/openrouter.txt");

    auto creds = sdcfg::loadWiFi();
    if (creds.empty()) halt("missing wifi.txt", "/CardputerLLM/wifi.txt");
    Serial.printf("[sd] %u wifi candidates\n", (unsigned)creds.size());

    if (!connectWiFiFromList(creds)) halt("wifi: all failed", "check wifi.txt");

    syncTime();

    String sys = sdcfg::loadSystemPrompt();
    if (sys.length() == 0) {
        sys = kDefaultSystemPrompt;
        Serial.println("[sys] using built-in default prompt");
    } else {
        Serial.printf("[sys] loaded /CardputerLLM/system.txt (%u chars)\n",
                      (unsigned)sys.length());
    }

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
