// Streaming-base64 transcription upload to OpenRouter.
//
// Endpoint (verified against current docs 2026-05-23):
//   POST https://openrouter.ai/api/v1/audio/transcriptions
//   Headers:
//     Authorization: Bearer <key>
//     Content-Type:  application/json
//     HTTP-Referer / X-OpenRouter-Title: optional, for ranking
//   Body:
//     { "model": "<slug>",
//       "input_audio": { "data": "<base64-wav>", "format": "wav" } }
//   Response (200):
//     { "text": "<transcript>" }
//
// The full base64-encoded body for a 5-minute WAV is ~12.8 MB which can't
// live in RAM on the FN8 -- transcribe::runWav() opens the WAV from SD,
// computes the exact Content-Length, and streams base64 directly from
// disk to the TLS socket in 768-byte triplet chunks.

#pragma once
#include <Arduino.h>
#include <functional>

namespace transcribe {

enum class Outcome : uint8_t {
    Ok,              // text populated, request succeeded
    FileMissing,     // SD couldn't open the wav (or it was too small)
    NoWiFi,
    TlsFailed,       // connect() returned false
    InvalidKey,      // 401 / 403
    RateLimited,     // 429
    ModelNotFound,   // 404
    PayloadTooLarge, // 413
    ServerError,     // 5xx
    NetworkError,    // socket dropped mid-stream
    BadResponse,     // non-JSON / missing "text" field
    ReadError,       // SD read returned <0 mid-stream
    Unknown,
};

struct ProgressInfo {
    uint32_t bytesSent;
    uint32_t totalBytes;
    bool     uploading;  // true while POSTing; false once we're waiting
                         // for the server response
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;

struct Result {
    Outcome outcome    = Outcome::Unknown;
    int     httpStatus = 0;
    String  text;        // transcript on Ok; empty otherwise
    String  errorDetail; // raw body or local message
};

// Blocking upload + parse. Returns when complete or on error. The
// progress callback fires at ~20 Hz during the upload phase; use it to
// drive an on-screen progress bar.
Result runWav(const String& wavPath,
              const String& apiKey,
              const String& model,
              ProgressCallback onProgress = nullptr);

const char* outcomeName(Outcome o);

} // namespace transcribe
