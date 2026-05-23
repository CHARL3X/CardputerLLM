// Cardputer merged firmware boot orchestration.
//
// Flow: splash -> boot log (SD, WiFi, NTP, system, key) -> first-run
// welcome -> mic warm-up cycle -> mode picker -> either ChatScreen
// (LLM) or NotesScreen (Verbatim) takes over the loop().
//
// Shared SD namespace /Cardputer/ for credentials + system prompt;
// app-specific data lives in chats/ (LLM) and notes/ (Verbatim) under
// the same root.

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>
#include <time.h>

#include "storage/sd_config.h"
#include "storage/settings.h"
#include "setup/wifi_setup.h"
#include "setup/key_setup.h"
#include "ui/boot_ui.h"
#include "ui/splash.h"
#include "ui/welcome.h"
#include "ui/mode_picker.h"
#include "ui/chat_screen.h"
#include "ui/notes_screen.h"

using namespace ESPAI;

// Override the framework's weak symbol so the loopTask stack survives
// mbedtls TLS handshakes. See cardputer-tls-loop-stack memory.
uint32_t getArduinoLoopTaskStackSize(void) { return 24576; }

// ---------- LLM mode constants ----------

static constexpr const char* kBaseUrl =
    "https://openrouter.ai/api/v1/chat/completions";

static const std::vector<ModelChoice> kLlmModels = {
    {"openai/gpt-5",                "gpt-5"},
    {"anthropic/claude-sonnet-4.5", "sonnet-4.5"},
    {"google/gemini-2.5-pro",       "gemini-2.5"},
};
static constexpr int kInitialLlmModelIdx = 1;

static constexpr const char* kFormatPrompt =
    "You are replying through a Cardputer: 240x135 px, 30 cols, monospace.\n"
    "Do NOT use markdown (**bold**, # heading, ```fences, links, etc).\n"
    "The device renders only these tags. Use them when they add visual\n"
    "clarity; plain prose is preferred for short conversational answers.\n"
    "Inline tags:\n"
    "  [h]hot[/h] [k]label[/k] [v]value[/v] [?]aside[/?]\n"
    "  [ok]success[/ok] [w]warn[/w] [!]error[/!]\n"
    "Block lines (each on its own line):\n"
    "  <<section title>>\n"
    "  ---  (horizontal divider)\n"
    "  - bulleted item\n"
    "  > quoted line\n"
    "  [bar:NN]  (renders a filled progress bar, NN=0..100)\n"
    "  ```lang ... ``` (multi-line code block; lines are NOT word-wrapped)\n";

static constexpr const char* kDefaultPersona =
    "You are concise. Aim for replies that fit in 7 lines of 30 columns "
    "when possible. Use the format tags above when they materially help.";

// ---------- shared boot helpers ----------

static bool tryWiFi(const String& ssid, const String& pw, uint32_t timeoutMs) {
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

static bool connectWiFiFromList(const std::vector<WiFiCred>& creds,
                                String* successSsid = nullptr) {
    for (auto& c : creds) {
        if (tryWiFi(c.ssid, c.password, 12000)) {
            String ip = WiFi.localIP().toString();
            Serial.printf("[wifi] connected on '%s' ip=%s\n",
                          c.ssid.c_str(), ip.c_str());
            if (successSsid) *successSsid = c.ssid;
            return true;
        }
        Serial.printf("[wifi] timeout on '%s'\n", c.ssid.c_str());
    }
    return false;
}

static void halt(const String& head, const String& foot) {
    boot_ui::clear();
    boot_ui::header(head, 0x8800);
    boot_ui::footer(foot, 0xF800);
    Serial.printf("[halt] %s | %s\n", head.c_str(), foot.c_str());
    while (true) delay(1000);
}

static bool syncTimeQuiet() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    uint32_t t0 = millis();
    time_t now;
    while ((now = time(nullptr)) < 1577836800 && (millis() - t0) < 5000) {
        delay(200);
    }
    return now >= 1577836800;
}

static OpenAICompatibleProvider* makeLlmProvider(const String& apiKey,
                                                 const char* modelSlug) {
    OpenAICompatibleConfig cfg;
    cfg.name    = "OpenRouter";
    cfg.baseUrl = kBaseUrl;
    cfg.apiKey  = apiKey;
    cfg.model   = modelSlug;
    return new OpenAICompatibleProvider(cfg);
}

// ---------- module state ----------

static ChatScreen*  g_chat  = nullptr;
static NotesScreen* g_notes = nullptr;

// ---------- setup / loop ----------

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    uint32_t serialDeadline = millis() + 2000;
    while (!Serial && millis() < serialDeadline) delay(10);
    Serial.println();
    Serial.println("[boot] cardputer v1.0 (merged: LLM + Verbatim)");

    settings::begin();
    splash::run();
    boot_ui::startLog();

    bool sdOk = sdcfg::begin();
    boot_ui::step("mounting sd", sdOk);
    if (!sdOk) halt("sd: mount failed", "insert sd card");
    sdcfg::ensureDirs();

    auto creds = sdcfg::loadWiFi();
    boot_ui::step("loaded wifi.txt", true,
                  String((unsigned)creds.size()) + " saved network(s)");

    String connectedSsid;
    bool   connected = false;
    if (!creds.empty()) {
        auto visible = wifi_setup::scanNow(false);
        boot_ui::step("wifi scan", true,
                      String((unsigned)visible.size()) + " in range");
        auto candidates = wifi_setup::filterByVisibility(creds, visible);
        if (candidates.empty()) {
            boot_ui::step("matched saved", false, "none of yours nearby");
        } else {
            boot_ui::step("matched saved", true,
                          String((unsigned)candidates.size()) + " candidate(s)");
            connected = connectWiFiFromList(candidates, &connectedSsid);
        }
    }
    if (!connected) {
        boot_ui::step("wifi", false,
                      creds.empty() ? "no saved networks" : "candidates rejected");
        delay(400);
        Serial.println("[boot] entering wifi setup");
        if (!wifi_setup::run(/*allowCancel=*/false)) halt("wifi setup cancelled", "");
        connectedSsid = WiFi.SSID();
        boot_ui::startLog();
        boot_ui::step("mounting sd", true);
        boot_ui::step("loaded wifi.txt", true,
                      String((unsigned)sdcfg::loadWiFi().size()) + " saved network(s)");
        boot_ui::step("wifi", true, connectedSsid + " . " + WiFi.localIP().toString());
    } else {
        boot_ui::step("wifi", true, connectedSsid + " . " + WiFi.localIP().toString());
    }

    bool ntpOk = syncTimeQuiet();
    boot_ui::step("ntp sync", ntpOk, ntpOk ? "" : "(skipped)");

    String userPersona = sdcfg::loadSystemPrompt();
    bool customSys = userPersona.length() > 0;
    if (!customSys) userPersona = kDefaultPersona;
    boot_ui::step("system prompt", true,
                  customSys ? "loaded /system.txt" : "built-in default");

    String apiKey = sdcfg::loadOpenRouterKey();
    if (apiKey.length() == 0) {
        boot_ui::step("api key", false, "none on sd");
        delay(400);
        Serial.println("[boot] no api key; entering web setup");
        if (!key_setup::run(/*allowCancel=*/false)) halt("key setup cancelled", "");
        apiKey = sdcfg::loadOpenRouterKey();
        if (apiKey.length() == 0) halt("key save inconsistent", "");
        boot_ui::startLog();
        boot_ui::step("mounting sd", true);
        boot_ui::step("wifi", true, WiFi.SSID() + " . " + WiFi.localIP().toString());
        boot_ui::step("ntp sync", ntpOk);
        boot_ui::step("system prompt", true,
                      customSys ? "loaded /system.txt" : "built-in default");
        boot_ui::step("api key", true, "saved");
    } else {
        boot_ui::step("api key", true, "loaded from sd");
    }

    boot_ui::step("data dirs", true, "/Cardputer/{chats,notes,snaps}");
    boot_ui::finishLog();

    // Pre-cycle the ES8311 codec so Verbatim mode's first record (if
    // the user picks Verbatim) doesn't capture the analog-stage startup
    // transient. Same fix that lived in Verbatim's main.cpp.
    M5Cardputer.Speaker.end();
    if (M5Cardputer.Mic.begin()) {
        delay(80);
        M5Cardputer.Mic.end();
        Serial.println("[boot] mic warm-up complete");
    } else {
        Serial.println("[boot] mic warm-up failed (mic.begin returned false)");
    }
    M5Cardputer.Speaker.begin();

    // First-run welcome shown once per device, before mode pick.
    if (!settings::welcomed()) {
        welcome::run();
        settings::setWelcomed(true);
    }

    // Mode picker -- defaults the highlight to whichever app the user
    // picked last time.
    uint8_t chosen = mode_picker::run(settings::lastMode());
    settings::setLastMode(chosen);

    if (chosen == mode_picker::kLLM) {
        Serial.println("[boot] launching LLM mode");
        int depth = settings::historyDepth();
        boot_ui::step("history depth", true, String(depth) + " messages");

        OpenAICompatibleProvider* ai =
            makeLlmProvider(apiKey, kLlmModels[kInitialLlmModelIdx].slug);
        String sys = String(kFormatPrompt) + "\n" + userPersona;

        g_chat = new ChatScreen(ai, sys, kLlmModels, kInitialLlmModelIdx, depth);
        g_chat->begin();
        Serial.println("[chat] ready");
    } else {
        Serial.println("[boot] launching Verbatim mode");
        g_notes = new NotesScreen(apiKey,
                                  settings::txModel(),
                                  settings::titleModel());
        g_notes->begin();
        Serial.println("[notes] ready");
    }
}

void loop() {
    if (g_chat)       g_chat->tick();
    else if (g_notes) g_notes->tick();
}
