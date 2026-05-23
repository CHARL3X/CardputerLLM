// Title generation via OpenRouter chat completions.
//
// Endpoint: POST https://openrouter.ai/api/v1/chat/completions
// Body: standard OpenAI-compatible chat completion request with the
// title prompt (from spec) as system message and the transcript as
// user message. Response: choices[0].message.content.
//
// The returned title is sanitized before it lands in Result.title:
//   - trimmed
//   - leading/trailing quotes stripped
//   - leading/trailing markdown chars (*, _, `) stripped
//   - YAML-hostile chars (newline, colon, double-quote) replaced
//   - capped at 60 characters
//   - empty -> "Untitled note"
//
// If the API call fails entirely the caller is expected to fall back
// to "Untitled note" without surfacing an error to the user -- title
// generation is convenience, not gating.

#pragma once
#include <Arduino.h>

namespace title {

enum class Outcome : uint8_t {
    Ok,
    NoWiFi,
    TlsFailed,
    InvalidKey,
    RateLimited,
    BadResponse,
    NetworkError,
    Unknown,
};

struct Result {
    Outcome outcome    = Outcome::Unknown;
    int     httpStatus = 0;
    String  title;       // sanitized; "Untitled note" on fallback
    String  errorDetail;
};

Result generate(const String& transcript,
                const String& apiKey,
                const String& model);

const char* outcomeName(Outcome o);

} // namespace title
