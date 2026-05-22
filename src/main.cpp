// CardputerLLM phase 2 (with phase-3 SD config pulled in):
// ESPAI proof against OpenRouter, with credentials loaded from SD card.
//
// At boot:
//   1. Mount SD.
//   2. Read /openrouter.txt (single line, the API key).
//   3. Read /wifi.txt (ssid/password line pairs, tried in order).
//   4. Connect to the first WiFi that associates.
//   5. Run one BasicChat against openai/gpt-4o-mini, dump to serial + screen.
//   6. Run one chatStream against the same model, stream tokens to serial
//      + screen live.
//
// On any failure: surface the error verbatim on screen and serial. Do not
// silently fall back. If streaming fails, the spec says pivot to a
// hand-rolled SSE client; document in NOTES.md and tag phase-2-streaming-failed.

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>

#include "storage/sd_config.h"

using namespace ESPAI;

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;
static constexpr int kHeaderH = 12;
static constexpr int kFooterH = 12;

static constexpr const char* kBaseUrl   = "https://openrouter.ai/api/v1/chat/completions";
static constexpr const char* kTestModel = "openai/gpt-4o-mini";
static constexpr const char* kPrompt    = "Say hi to the Cardputer in one short sentence.";

static int bodyY      = kHeaderH + 2;
static int bodyCursor = bodyY;
static int bodyLineH  = 10;

static void clearBody() {
    M5Cardputer.Display.fillRect(0, kHeaderH, kScreenW, kScreenH - kHeaderH - kFooterH, TFT_BLACK);
    bodyCursor = bodyY;
    M5Cardputer.Display.setCursor(2, bodyCursor);
}

static void setHeader(const String& msg, uint16_t bg = TFT_NAVY) {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kHeaderH, bg);
    M5Cardputer.Display.setTextColor(TFT_WHITE, bg);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print(msg);
}

static void setFooter(const String& msg, uint16_t bg = TFT_DARKGREY) {
    M5Cardputer.Display.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, bg);
    M5Cardputer.Display.setTextColor(TFT_BLACK, bg);
    M5Cardputer.Display.setCursor(2, kScreenH - kFooterH + 2);
    M5Cardputer.Display.print(msg);
}

static void bodyPrint(const String& chunk) {
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    for (size_t i = 0; i < chunk.length(); i++) {
        char c = chunk[i];
        if (c == '\n') {
            bodyCursor += bodyLineH;
            M5Cardputer.Display.setCursor(2, bodyCursor);
            continue;
        }
        if (M5Cardputer.Display.getCursorX() > kScreenW - 8) {
            bodyCursor += bodyLineH;
            M5Cardputer.Display.setCursor(2, bodyCursor);
        }
        if (bodyCursor > kScreenH - kFooterH - bodyLineH) {
            clearBody();
        }
        M5Cardputer.Display.print(c);
    }
}

static bool tryWiFi(const String& ssid, const String& pw, uint32_t timeoutMs) {
    setHeader(String("WiFi: ") + ssid);
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
    if (creds.empty()) {
        Serial.println("[wifi] no creds in /wifi.txt");
        return false;
    }
    for (auto& c : creds) {
        if (tryWiFi(c.ssid, c.password, 12000)) {
            String ip = WiFi.localIP().toString();
            setHeader(String("WiFi ok: ") + ip);
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

static void runBasicChat(OpenAICompatibleProvider& ai) {
    setFooter("test 1/2: basic chat...");
    Serial.println("[basic] sending...");
    clearBody();

    std::vector<Message> messages;
    messages.push_back(Message(Role::User, kPrompt));
    ChatOptions options;
    options.maxTokens = 64;

    uint32_t t0 = millis();
    Response r  = ai.chat(messages, options);
    uint32_t dt = millis() - t0;

    if (r.success) {
        Serial.println("[basic] OK:");
        Serial.println(r.content);
        Serial.printf("[basic] %ums, prompt=%d, completion=%d, total=%d\n",
                      dt, r.promptTokens, r.completionTokens, r.totalTokens());
        bodyPrint(r.content);
        setFooter("basic ok " + String(dt) + "ms", TFT_DARKGREEN);
    } else {
        Serial.println("[basic] FAIL:");
        Serial.print("  err: ");  Serial.println(errorCodeToString(r.error));
        Serial.print("  msg: ");  Serial.println(r.errorMessage);
        Serial.print("  http: "); Serial.println(r.httpStatus);
        bodyPrint(String("ERR ") + errorCodeToString(r.error) + "\n");
        bodyPrint(r.errorMessage + "\n");
        bodyPrint(String("http ") + r.httpStatus);
        setFooter("basic FAIL", TFT_RED);
    }
}

static void runStreamingChat(OpenAICompatibleProvider& ai) {
    setFooter("test 2/2: streaming...");
    Serial.println("[stream] sending...");
    clearBody();

    std::vector<Message> messages;
    messages.push_back(Message(Role::User, kPrompt));
    ChatOptions options;
    options.maxTokens = 64;

    uint32_t t0  = millis();
    uint32_t tFt = 0;
    int chunks = 0;

    bool ok = ai.chatStream(messages, options,
        [&](const String& chunk, bool done) {
            if (chunks == 0 && chunk.length() > 0) tFt = millis() - t0;
            chunks++;
            Serial.print(chunk);
            bodyPrint(chunk);
            if (done) {
                uint32_t dt = millis() - t0;
                Serial.println();
                Serial.printf("[stream] done. %ums total, %ums ttft, %d chunks\n",
                              dt, tFt, chunks);
                setFooter(String("stream ok ttft=") + tFt + "ms", TFT_DARKGREEN);
            }
        });

    if (!ok) {
        Serial.println("[stream] FAIL");
        bodyPrint("\nERR stream failed");
        setFooter("stream FAIL", TFT_RED);
    }
}

static void halt(const String& head, const String& foot) {
    setHeader(head, TFT_MAROON);
    setFooter(foot, TFT_RED);
    Serial.printf("[halt] %s | %s\n", head.c_str(), foot.c_str());
    while (true) delay(1000);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, false); // keyboard not needed this phase
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("[boot] cardputerllm phase 2 (sd creds)");

    setHeader("phase 2 boot");
    setFooter("mounting sd...");

    if (!sdcfg::begin()) {
        halt("SD: mount failed", "insert sd card");
    }

    String apiKey = sdcfg::loadOpenRouterKey();
    if (apiKey.length() == 0) {
        halt("missing /CardputerLLM/openrouter.txt", "put key on sd");
    }
    if (!apiKey.startsWith("sk-or-")) {
        Serial.println("[sd] warn: api key does not start with sk-or-");
    }

    auto creds = sdcfg::loadWiFi();
    if (creds.empty()) {
        halt("missing /CardputerLLM/wifi.txt", "put ssid+pw pairs on sd");
    }
    Serial.printf("[sd] %u wifi candidates loaded\n", (unsigned)creds.size());

    if (!connectWiFiFromList(creds)) {
        halt("wifi: all failed", "check /wifi.txt");
    }

    OpenAICompatibleProvider* ai = makeProvider(apiKey);
    runBasicChat(*ai);
    delay(1500);
    runStreamingChat(*ai);
    delete ai;
}

void loop() {
    delay(1000);
}
