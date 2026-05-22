#include "chat_screen.h"
#include <M5Cardputer.h>

namespace {

// Layout
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kInputH  = 20;
constexpr int kBodyTop = 0;
constexpr int kBodyH   = kScreenH - kInputH;
constexpr int kPadX    = 4;
constexpr int kLinePad = 2;

// Palette. Warm CRT, no terminal green, no slabby grey.
constexpr uint16_t kBg           = 0x0000;  // black
constexpr uint16_t kDivider      = 0x2104;  // very dim warm grey
constexpr uint16_t kUserColor    = 0xEF7D;  // off-white cream
constexpr uint16_t kAsstColor    = 0xFD60;  // warm amber/orange
constexpr uint16_t kInputColor   = 0xEF7D;  // same as user
constexpr uint16_t kCursorColor  = 0xFD60;  // amber cursor
constexpr uint16_t kStreamHint   = 0x83C0;  // dim amber for "..." while streaming

} // namespace

ChatScreen::ChatScreen(ESPAI::OpenAICompatibleProvider* ai) : _ai(ai) {}

int ChatScreen::lineHeight() const {
    return M5Cardputer.Display.fontHeight() + kLinePad;
}

void ChatScreen::begin() {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);
    _bodyDirty  = true;
    _inputDirty = true;
}

void ChatScreen::tick() {
    M5Cardputer.update();

    if (!_streaming && M5Cardputer.Keyboard.isChange()) {
        onKeyChange();
    }

    // Cursor blink, only when idle
    if (!_streaming && (millis() - _cursorTick) > 450) {
        _cursorTick = millis();
        _cursorOn   = !_cursorOn;
        renderCursorOnly();
    }

    if (_bodyDirty)  { renderTurns(); _bodyDirty  = false; }
    if (_inputDirty) { renderInput(); _inputDirty = false; }

    delay(5);
}

void ChatScreen::onKeyChange() {
    if (!M5Cardputer.Keyboard.isPressed()) return;
    auto& state = M5Cardputer.Keyboard.keysState();

    bool changed = false;
    for (auto c : state.word) {
        _input += (char)c;
        changed = true;
    }
    if (state.del && _input.length() > 0) {
        _input.remove(_input.length() - 1);
        changed = true;
    }
    if (state.enter && _input.length() > 0) {
        sendCurrent();
        return;
    }
    if (changed) _inputDirty = true;
}

void ChatScreen::sendCurrent() {
    String prompt = _input;
    _input = "";
    _turns.push_back({ESPAI::Role::User, prompt});
    _turns.push_back({ESPAI::Role::Assistant, String()});
    _turns.back().text.reserve(256);
    _streaming  = true;
    _bodyDirty  = true;
    _inputDirty = true;
    renderTurns();
    renderInput();

    Serial.printf("[chat] send: %s\n", prompt.c_str());

    std::vector<ESPAI::Message> messages;
    // Phase 5: single-turn, no prior history. Phase 6 adds history.
    messages.push_back(ESPAI::Message(ESPAI::Role::User, prompt));
    ESPAI::ChatOptions options;
    options.maxTokens = 256;

    uint32_t t0 = millis();
    int chunks  = 0;

    bool ok = _ai->chatStream(messages, options,
        [&](const String& chunk, bool done) {
            chunks++;
            _turns.back().text += chunk;
            // Re-render body so streamed text appears live.
            renderTurns();
            if (done) {
                Serial.printf("[chat] done. %ums, %d chunks\n",
                              (unsigned)(millis() - t0), chunks);
            }
        });

    if (!ok) {
        Serial.println("[chat] FAIL");
        _turns.back().text += " [error]";
    }
    _streaming  = false;
    _bodyDirty  = true;
    _inputDirty = true;
}

void ChatScreen::wrapInto(const String& s, int maxPx, uint16_t color,
                          bool right, std::vector<Line>& out) {
    const int n = (int)s.length();
    int i = 0;
    while (i < n) {
        int j = i;
        int lastBreak = -1;
        while (j < n) {
            char c = s.charAt(j);
            if (c == '\n') {
                lastBreak = j;
                j++;
                break;
            }
            // Probe width by extending one char
            int probeEnd = j + 1;
            String probe = s.substring(i, probeEnd);
            if (M5Cardputer.Display.textWidth(probe.c_str()) > maxPx) {
                break;
            }
            if (c == ' ') lastBreak = j;
            j++;
        }
        int end = j;
        if (end < n && s.charAt(end) != '\n') {
            // ran out of width before newline
            if (lastBreak > i) end = lastBreak;
        }
        if (end > i || (end == i && i < n && s.charAt(i) == '\n')) {
            String line = s.substring(i, end);
            // Strip a trailing space if we broke on one
            while (line.length() > 0 && line.charAt(line.length()-1) == ' ') {
                line.remove(line.length()-1);
            }
            out.push_back({line, color, right});
        }
        i = end;
        // skip the break character (space or newline)
        if (i < n && (s.charAt(i) == ' ' || s.charAt(i) == '\n')) i++;
    }
    if (n == 0) {
        out.push_back({String(""), color, right});
    }
}

void ChatScreen::renderTurns() {
    M5Cardputer.Display.fillRect(0, kBodyTop, kScreenW, kBodyH, kBg);

    int maxPx = kScreenW - 2 * kPadX;

    std::vector<Line> lines;
    lines.reserve(_turns.size() * 3);
    for (size_t i = 0; i < _turns.size(); i++) {
        const auto& t = _turns[i];
        uint16_t color = (t.role == ESPAI::Role::User) ? kUserColor : kAsstColor;
        bool right     = (t.role == ESPAI::Role::User);
        wrapInto(t.text, maxPx, color, right, lines);
        // Gap between turns
        if (i + 1 < _turns.size()) {
            lines.push_back({String(""), 0, false});
        }
    }

    const int lh = lineHeight();
    const int maxLines = kBodyH / lh;
    int startIdx = ((int)lines.size() > maxLines)
                       ? (int)lines.size() - maxLines
                       : 0;

    int y = kBodyTop + 2;
    for (int i = startIdx; i < (int)lines.size(); i++) {
        const auto& ln = lines[i];
        if (ln.text.length() == 0) {
            y += lh;
            continue;
        }
        M5Cardputer.Display.setTextColor(ln.color, kBg);
        int x = kPadX;
        if (ln.rightAlign) {
            int w = M5Cardputer.Display.textWidth(ln.text.c_str());
            x = kScreenW - kPadX - w;
            if (x < kPadX) x = kPadX;
        }
        M5Cardputer.Display.setCursor(x, y);
        M5Cardputer.Display.print(ln.text);
        y += lh;
    }
}

void ChatScreen::renderInput() {
    const int y = kBodyH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kInputH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);

    // Show the tail of the input if too long to fit
    String shown = _input;
    int maxPx = kScreenW - 2 * kPadX - 8; // leave room for cursor
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx
           && shown.length() > 0) {
        shown.remove(0, 1);
    }

    M5Cardputer.Display.setTextColor(kInputColor, kBg);
    M5Cardputer.Display.setCursor(kPadX, y + 3);
    M5Cardputer.Display.print(shown);

    // Streaming indicator on the right
    if (_streaming) {
        const char* hint = "...";
        int hw = M5Cardputer.Display.textWidth(hint);
        M5Cardputer.Display.setTextColor(kStreamHint, kBg);
        M5Cardputer.Display.setCursor(kScreenW - kPadX - hw, y + 3);
        M5Cardputer.Display.print(hint);
    } else if (_cursorOn) {
        int cx = M5Cardputer.Display.getCursorX();
        int ch = M5Cardputer.Display.fontHeight();
        M5Cardputer.Display.fillRect(cx + 1, y + 3, 6, ch, kCursorColor);
    }
}

void ChatScreen::renderCursorOnly() {
    // Minimal redraw to avoid flicker on the rest of the screen
    const int y = kBodyH;
    int maxPx = kScreenW - 2 * kPadX - 8;
    String shown = _input;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx
           && shown.length() > 0) {
        shown.remove(0, 1);
    }
    // Where the cursor sits
    int cx = kPadX + M5Cardputer.Display.textWidth(shown.c_str());
    int ch = M5Cardputer.Display.fontHeight();
    // Clear cursor region then re-draw if on
    M5Cardputer.Display.fillRect(cx + 1, y + 3, 8, ch, kBg);
    if (_cursorOn && !_streaming) {
        M5Cardputer.Display.fillRect(cx + 1, y + 3, 6, ch, kCursorColor);
    }
}

void ChatScreen::renderAll() {
    M5Cardputer.Display.fillScreen(kBg);
    renderTurns();
    renderInput();
}
