// CardputerLLM phase 5: minimal chat UI.
//
// Boot: SD mount, load credentials, connect WiFi, build provider, hand
// off to ChatScreen. ChatScreen owns input + render + streaming send.

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>

#include "storage/sd_config.h"
#include "ui/chat_screen.h"

using namespace ESPAI;

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;
static constexpr int kHeaderH = 14;
static constexpr int kFooterH = 14;

static constexpr const char* kBaseUrl   = "https://openrouter.ai/api/v1/chat/completions";
static constexpr const char* kTestModel = "openai/gpt-4o-mini";

// Boot screen helpers. Used only during the SD+wifi+provider setup so the
// user can see progress and any failure cause. ChatScreen takes over the
// display once we're ready to chat.

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

static OpenAICompatibleProvider* makeProvider(const String& apiKey) {
    OpenAICompatibleConfig cfg;
    cfg.name    = "OpenRouter";
    cfg.baseUrl = kBaseUrl;
    cfg.apiKey  = apiKey;
    cfg.model   = kTestModel;
    return new OpenAICompatibleProvider(cfg);
}

static void halt(const String& head, const String& foot) {
    bootHeader(head, TFT_MAROON);
    bootFooter(foot, TFT_RED);
    Serial.printf("[halt] %s | %s\n", head.c_str(), foot.c_str());
    while (true) delay(1000);
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
    Serial.println("[boot] cardputerllm phase 5 (chat ui)");

    bootHeader("boot");
    bootFooter("mounting sd...");

    if (!sdcfg::begin()) {
        halt("sd: mount failed", "insert sd card");
    }

    String apiKey = sdcfg::loadOpenRouterKey();
    if (apiKey.length() == 0) {
        halt("missing key file", "/CardputerLLM/openrouter.txt");
    }

    auto creds = sdcfg::loadWiFi();
    if (creds.empty()) {
        halt("missing wifi.txt", "/CardputerLLM/wifi.txt");
    }
    Serial.printf("[sd] %u wifi candidates loaded\n", (unsigned)creds.size());

    if (!connectWiFiFromList(creds)) {
        halt("wifi: all failed", "check wifi.txt");
    }

    auto* ai = makeProvider(apiKey);
    g_chat = new ChatScreen(ai);
    g_chat->begin();
    Serial.println("[chat] ready");
}

void loop() {
    g_chat->tick();
}
