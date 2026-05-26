// Cardputer merged firmware boot orchestration.
//
// Flow:
//   splash -> silent SD mount -> first-run welcome -> mode picker.
//
// Network-backed modes (LLM, Verbatim) connect WiFi + sync NTP + load
// the API key AFTER the picker, only if the user selected one of those
// modes. Tetris skips network setup entirely.
//
// On user-cancel inside post-picker wifi_setup / key_setup we just
// ESP.restart() back to the picker -- they can pick Tetris instead.
//
// Shared SD namespace /Cardputer/ for credentials + system prompt;
// app-specific data lives in chats/ (LLM) and notes/ (Verbatim).

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>
#include <time.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

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
#include "ui/tetris.h"

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

// ---------- post-picker network + key setup ----------
//
// Runs only for LLM / Verbatim. Connects WiFi (entering interactive
// wifi_setup if no saved creds match), syncs NTP, and loads or
// captures the OpenRouter API key. Renders progress via boot_ui.
//
// Returns true on success with `apiKeyOut` populated. Returns false if
// the user cancelled wifi_setup or key_setup -- caller should
// ESP.restart() back to the picker so they can pick an offline mode.
static bool postPickerNetworkSetup(String& apiKeyOut, bool& ntpOkOut) {
    boot_ui::startLog();

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
        Serial.println("[boot] entering wifi setup (post-picker)");
        if (!wifi_setup::run(/*allowCancel=*/true)) {
            Serial.println("[boot] wifi setup cancelled by user");
            return false;
        }
        connectedSsid = WiFi.SSID();
        // Re-render the visible portion of the log after wifi_setup
        // took over the screen.
        boot_ui::startLog();
        boot_ui::step("loaded wifi.txt", true,
                      String((unsigned)sdcfg::loadWiFi().size()) + " saved network(s)");
        boot_ui::step("wifi", true, connectedSsid + " . " + WiFi.localIP().toString());
    } else {
        boot_ui::step("wifi", true, connectedSsid + " . " + WiFi.localIP().toString());
    }

    ntpOkOut = syncTimeQuiet();
    boot_ui::step("ntp sync", ntpOkOut, ntpOkOut ? "" : "(skipped)");

    apiKeyOut = sdcfg::loadOpenRouterKey();
    if (apiKeyOut.length() == 0) {
        boot_ui::step("api key", false, "none on sd");
        delay(400);
        Serial.println("[boot] no api key; entering web setup");
        if (!key_setup::run(/*allowCancel=*/true)) {
            Serial.println("[boot] key setup cancelled by user");
            return false;
        }
        apiKeyOut = sdcfg::loadOpenRouterKey();
        if (apiKeyOut.length() == 0) {
            Serial.println("[boot] key save inconsistent");
            return false;
        }
        boot_ui::startLog();
        boot_ui::step("wifi", true, WiFi.SSID() + " . " + WiFi.localIP().toString());
        boot_ui::step("ntp sync", ntpOkOut);
        boot_ui::step("api key", true, "saved");
    } else {
        boot_ui::step("api key", true, "loaded from sd");
    }

    return true;
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
    Serial.println("[boot] cardputer (LLM + Verbatim + Tetris)");

    // Diagnostic: what kind of reset triggered this boot? Helps narrow
    // down whether a sub-app crashed (PANIC/WDT/etc) vs a normal restart
    // we issued ourselves.
    esp_reset_reason_t rr = esp_reset_reason();
    const char* rrName = "?";
    switch (rr) {
        case ESP_RST_POWERON:  rrName = "POWERON"; break;
        case ESP_RST_EXT:      rrName = "EXT";     break;
        case ESP_RST_SW:       rrName = "SW";      break;
        case ESP_RST_PANIC:    rrName = "PANIC";   break;
        case ESP_RST_INT_WDT:  rrName = "INT_WDT"; break;
        case ESP_RST_TASK_WDT: rrName = "TASK_WDT";break;
        case ESP_RST_WDT:      rrName = "WDT";     break;
        case ESP_RST_DEEPSLEEP:rrName = "DEEPSLEEP";break;
        case ESP_RST_BROWNOUT: rrName = "BROWNOUT";break;
        case ESP_RST_SDIO:     rrName = "SDIO";    break;
        default: break;
    }
    Serial.printf("[boot] reset_reason=%s (%d)\n", rrName, (int)rr);

    // Diagnostic: what partition are we running from, and what does
    // otadata say should boot next? Helps trace two-stage chain behavior.
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* bootPart = esp_ota_get_boot_partition();
    Serial.printf("[boot] running=%s @ 0x%lX (size 0x%lX)\n",
                  running ? running->label : "(null)",
                  running ? (long)running->address : 0L,
                  running ? (long)running->size : 0L);
    Serial.printf("[boot] otadata->next=%s @ 0x%lX\n",
                  bootPart ? bootPart->label : "(null)",
                  bootPart ? (long)bootPart->address : 0L);

    settings::begin();

    splash::run();

    // Minimum required for ANY mode: SD card mounted. Three modes touch
    // SD (creds + history for LLM, creds + notes for Verbatim; Tetris
    // is harmless either way). Silent if it works -- halt with a clear
    // error if not.
    if (!sdcfg::begin()) halt("sd: mount failed", "insert sd card");
    sdcfg::ensureDirs();
    Serial.println("[boot] sd mounted; data dirs ready");

    // First-run welcome -- mode-neutral, shown once per device.
    if (!settings::welcomed()) {
        welcome::run();
        settings::setWelcomed(true);
    }

    // Mode picker. Highlight defaults to whatever the user last ran.
    uint8_t chosen = mode_picker::run(settings::lastMode());
    settings::setLastMode(chosen);

    // -------- Offline modes: no network needed --------

    if (chosen == mode_picker::kTetris) {
        Serial.println("[boot] launching Tetris");
        tetris_screen::run();
        Serial.println("[tetris] returned; restarting to picker");
        ESP.restart();
    }

    // -------- Network-backed modes (LLM + Verbatim) --------

    String apiKey;
    bool   ntpOk = false;
    if (!postPickerNetworkSetup(apiKey, ntpOk)) {
        // User cancelled wifi or key setup. Bounce back to the picker
        // so they can pick an offline mode instead.
        Serial.println("[boot] network/key cancelled; back to picker");
        delay(400);
        ESP.restart();
    }

    if (chosen == mode_picker::kLLM) {
        Serial.println("[boot] launching LLM mode");

        String userPersona = sdcfg::loadSystemPrompt();
        bool customSys = userPersona.length() > 0;
        if (!customSys) userPersona = kDefaultPersona;
        boot_ui::step("system prompt", true,
                      customSys ? "loaded /system.txt" : "built-in default");

        int depth = settings::historyDepth();
        boot_ui::step("history depth", true, String(depth) + " messages");
        boot_ui::finishLog();

        OpenAICompatibleProvider* ai =
            makeLlmProvider(apiKey, kLlmModels[kInitialLlmModelIdx].slug);
        String sys = String(kFormatPrompt) + "\n" + userPersona;

        g_chat = new ChatScreen(ai, sys, kLlmModels, kInitialLlmModelIdx, depth);
        g_chat->begin();
        Serial.println("[chat] ready");
    } else {
        // mode_picker::kVerbatim
        Serial.println("[boot] launching Verbatim mode");

        // Pre-cycle the ES8311 codec so the first record doesn't capture
        // the analog-stage startup transient. Verbatim-specific, so it
        // lives here rather than in the universal pre-picker setup.
        boot_ui::step("mic warm-up", true);
        M5Cardputer.Speaker.end();
        if (M5Cardputer.Mic.begin()) {
            delay(80);
            M5Cardputer.Mic.end();
            Serial.println("[boot] mic warm-up complete");
        } else {
            Serial.println("[boot] mic warm-up failed (mic.begin returned false)");
        }
        M5Cardputer.Speaker.begin();
        boot_ui::finishLog();

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
