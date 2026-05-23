// Title generator. Same HTTP/TLS pattern as transcribe.cpp but with
// a non-streamed JSON body and the chat-completions endpoint.
#include "title.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

namespace title {

namespace {

constexpr const char* kHost = "openrouter.ai";
constexpr uint16_t    kPort = 443;
constexpr const char* kPath = "/api/v1/chat/completions";

// Locked title prompt from Verbatim/SPEC. Word-for-word.
constexpr const char* kPrompt =
    "You are titling a voice memo transcript. Generate a 3-6 word title "
    "capturing the essence of the content. No punctuation, no quotes, no "
    "preamble, just the title text. If the transcript is unclear or "
    "empty, respond with exactly: Untitled note";

String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if ((uint8_t)c < 0x20) {
                    char b[8];
                    snprintf(b, sizeof(b), "\\u%04x", (uint8_t)c);
                    out += b;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

String sanitizeTitle(const String& raw) {
    String t = raw;
    t.trim();
    if (t.length() == 0) return String("Untitled note");

    // Strip leading/trailing quotes and common markdown wrappers.
    auto stripBoth = [&](char ch) {
        while (t.length() > 0 && t.charAt(0) == ch)            t.remove(0, 1);
        while (t.length() > 0 && t.charAt(t.length() - 1) == ch) t.remove(t.length() - 1);
    };
    stripBoth('"');
    stripBoth('\'');
    stripBoth('*');
    stripBoth('_');
    stripBoth('`');

    // Replace YAML-hostile chars with spaces so the frontmatter stays
    // well-formed. The model is instructed not to use these but defense
    // in depth.
    String cleaned;
    cleaned.reserve(t.length());
    for (size_t i = 0; i < t.length(); i++) {
        char c = t.charAt(i);
        if (c == '\n' || c == '\r' || c == '\t' || c == ':' || c == '"') {
            cleaned += ' ';
        } else {
            cleaned += c;
        }
    }
    t = cleaned;
    t.trim();
    if (t.length() == 0) return String("Untitled note");

    if (t.length() > 60) t = t.substring(0, 60);
    return t;
}

String readHeaderLine(WiFiClientSecure& c, uint32_t timeoutMs) {
    String line;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (!c.available()) { delay(2); continue; }
        char ch = (char)c.read();
        if (ch == '\r') continue;
        if (ch == '\n') return line;
        line += ch;
    }
    return line;
}

Result fail(Outcome o, int http, const String& detail) {
    Result r;
    r.outcome = o; r.httpStatus = http; r.errorDetail = detail;
    r.title = "Untitled note";
    return r;
}

} // namespace

const char* outcomeName(Outcome o) {
    switch (o) {
        case Outcome::Ok:           return "ok";
        case Outcome::NoWiFi:       return "no wifi";
        case Outcome::TlsFailed:    return "tls failed";
        case Outcome::InvalidKey:   return "invalid key";
        case Outcome::RateLimited:  return "rate limited";
        case Outcome::BadResponse:  return "bad response";
        case Outcome::NetworkError: return "network error";
        default:                    return "unknown";
    }
}

Result generate(const String& transcript,
                const String& apiKey,
                const String& model) {
    if (WiFi.status() != WL_CONNECTED) return fail(Outcome::NoWiFi, 0, "wifi down");
    if (apiKey.length() == 0)          return fail(Outcome::InvalidKey, 0, "empty key");

    String body;
    body.reserve(transcript.length() + strlen(kPrompt) + 256);
    body  = "{\"model\":\"";
    body += model;
    body += "\",\"messages\":[{\"role\":\"system\",\"content\":\"";
    body += jsonEscape(kPrompt);
    body += "\"},{\"role\":\"user\",\"content\":\"";
    body += jsonEscape(transcript);
    body += "\"}],\"max_tokens\":30,\"temperature\":0.3}";

    Serial.printf("[title] body=%u heap=%u\n",
                  (unsigned)body.length(), (unsigned)ESP.getFreeHeap());

    static WiFiClientSecure client;
    client.stop();
    client.setInsecure();
    client.setTimeout(30000);

    uint32_t t0 = millis();
    if (!client.connect(kHost, kPort)) {
        return fail(Outcome::TlsFailed, 0, "could not connect");
    }
    Serial.printf("[title] connected in %u ms\n", (unsigned)(millis() - t0));

    client.printf("POST %s HTTP/1.1\r\n", kPath);
    client.printf("Host: %s\r\n", kHost);
    client.printf("Authorization: Bearer %s\r\n", apiKey.c_str());
    client.print("Content-Type: application/json\r\n");
    client.print("Accept: application/json\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)body.length());
    client.print("Connection: close\r\n");
    client.print("HTTP-Referer: https://github.com/CHARL3X/Cardputer\r\n");
    client.print("X-OpenRouter-Title: Cardputer\r\n");
    client.print("\r\n");
    client.print(body);

    uint32_t tResp = millis();
    while (!client.available() && (millis() - tResp) < 30000 && client.connected()) {
        delay(10);
    }
    if (!client.available()) {
        client.stop();
        return fail(Outcome::NetworkError, 0, "no response");
    }

    String statusLine = readHeaderLine(client, 10000);
    Serial.printf("[title] <- %s\n", statusLine.c_str());
    int statusCode = 0;
    {
        int sp1 = statusLine.indexOf(' ');
        if (sp1 > 0) {
            int sp2 = statusLine.indexOf(' ', sp1 + 1);
            if (sp2 > sp1) statusCode = statusLine.substring(sp1 + 1, sp2).toInt();
        }
    }

    int  contentLength = -1;
    bool chunked       = false;
    while (true) {
        String h = readHeaderLine(client, 10000);
        if (h.length() == 0) break;
        String lo = h; lo.toLowerCase();
        if (lo.startsWith("content-length:"))                 contentLength = h.substring(15).toInt();
        else if (lo.startsWith("transfer-encoding:")
                 && lo.indexOf("chunked") > 0)               chunked = true;
    }

    String respBody;
    respBody.reserve(2048);
    uint32_t tBody = millis();
    if (chunked) {
        while (millis() - tBody < 30000) {
            String sl = readHeaderLine(client, 10000); sl.trim();
            uint32_t cz = (uint32_t)strtoul(sl.c_str(), nullptr, 16);
            if (cz == 0) break;
            uint32_t got = 0;
            while (got < cz) {
                while (!client.available() && (millis() - tBody) < 10000) delay(2);
                if (!client.available()) break;
                respBody += (char)client.read();
                got++;
            }
            readHeaderLine(client, 1000);
        }
    } else {
        uint32_t target = (contentLength > 0) ? (uint32_t)contentLength : 0xFFFFFFFFu;
        while ((respBody.length() < target)
               && (client.available() || client.connected())
               && (millis() - tBody < 30000)) {
            while (client.available()) {
                respBody += (char)client.read();
                if (respBody.length() >= target) break;
            }
            delay(5);
        }
    }
    client.stop();

    Serial.printf("[title] body=%u bytes\n", (unsigned)respBody.length());

    Result r;
    r.httpStatus = statusCode;
    r.title      = "Untitled note";   // default; overwritten on Ok

    switch (statusCode) {
        case 200: case 201: case 202: break;
        case 401: case 403: r.outcome = Outcome::InvalidKey;   r.errorDetail = respBody; return r;
        case 429:           r.outcome = Outcome::RateLimited;  r.errorDetail = respBody; return r;
        default:
            if (statusCode >= 500) { r.outcome = Outcome::NetworkError; r.errorDetail = respBody; return r; }
            r.outcome = Outcome::BadResponse; r.errorDetail = respBody; return r;
    }

    int braceIdx = respBody.indexOf('{');
    if (braceIdx < 0) {
        r.outcome = Outcome::BadResponse;
        r.errorDetail = "no json";
        return r;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, respBody.c_str() + braceIdx);
    if (err) {
        r.outcome = Outcome::BadResponse;
        r.errorDetail = String("json: ") + err.c_str();
        return r;
    }

    String content;
    if (!doc["choices"][0]["message"]["content"].isNull()) {
        content = doc["choices"][0]["message"]["content"].as<String>();
    } else if (!doc["text"].isNull()) {
        content = doc["text"].as<String>();
    }

    r.title   = sanitizeTitle(content);
    r.outcome = Outcome::Ok;
    Serial.printf("[title] -> %s\n", r.title.c_str());
    return r;
}

} // namespace title
