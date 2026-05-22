// CardputerLLM phase 2: ESPAI proof against OpenRouter.
//
// Connects WiFi, makes one non-streaming chat call (BasicChat), then one
// streaming chat call (chatStream). Prints everything to USB serial AND
// streams to the display body so we can confirm both paths work end to end
// without a keyboard or full chat UI.
//
// Test model: openai/gpt-4o-mini via OpenRouter. Cheap, fast, OK signal.
// Final model slugs are picked in Phase 7.
//
// On failure: surface the error verbatim. Do not silently swap providers or
// fall back to non-streaming. If this phase fails to stream, the project
// pivots to a hand-rolled SSE client; the spec is explicit about that.

#include <M5Cardputer.h>
#include <WiFi.h>
#include <ESPAI.h>

#include "secrets.h"

using namespace ESPAI;

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;
static constexpr int kHeaderH = 12;
static constexpr int kFooterH = 12;

static constexpr const char* kBaseUrl  = "https://openrouter.ai/api/v1/chat/completions";
static constexpr const char* kTestModel = "openai/gpt-4o-mini";
static constexpr const char* kPrompt   = "Say hi to the Cardputer in one short sentence.";

static int bodyY      = kHeaderH + 2;
static int bodyCursor = bodyY;
static int bodyLineH  = 10;

static void clearBody() {
    M5Cardputer.Display.fillRect(0, kHeaderH, kScreenW, kScreenH - kHeaderH - kFooterH, TFT_BLACK);
    bodyCursor = bodyY;
    M5Cardputer.Display.setCursor(2, bodyCursor);
}

static void setHeader(const String& msg) {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kHeaderH, TFT_NAVY);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5Cardputer.Display.setCursor(2, 2);
    M5Cardputer.Display.print(msg);
}

static void setFooter(const String& msg, uint16_t color = TFT_DARKGREY) {
    M5Cardputer.Display.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, color);
    M5Cardputer.Display.setTextColor(TFT_BLACK, color);
    M5Cardputer.Display.setCursor(2, kScreenH - kFooterH + 2);
    M5Cardputer.Display.print(msg);
}

// Stream-friendly body print. Wraps at screen width, advances cursor,
// scrolls by clearing when we hit the bottom.
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

static bool connectWiFi() {
    setHeader("WiFi: connecting...");
    Serial.print("[wifi] connecting to "); Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(250);
        Serial.print('.');
        attempts++;
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        setHeader("WiFi: FAILED");
        Serial.println("[wifi] FAILED");
        return false;
    }
    String ip = WiFi.localIP().toString();
    setHeader(String("WiFi: ") + ip);
    Serial.print("[wifi] ok, ip="); Serial.println(ip);
    return true;
}

static OpenAICompatibleProvider* makeProvider() {
    OpenAICompatibleConfig cfg;
    cfg.name    = "OpenRouter";
    cfg.baseUrl = kBaseUrl;
    cfg.apiKey  = OPENROUTER_API_KEY;
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

    unsigned long t0 = millis();
    Response r = ai.chat(messages, options);
    unsigned long dt = millis() - t0;

    if (r.success) {
        Serial.println("[basic] OK:");
        Serial.println(r.content);
        Serial.printf("[basic] %lums, prompt=%d, completion=%d, total=%d\n",
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

    unsigned long t0  = millis();
    unsigned long tFt = 0;
    int chunks = 0;

    bool ok = ai.chatStream(messages, options,
        [&](const String& chunk, bool done) {
            if (chunks == 0 && chunk.length() > 0) tFt = millis() - t0;
            chunks++;
            Serial.print(chunk);
            bodyPrint(chunk);
            if (done) {
                unsigned long dt = millis() - t0;
                Serial.println();
                Serial.printf("[stream] done. %lums total, %lums ttft, %d chunks\n",
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

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, false); // keyboard not needed this phase
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("[boot] cardputerllm phase 2: espai proof");

    setHeader("phase 2 boot");
    setFooter("idle");

    if (strlen(WIFI_SSID) == 0 || strlen(OPENROUTER_API_KEY) == 0) {
        setHeader("missing secrets.h");
        setFooter("fill include/secrets.h", TFT_RED);
        Serial.println("[boot] secrets.h is empty. fill in WIFI_SSID/WIFI_PASSWORD/OPENROUTER_API_KEY and rebuild.");
        return;
    }

    if (!connectWiFi()) {
        setFooter("wifi failed", TFT_RED);
        return;
    }

    OpenAICompatibleProvider* ai = makeProvider();
    runBasicChat(*ai);
    delay(1500);
    runStreamingChat(*ai);
    delete ai;
}

void loop() {
    delay(1000);
}
