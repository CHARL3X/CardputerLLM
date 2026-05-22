#include "chat_screen.h"
#include "../storage/chat_store.h"
#include <M5Cardputer.h>

namespace {

// Layout
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kInputH  = 20;
constexpr int kPadX    = 4;
constexpr int kLinePad = 2;

// Palette. Warm CRT, no terminal green, no slabby grey.
constexpr uint16_t kBg          = 0x0000;
constexpr uint16_t kDivider     = 0x2104;
constexpr uint16_t kUserColor   = 0xEF7D;
constexpr uint16_t kAsstColor   = 0xFD60;
constexpr uint16_t kInputColor  = 0xEF7D;
constexpr uint16_t kCursorColor = 0xFD60;
constexpr uint16_t kStreamHint  = 0x83C0;
constexpr uint16_t kStatusDim   = 0x6B4D; // dim warm grey for status row
constexpr uint16_t kStatusAccent= 0xFD60; // model accent
constexpr uint16_t kPickerSel   = 0xFD60;
constexpr uint16_t kPickerIdle  = 0xEF7D;

} // namespace

ChatScreen::ChatScreen(ESPAI::OpenAICompatibleProvider* ai,
                       const String& systemPrompt,
                       const std::vector<ModelChoice>& models,
                       int initialModelIdx)
    : _ai(ai),
      _systemPrompt(systemPrompt),
      _models(models),
      _modelIdx(initialModelIdx),
      _conv(20) {
    _conv.setSystemPrompt(systemPrompt);
}

int ChatScreen::lineHeight() const {
    return M5Cardputer.Display.fontHeight() + kLinePad;
}
int ChatScreen::bodyTop() const   { return kStatusH; }
int ChatScreen::bodyHeight() const { return kScreenH - kStatusH - kInputH; }
int ChatScreen::visibleLines() const { return bodyHeight() / lineHeight(); }

void ChatScreen::begin() {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);
    _statusDirty = _bodyDirty = _inputDirty = true;
}

// ----- main tick -----

void ChatScreen::tick() {
    pollKeyboard();

    if (_mode == Mode::Chat && !_streaming
        && (millis() - _cursorTick) > 450) {
        _cursorTick = millis();
        _cursorOn   = !_cursorOn;
        renderCursorOnly();
    }

    if (_statusDirty) { renderStatus(); _statusDirty = false; }
    if (_bodyDirty)   { renderBody();   _bodyDirty   = false; }
    if (_inputDirty)  { renderInput();  _inputDirty  = false; }

    delay(5);
}

// ----- keyboard polling -----

bool ChatScreen::repeatTick(Held& h, uint32_t initialMs, uint32_t periodMs) {
    uint32_t now = millis();
    if (!h.active) return false;
    if (now - h.firstPress < initialMs) return false;
    if (now - h.lastRepeat < periodMs) return false;
    h.lastRepeat = now;
    return true;
}

void ChatScreen::pollKeyboard() {
    M5Cardputer.update();

    // While streaming, ignore everything.
    if (_streaming) return;

    auto& s = M5Cardputer.Keyboard.keysState();

    // Newly-pressed printable chars (those in s.word that weren't there before)
    for (char c : s.word) {
        bool wasPrev = false;
        for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
        if (!wasPrev) onCharPressed(c, s.fn);
    }

    // Del rising edge + hold repeat
    if (s.del && !_prevDel) {
        onDel();
        _heldDel.firstPress = millis(); _heldDel.lastRepeat = millis(); _heldDel.active = true;
    } else if (!s.del) {
        _heldDel.active = false;
    } else if (repeatTick(_heldDel)) {
        onDel();
    }

    // Enter rising edge only
    if (s.enter && !_prevEnter) {
        onEnter();
    }

    // Scroll hold-repeat for Fn+',' and Fn+'.' (and the alt convention Fn+';' / Fn+'/')
    // The chars are already routed via onCharPressed; here we just track hold for repeat.
    auto fnHas = [&](char target) {
        if (!s.fn) return false;
        for (char c : s.word) if (c == target) return true;
        return false;
    };
    bool upHeld   = fnHas(',') || fnHas(';');
    bool downHeld = fnHas('.') || fnHas('/');
    if (upHeld && !_heldScrollUp.active) {
        _heldScrollUp.firstPress = millis(); _heldScrollUp.lastRepeat = millis(); _heldScrollUp.active = true;
    } else if (!upHeld) {
        _heldScrollUp.active = false;
    } else if (repeatTick(_heldScrollUp, 350, 60)) {
        if (_mode == Mode::Chat) {
            _scrollOffset = std::min(_scrollOffset + 1, 200);
            _autoScroll = (_scrollOffset == 0);
            _bodyDirty = true;
        } else if (_mode == Mode::Picker) {
            if (_pickerSel > 0) { _pickerSel--; _bodyDirty = true; }
        }
    }
    if (downHeld && !_heldScrollDown.active) {
        _heldScrollDown.firstPress = millis(); _heldScrollDown.lastRepeat = millis(); _heldScrollDown.active = true;
    } else if (!downHeld) {
        _heldScrollDown.active = false;
    } else if (repeatTick(_heldScrollDown, 350, 60)) {
        if (_mode == Mode::Chat) {
            if (_scrollOffset > 0) { _scrollOffset--; _bodyDirty = true; }
            if (_scrollOffset == 0) _autoScroll = true;
        } else if (_mode == Mode::Picker) {
            if (_pickerSel + 1 < (int)_models.size()) { _pickerSel++; _bodyDirty = true; }
        }
    }

    // Save state for next tick
    _prevWord  = s.word;
    _prevDel   = s.del;
    _prevEnter = s.enter;
}

// ----- key handlers (rising-edge) -----

void ChatScreen::onCharPressed(char c, bool fn) {
    if (fn) {
        if (_mode == Mode::Chat) {
            if (c == 'n' || c == 'N') { newChat(); return; }
            if (c == 'm' || c == 'M') { openPicker(); return; }
            // Scroll keys: one-shot here; repeat handled in pollKeyboard
            if (c == ',' || c == ';') {
                _scrollOffset = std::min(_scrollOffset + 1, 200);
                _autoScroll = (_scrollOffset == 0);
                _bodyDirty = true;
                return;
            }
            if (c == '.' || c == '/') {
                if (_scrollOffset > 0) _scrollOffset--;
                if (_scrollOffset == 0) _autoScroll = true;
                _bodyDirty = true;
                return;
            }
        } else if (_mode == Mode::Picker) {
            if (c == ',' || c == ';') {
                if (_pickerSel > 0) { _pickerSel--; _bodyDirty = true; }
                return;
            }
            if (c == '.' || c == '/') {
                if (_pickerSel + 1 < (int)_models.size()) { _pickerSel++; _bodyDirty = true; }
                return;
            }
        }
        // Fn+anything else: swallow to avoid typing the underlying char
        return;
    }

    if (_mode == Mode::Chat) {
        _input += c;
        _inputDirty = true;
        return;
    }
    if (_mode == Mode::Picker) {
        // Allow plain ,/. as alternates if user isn't holding Fn
        if (c == ',') { if (_pickerSel > 0) { _pickerSel--; _bodyDirty = true; } return; }
        if (c == '.') { if (_pickerSel + 1 < (int)_models.size()) { _pickerSel++; _bodyDirty = true; } return; }
        return;
    }
    if (_mode == Mode::Confirm) {
        if (c == 'y' || c == 'Y') {
            applyModel(_pendingModelIdx);
            _mode = Mode::Chat;
            _statusDirty = _bodyDirty = _inputDirty = true;
            return;
        }
        if (c == 'n' || c == 'N') {
            _mode = Mode::Picker;
            _bodyDirty = _inputDirty = true;
            return;
        }
    }
}

void ChatScreen::onDel() {
    if (_mode == Mode::Chat) {
        if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            _inputDirty = true;
        }
        return;
    }
    if (_mode == Mode::Picker) {
        closePicker(false);
        return;
    }
    if (_mode == Mode::Confirm) {
        _mode = Mode::Picker;
        _bodyDirty = _inputDirty = true;
        return;
    }
}

void ChatScreen::onEnter() {
    if (_mode == Mode::Chat) {
        if (_input.length() > 0) sendCurrent();
        return;
    }
    if (_mode == Mode::Picker) {
        if (_pickerSel != _modelIdx) {
            _pendingModelIdx = _pickerSel;
            _mode = Mode::Confirm;
            _bodyDirty = _inputDirty = true;
        } else {
            closePicker(false);
        }
        return;
    }
    if (_mode == Mode::Confirm) {
        applyModel(_pendingModelIdx);
        _mode = Mode::Chat;
        _statusDirty = _bodyDirty = _inputDirty = true;
        return;
    }
}

// ----- actions -----

void ChatScreen::sendCurrent() {
    String prompt = _input;
    _input = "";

    _conv.addUserMessage(prompt);
    _conv.addAssistantMessage(""); // placeholder we'll grow

    // Reset session filename on the very first message of this chat
    if (_sessionFile.length() == 0) {
        _sessionFile = chatstore::newSessionFilename();
        Serial.printf("[chat] new session file: %s\n", _sessionFile.c_str());
    }

    _streaming   = true;
    _scrollOffset = 0; _autoScroll = true;
    _bodyDirty = _inputDirty = true;
    renderBody();
    renderInput();

    Serial.printf("[chat] send: %s\n", prompt.c_str());
    Serial.printf("[heap] pre-send free=%u min=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());

    // Build the messages list: system prompt first, then all history.
    std::vector<ESPAI::Message> messages;
    if (_systemPrompt.length() > 0) {
        messages.push_back(ESPAI::Message(ESPAI::Role::System, _systemPrompt));
    }
    // Conversation::getMessages() returns the full history including the
    // empty assistant placeholder we just added. Drop the last one when
    // sending so the API has a fresh slate to complete.
    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i + 1 < msgs.size(); i++) messages.push_back(msgs[i]);

    ESPAI::ChatOptions options;
    options.maxTokens = 512;

    uint32_t t0 = millis();
    int chunks  = 0;

    bool ok = _ai->chatStream(messages, options,
        [&](const String& chunk, bool done) {
            chunks++;
            // Grow the in-flight assistant turn. Mutate via clear+add since
            // Conversation doesn't expose mutation otherwise.
            auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
            if (!m.empty()) m.back().content += chunk;
            if (_autoScroll) renderChat();
            if (done) {
                Serial.printf("[chat] done. %ums, %d chunks\n",
                              (unsigned)(millis() - t0), chunks);
            }
        });

    if (!ok) {
        Serial.println("[chat] FAIL");
        auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
        if (!m.empty()) m.back().content += " [error]";
    }
    _streaming = false;

    // Persist the conversation after each completed exchange.
    if (!chatstore::saveSession(_sessionFile, _conv,
                                _models[_modelIdx].slug)) {
        Serial.println("[chat] save failed");
    }

    _bodyDirty = _inputDirty = true;
}

void ChatScreen::newChat() {
    Serial.println("[chat] new session");
    _conv.clear();
    _conv.setSystemPrompt(_systemPrompt);
    _sessionFile = "";
    _input = "";
    _scrollOffset = 0; _autoScroll = true;
    _bodyDirty = _inputDirty = true;
}

void ChatScreen::openPicker() {
    _pickerSel = _modelIdx;
    _mode = Mode::Picker;
    _bodyDirty = _inputDirty = true;
}

void ChatScreen::closePicker(bool /*committed*/) {
    _mode = Mode::Chat;
    _bodyDirty = _inputDirty = true;
}

void ChatScreen::applyModel(int idx) {
    if (idx < 0 || idx >= (int)_models.size()) return;
    Serial.printf("[chat] switching to %s\n", _models[idx].slug);
    _modelIdx = idx;
    _ai->setModel(_models[idx].slug);
    // Switching model starts a new chat per spec.
    newChat();
}

// ----- rendering -----

void ChatScreen::renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);

    // Right-aligned model label, no background.
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    const char* label = _models[_modelIdx].label;
    int lw = M5Cardputer.Display.textWidth(label);
    M5Cardputer.Display.setCursor(kScreenW - kPadX - lw, 2);
    M5Cardputer.Display.print(label);

    // Left-aligned mode hint (only when not in chat)
    if (_mode != Mode::Chat) {
        const char* hint = (_mode == Mode::Picker) ? "[picker]" : "[confirm]";
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(kPadX, 2);
        M5Cardputer.Display.print(hint);
    }

    // Restore body font
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
}

void ChatScreen::renderBody() {
    switch (_mode) {
        case Mode::Chat:    renderChat();    break;
        case Mode::Picker:  renderPicker();  break;
        case Mode::Confirm: renderConfirm(); break;
    }
}

void ChatScreen::buildLines(std::vector<Line>& out) {
    const int maxPx = kScreenW - 2 * kPadX;
    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i < msgs.size(); i++) {
        const auto& m = msgs[i];
        if (m.role == ESPAI::Role::System) continue;
        uint16_t color = (m.role == ESPAI::Role::User) ? kUserColor : kAsstColor;
        bool right     = (m.role == ESPAI::Role::User);
        wrapInto(m.content, maxPx, color, right, out);
        if (i + 1 < msgs.size()) out.push_back({String(""), 0, false});
    }
}

void ChatScreen::renderChat() {
    M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);

    std::vector<Line> lines;
    lines.reserve(_conv.size() * 3);
    buildLines(lines);

    const int lh   = lineHeight();
    const int vis  = visibleLines();
    const int tot  = (int)lines.size();
    // We want the LAST (tot - _scrollOffset) line to be the bottom one.
    int endIdx   = tot - _scrollOffset;
    if (endIdx < 1) endIdx = std::min(1, tot);
    int startIdx = endIdx - vis;
    if (startIdx < 0) startIdx = 0;

    int y = bodyTop() + 2;
    for (int i = startIdx; i < endIdx && i < tot; i++) {
        const auto& ln = lines[i];
        if (ln.text.length() == 0) { y += lh; continue; }
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

void ChatScreen::renderPicker() {
    M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);
    int y = bodyTop() + 6;
    int lh = lineHeight() + 2;

    M5Cardputer.Display.setTextColor(kStatusDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, y);
    M5Cardputer.Display.print("select model:");
    y += lh + 2;

    for (size_t i = 0; i < _models.size(); i++) {
        bool sel = ((int)i == _pickerSel);
        bool cur = ((int)i == _modelIdx);
        uint16_t color = sel ? kPickerSel : kPickerIdle;
        const char* mark = sel ? "> " : "  ";
        M5Cardputer.Display.setTextColor(color, kBg);
        M5Cardputer.Display.setCursor(kPadX, y);
        M5Cardputer.Display.print(mark);
        M5Cardputer.Display.print(_models[i].label);
        if (cur) {
            M5Cardputer.Display.setTextColor(kStatusDim, kBg);
            M5Cardputer.Display.print("  (current)");
        }
        y += lh;
    }
}

void ChatScreen::renderConfirm() {
    M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);
    int y = bodyTop() + 8;
    int lh = lineHeight() + 2;

    M5Cardputer.Display.setTextColor(kUserColor, kBg);
    M5Cardputer.Display.setCursor(kPadX, y); y += lh;
    M5Cardputer.Display.print("switch to:");
    M5Cardputer.Display.setTextColor(kAsstColor, kBg);
    M5Cardputer.Display.setCursor(kPadX + 8, y); y += lh;
    M5Cardputer.Display.print(_models[_pendingModelIdx].label);

    y += 4;
    M5Cardputer.Display.setTextColor(kStatusDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, y); y += lh;
    M5Cardputer.Display.print("this clears the chat.");
    M5Cardputer.Display.setCursor(kPadX, y);
    M5Cardputer.Display.print("[enter/y] ok  [del/n] cancel");
}

void ChatScreen::renderInput() {
    const int y = kScreenH - kInputH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kInputH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);

    if (_mode != Mode::Chat) {
        // Footer hint per mode
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(kPadX, y + 3);
        if (_mode == Mode::Picker) {
            M5Cardputer.Display.print("fn+,/.  enter  del=cancel");
        } else {
            M5Cardputer.Display.print("y=switch  n/del=cancel");
        }
        return;
    }

    String shown = _input;
    int maxPx = kScreenW - 2 * kPadX - 8;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx
           && shown.length() > 0) {
        shown.remove(0, 1);
    }

    M5Cardputer.Display.setTextColor(kInputColor, kBg);
    M5Cardputer.Display.setCursor(kPadX, y + 3);
    M5Cardputer.Display.print(shown);

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
    if (_mode != Mode::Chat) return;
    const int y = kScreenH - kInputH;
    int maxPx = kScreenW - 2 * kPadX - 8;
    String shown = _input;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx
           && shown.length() > 0) {
        shown.remove(0, 1);
    }
    int cx = kPadX + M5Cardputer.Display.textWidth(shown.c_str());
    int ch = M5Cardputer.Display.fontHeight();
    M5Cardputer.Display.fillRect(cx + 1, y + 3, 8, ch, kBg);
    if (_cursorOn && !_streaming) {
        M5Cardputer.Display.fillRect(cx + 1, y + 3, 6, ch, kCursorColor);
    }
}

void ChatScreen::wrapInto(const String& s, int maxPx, uint16_t color,
                          bool right, std::vector<Line>& out) {
    const int n = (int)s.length();
    if (n == 0) { out.push_back({String(""), color, right}); return; }
    int i = 0;
    while (i < n) {
        int j = i;
        int lastBreak = -1;
        while (j < n) {
            char c = s.charAt(j);
            if (c == '\n') { lastBreak = j; j++; break; }
            String probe = s.substring(i, j + 1);
            if (M5Cardputer.Display.textWidth(probe.c_str()) > maxPx) break;
            if (c == ' ') lastBreak = j;
            j++;
        }
        int end = j;
        if (end < n && s.charAt(end) != '\n') {
            if (lastBreak > i) end = lastBreak;
        }
        if (end <= i) end = i + 1;
        String line = s.substring(i, end);
        while (line.length() > 0 && line.charAt(line.length() - 1) == ' ') {
            line.remove(line.length() - 1);
        }
        out.push_back({line, color, right});
        i = end;
        while (i < n && (s.charAt(i) == ' ' || s.charAt(i) == '\n')) i++;
    }
}

void ChatScreen::renderAll() {
    M5Cardputer.Display.fillScreen(kBg);
    renderStatus();
    renderBody();
    renderInput();
}
