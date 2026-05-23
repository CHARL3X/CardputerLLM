#include "transcribe.h"
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <algorithm>

namespace transcribe {

namespace {

constexpr const char* kHost = "openrouter.ai";
constexpr uint16_t    kPort = 443;
constexpr const char* kPath = "/api/v1/audio/transcriptions";

// RFC 4648 standard alphabet (NOT url-safe). OpenRouter expects standard.
constexpr const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline uint32_t b64Len(uint32_t n) {
    return ((n + 2) / 3) * 4;
}

// Encode up to 3 input bytes into 4 base64 chars at `out`. Final-partial
// padding ('=') applied automatically.
void encodeTriplet(const uint8_t* in, int bytes, char* out) {
    uint32_t v = (uint32_t)in[0] << 16;
    if (bytes > 1) v |= (uint32_t)in[1] << 8;
    if (bytes > 2) v |=  (uint32_t)in[2];
    out[0] = kB64[(v >> 18) & 0x3F];
    out[1] = kB64[(v >> 12) & 0x3F];
    out[2] = (bytes > 1) ? kB64[(v >> 6) & 0x3F] : '=';
    out[3] = (bytes > 2) ? kB64[ v        & 0x3F] : '=';
}

bool isSimpleJsonString(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c == '"' || c == '\\' || (uint8_t)c < 0x20) return false;
    }
    return true;
}

Result fail(Outcome o, int http, const String& detail) {
    Result r;
    r.outcome     = o;
    r.httpStatus  = http;
    r.errorDetail = detail;
    return r;
}

// Read a single header line up to CRLF; trim trailing whitespace.
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

} // namespace

const char* outcomeName(Outcome o) {
    switch (o) {
        case Outcome::Ok:              return "ok";
        case Outcome::FileMissing:     return "file missing";
        case Outcome::NoWiFi:          return "no wifi";
        case Outcome::TlsFailed:       return "tls failed";
        case Outcome::InvalidKey:      return "invalid key";
        case Outcome::RateLimited:     return "rate limited";
        case Outcome::ModelNotFound:   return "model not found";
        case Outcome::PayloadTooLarge: return "payload too large";
        case Outcome::ServerError:     return "server error";
        case Outcome::NetworkError:    return "network error";
        case Outcome::BadResponse:     return "bad response";
        case Outcome::ReadError:       return "sd read error";
        default:                       return "unknown";
    }
}

Result runWav(const String& wavPath,
              const String& apiKey,
              const String& model,
              ProgressCallback onProgress) {
    if (WiFi.status() != WL_CONNECTED) {
        return fail(Outcome::NoWiFi, 0, "wifi not connected");
    }
    if (apiKey.length() == 0) {
        return fail(Outcome::InvalidKey, 0, "empty api key");
    }
    if (!isSimpleJsonString(model)) {
        return fail(Outcome::Unknown, 0, "model slug has unsafe chars");
    }

    File f = SD.open(wavPath.c_str(), FILE_READ);
    if (!f) return fail(Outcome::FileMissing, 0, "could not open wav");
    uint32_t wavSize = f.size();
    if (wavSize <= 44) {
        f.close();
        return fail(Outcome::FileMissing, 0, "wav file too small");
    }

    // Exact body-length math. Off-by-one returns 400 silently.
    String prefix = String("{\"model\":\"") + model
                  + "\",\"input_audio\":{\"data\":\"";
    String suffix = "\",\"format\":\"wav\"}}";
    uint32_t b64n    = b64Len(wavSize);
    uint32_t bodyLen = (uint32_t)prefix.length() + b64n
                     + (uint32_t)suffix.length();

    Serial.printf("[transcribe] wav=%u b64=%u prefix=%u suffix=%u body=%u\n",
                  wavSize, b64n,
                  (unsigned)prefix.length(), (unsigned)suffix.length(),
                  bodyLen);
    Serial.printf("[transcribe] heap pre-tls free=%u min=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    Serial.flush();

    // Static so the mbedTLS context is allocated once on first call and
    // reused; avoids re-fragmenting the heap on each transcribe attempt.
    Serial.println("[transcribe] step: WiFiClientSecure");
    Serial.flush();
    static WiFiClientSecure client;
    client.stop();                  // reset between calls
    client.setInsecure();           // Phase 7 polish: pin Cloudflare's root
    client.setTimeout(30000);
    Serial.printf("[transcribe] step: post-tls-init free=%u largest=%u\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
    Serial.flush();

    Serial.printf("[transcribe] connect %s:%u\n", kHost, kPort);
    Serial.flush();
    uint32_t tConnect = millis();
    if (!client.connect(kHost, kPort)) {
        f.close();
        return fail(Outcome::TlsFailed, 0, "could not connect");
    }
    Serial.printf("[transcribe] connected in %u ms . heap=%u\n",
                  (unsigned)(millis() - tConnect),
                  (unsigned)ESP.getFreeHeap());
    Serial.flush();

    Serial.println("[transcribe] step: writing request headers");
    Serial.flush();
    client.printf("POST %s HTTP/1.1\r\n", kPath);
    client.printf("Host: %s\r\n", kHost);
    client.printf("Authorization: Bearer %s\r\n", apiKey.c_str());
    client.print("Content-Type: application/json\r\n");
    client.print("Accept: application/json\r\n");
    client.printf("Content-Length: %u\r\n", bodyLen);
    client.print("Connection: close\r\n");
    client.print("HTTP-Referer: https://github.com/CHARL3X/Cardputer\r\n");
    client.print("X-OpenRouter-Title: Cardputer\r\n");
    client.print("\r\n");

    Serial.println("[transcribe] step: writing prefix");
    Serial.flush();
    client.print(prefix);
    uint32_t sent = prefix.length();

    constexpr size_t kInChunk  = 3 * 256;   // 768 bytes / 256 triplets
    constexpr size_t kOutChunk = 4 * 256;   // 1024 bytes / 256 quads
    uint8_t  inBuf[kInChunk];
    char     outBuf[kOutChunk];

    uint32_t bytesRead    = 0;
    uint32_t lastProgress = 0;
    uint32_t tUpload      = millis();

    while (bytesRead < wavSize) {
        size_t want = kInChunk;
        if (bytesRead + want > wavSize) want = wavSize - bytesRead;
        // All but the final read must be a multiple of 3 so we don't
        // emit '=' padding mid-stream.
        bool   isFinal = (bytesRead + want == wavSize);
        size_t mod     = want % 3;
        if (!isFinal && mod != 0) want -= mod;

        int n = f.read(inBuf, want);
        if (n <= 0) {
            client.stop();
            f.close();
            return fail(Outcome::ReadError, 0, "sd read returned <=0");
        }
        bytesRead += (uint32_t)n;

        int outI = 0;
        for (int i = 0; i < n; i += 3) {
            int chunk = std::min(3, n - i);
            encodeTriplet(inBuf + i, chunk, outBuf + outI);
            outI += 4;
        }
        size_t written = client.write((const uint8_t*)outBuf, (size_t)outI);
        if (written != (size_t)outI) {
            client.stop();
            f.close();
            return fail(Outcome::NetworkError, 0, "socket write short");
        }
        sent += outI;

        if (onProgress && (millis() - lastProgress >= 50)) {
            lastProgress = millis();
            ProgressInfo p{sent, bodyLen, true};
            onProgress(p);
        }
    }
    f.close();

    client.print(suffix);
    sent += suffix.length();

    uint32_t uploadMs = millis() - tUpload;
    Serial.printf("[transcribe] upload done %u/%u in %u ms\n",
                  sent, bodyLen, (unsigned)uploadMs);

    if (onProgress) {
        ProgressInfo p{sent, bodyLen, false};
        onProgress(p);
    }

    // ---- Read response ----
    uint32_t tResp = millis();
    while (!client.available() && (millis() - tResp) < 60000
           && client.connected()) {
        delay(20);
    }
    if (!client.available()) {
        client.stop();
        return fail(Outcome::NetworkError, 0, "no response from server");
    }

    String statusLine = readHeaderLine(client, 10000);
    Serial.printf("[transcribe] <- %s\n", statusLine.c_str());

    int statusCode = 0;
    {
        int sp1 = statusLine.indexOf(' ');
        if (sp1 > 0) {
            int sp2 = statusLine.indexOf(' ', sp1 + 1);
            if (sp2 > sp1) {
                statusCode = statusLine.substring(sp1 + 1, sp2).toInt();
            }
        }
    }

    // Parse headers: capture Content-Length and Transfer-Encoding so we
    // know how to read the body cleanly.
    int  contentLength = -1;
    bool chunked       = false;
    while (true) {
        String h = readHeaderLine(client, 10000);
        if (h.length() == 0) break;
        String lower = h;
        lower.toLowerCase();
        if (lower.startsWith("content-length:")) {
            contentLength = h.substring(15).toInt();
        } else if (lower.startsWith("transfer-encoding:")
                   && lower.indexOf("chunked") > 0) {
            chunked = true;
        }
    }
    Serial.printf("[transcribe] CL=%d chunked=%d\n", contentLength, chunked);

    // Read body
    String body;
    body.reserve(4096);
    uint32_t tBody = millis();

    if (chunked) {
        while (millis() - tBody < 60000) {
            String sizeLine = readHeaderLine(client, 10000);
            sizeLine.trim();
            uint32_t chunkSize = (uint32_t)strtoul(sizeLine.c_str(), nullptr, 16);
            if (chunkSize == 0) break;
            uint32_t got = 0;
            while (got < chunkSize) {
                while (!client.available() && (millis() - tBody) < 10000) delay(2);
                if (!client.available()) break;
                body += (char)client.read();
                got++;
            }
            // Consume trailing CRLF after chunk
            readHeaderLine(client, 1000);
        }
    } else {
        uint32_t target = (contentLength > 0) ? (uint32_t)contentLength : 0xFFFFFFFFu;
        while ((body.length() < target)
               && (client.available() || client.connected())
               && (millis() - tBody < 60000)) {
            while (client.available()) {
                body += (char)client.read();
                if (body.length() >= target) break;
            }
            delay(5);
        }
    }
    client.stop();
    Serial.printf("[transcribe] body=%u bytes in %u ms\n",
                  (unsigned)body.length(), (unsigned)(millis() - tBody));
    Serial.printf("[transcribe] body: %s\n", body.c_str());

    Result r;
    r.httpStatus = statusCode;

    switch (statusCode) {
        case 200: case 201: case 202: break;
        case 401: case 403:
            r.outcome = Outcome::InvalidKey;     r.errorDetail = body; return r;
        case 404:
            r.outcome = Outcome::ModelNotFound;  r.errorDetail = body; return r;
        case 413:
            r.outcome = Outcome::PayloadTooLarge;r.errorDetail = body; return r;
        case 429:
            r.outcome = Outcome::RateLimited;    r.errorDetail = body; return r;
        default:
            if (statusCode >= 500) {
                r.outcome = Outcome::ServerError; r.errorDetail = body; return r;
            }
            r.outcome = Outcome::BadResponse;    r.errorDetail = body; return r;
    }

    // Parse JSON
    int braceIdx = body.indexOf('{');
    if (braceIdx < 0) {
        r.outcome = Outcome::BadResponse;
        r.errorDetail = "no json in response body";
        return r;
    }

    // ArduinoJson v7: JsonDocument is heap-allocated and auto-grows.
    // The deprecated StaticJsonDocument<8192> was a stack allocation that
    // overflowed the Arduino loopTask's 8 KB stack on this build (combined
    // with other locals) and triggered a panic reset on the first parse.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body.c_str() + braceIdx);
    if (err) {
        r.outcome     = Outcome::BadResponse;
        r.errorDetail = String("json parse: ") + err.c_str();
        return r;
    }

    if (doc["text"].isNull()) {
        // Some responses might nest text differently; try a couple of
        // fallback paths before giving up.
        if (!doc["choices"][0]["message"]["content"].isNull()) {
            r.text = doc["choices"][0]["message"]["content"].as<String>();
            r.outcome = Outcome::Ok;
            return r;
        }
        r.outcome     = Outcome::BadResponse;
        r.errorDetail = "no 'text' field in json";
        return r;
    }

    r.text    = doc["text"].as<String>();
    r.outcome = Outcome::Ok;
    return r;
}

} // namespace transcribe
