#include "chat_screen.h"
#include "../storage/chat_store.h"
#include "../storage/sd_config.h"
#include "../storage/settings.h"
#include "../setup/wifi_setup.h"
#include "../setup/key_setup.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <time.h>

namespace {

// Layout
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kInputH  = 20;
constexpr int kPadX    = 4;
constexpr int kLinePad = 2;

// Palette
constexpr uint16_t kBg          = 0x0000;
constexpr uint16_t kDivider     = 0x2104;
constexpr uint16_t kUserColor   = 0xEF7D;
constexpr uint16_t kAsstColor   = 0xFD60;
constexpr uint16_t kInputColor  = 0xEF7D;
constexpr uint16_t kCursorColor = 0xFD60;
constexpr uint16_t kStreamHint  = 0x83C0;
constexpr uint16_t kStatusDim   = 0x6B4D;
constexpr uint16_t kStatusAccent= 0xFD60;
constexpr uint16_t kSelColor    = 0xFD60;
constexpr uint16_t kIdleColor   = 0xEF7D;

// History depth options
constexpr int kDepthOptions[] = {5, 10, 20, 40, 60};
constexpr int kDepthCount     = sizeof(kDepthOptions) / sizeof(int);

// Menu items (charles-curated set)
constexpr int kMenuItemCount = 9;
const char* const kMenuLabels[kMenuItemCount] = {
    "models",
    "new chat",
    "history depth",
    "system prompt",
    "wifi info",
    "add wifi",
    "set api key",
    "diagnostics",
    "exit",
};

} // namespace

ChatScreen::ChatScreen(ESPAI::OpenAICompatibleProvider* ai,
                       const String& systemPrompt,
                       const std::vector<ModelChoice>& models,
                       int initialModelIdx,
                       int initialHistoryDepth)
    : _ai(ai),
      _systemPrompt(systemPrompt),
      _models(models),
      _modelIdx(initialModelIdx),
      _historyDepth(initialHistoryDepth),
      _conv(initialHistoryDepth),
      _bodyCanvas(&M5Cardputer.Display) {
    _conv.setSystemPrompt(systemPrompt);
    // Initialize _depthSel to whichever option matches the persisted depth.
    for (int i = 0; i < kDepthCount; i++) {
        if (kDepthOptions[i] == initialHistoryDepth) { _depthSel = i; break; }
    }
}

int ChatScreen::lineHeight()   const { return _bodyCanvas.fontHeight() + kLinePad; }
int ChatScreen::bodyTop()      const { return kStatusH; }
int ChatScreen::bodyHeight()   const { return kScreenH - kStatusH - kInputH; }
int ChatScreen::visibleLines() const { return bodyHeight() / lineHeight(); }

void ChatScreen::begin() {
    // Direct-draw target: status + input rows
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);

    // Offscreen body buffer kills the fillRect-then-print flicker on streaming.
    _bodyCanvas.setColorDepth(16);
    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (!_canvasOk) {
        Serial.printf("[chat] WARN: canvas alloc failed for %dx%d\n",
                      kScreenW, bodyHeight());
    } else {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
        _bodyCanvas.fillScreen(kBg);
    }

    _statusDirty = _bodyDirty = _inputDirty = true;
}

// ============================================================================
// main tick
// ============================================================================

void ChatScreen::tick() {
    pollKeyboard();

    if (_mode == Mode::Chat && !_streaming
        && (millis() - _cursorTick) > 450) {
        _cursorTick = millis();
        _cursorOn   = !_cursorOn;
        renderCursorOnly();
    }

    // Ambient animation. Empty chat -> drives watermark motion. Streaming
    // -> drives the dot indicator. Anything else is idle (no refresh).
    bool emptyChat = (_mode == Mode::Chat && !_streaming && _conv.size() == 0);
    if (emptyChat && (millis() - _animTick) > 80) {
        _animTick = millis();
        _animPhase++;
        _bodyDirty = true;
    } else if (_streaming && (millis() - _animTick) > 180) {
        _animTick = millis();
        _animPhase++;
        _inputDirty = true;
    }

    if (_statusDirty) { renderStatus(); _statusDirty = false; }
    if (_bodyDirty)   { renderBody();   _bodyDirty   = false; }
    if (_inputDirty)  { renderInput();  _inputDirty  = false; }

    delay(5);
}

// ============================================================================
// keyboard
// ============================================================================

bool ChatScreen::repeatTick(Held& h, uint32_t initialMs, uint32_t periodMs) {
    uint32_t now = millis();
    if (!h.active) return false;
    if (now - h.firstPress < initialMs) return false;
    if (now - h.lastRepeat < periodMs)  return false;
    h.lastRepeat = now;
    return true;
}

void ChatScreen::pollKeyboard() {
    M5Cardputer.update();
    if (_streaming) return;

    auto& s = M5Cardputer.Keyboard.keysState();

    // Rising-edge: chars that just became pressed
    for (char c : s.word) {
        bool wasPrev = false;
        for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
        if (!wasPrev) onCharPressed(c, s.fn);
    }

    // Del rising + hold-repeat (50ms cadence, 400ms initial)
    if (s.del && !_prevDel) {
        onDel();
        _heldDel.firstPress = millis();
        _heldDel.lastRepeat = millis();
        _heldDel.active     = true;
    } else if (!s.del) {
        _heldDel.active = false;
    } else if (repeatTick(_heldDel)) {
        onDel();
    }

    // Enter rising only
    if (s.enter && !_prevEnter) onEnter();

    // Fn+scroll hold-repeat
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
        if      (_mode == Mode::Chat)        scrollUp();
        else if (_mode == Mode::Menu)        { if (_menuSel > 0)            { _menuSel--;   _bodyDirty = true; } }
        else if (_mode == Mode::Picker)      { if (_pickerSel > 0)          { _pickerSel--; _bodyDirty = true; } }
        else if (_mode == Mode::DepthPicker) { if (_depthSel > 0)           { _depthSel--;  _bodyDirty = true; } }
        else if (_mode == Mode::Info)        { if (_infoScroll > 0)         { _infoScroll--;_bodyDirty = true; } }
    }
    if (downHeld && !_heldScrollDown.active) {
        _heldScrollDown.firstPress = millis(); _heldScrollDown.lastRepeat = millis(); _heldScrollDown.active = true;
    } else if (!downHeld) {
        _heldScrollDown.active = false;
    } else if (repeatTick(_heldScrollDown, 350, 60)) {
        if      (_mode == Mode::Chat)        scrollDown();
        else if (_mode == Mode::Menu)        { if (_menuSel + 1 < kMenuItemCount)        { _menuSel++;    _bodyDirty = true; } }
        else if (_mode == Mode::Picker)      { if (_pickerSel + 1 < (int)_models.size()) { _pickerSel++;  _bodyDirty = true; } }
        else if (_mode == Mode::DepthPicker) { if (_depthSel + 1 < kDepthCount)          { _depthSel++;   _bodyDirty = true; } }
        else if (_mode == Mode::Info)        { _infoScroll++; _bodyDirty = true; }
    }

    _prevWord  = s.word;
    _prevDel   = s.del;
    _prevEnter = s.enter;
}

// ============================================================================
// rising-edge handlers
// ============================================================================

void ChatScreen::onCharPressed(char c, bool fn) {
    // Top-left key ('`' / '~') is wired as ESC. Bare or with Fn, both
    // toggle the menu from chat / close it / back out of any submodal.
    // Trade-off: '`' and '~' are not typeable in chat input. The system
    // prompt enforces plain-text replies anyway.
    if (c == '`' || c == '~') {
        if      (_mode == Mode::Chat) openMenu();
        else if (_mode == Mode::Menu) closeMenu();
        else                          onDel();
        return;
    }

    if (_mode == Mode::Chat) {
        if (fn) {
            if (c == 'n' || c == 'N') {
                confirmDestructive("new chat?",
                                   "all turns will be cleared.",
                                   [this]{ newChat(); }, Mode::Chat);
                return;
            }
            if (c == 'm' || c == 'M') { openPicker(Mode::Chat); return; }
            if (c == 's' || c == 'S') { openMenu(); return; }
            if (c == ',' || c == ';') { scrollUp();   return; }
            if (c == '.' || c == '/') { scrollDown(); return; }
            return; // swallow other Fn+x so the bare char doesn't show up in input
        }
        _input += c;
        _inputDirty = true;
        return;
    }

    if (_mode == Mode::Menu) {
        if (c == ',') { if (_menuSel > 0)                       { _menuSel--; _bodyDirty = true; } return; }
        if (c == '.') { if (_menuSel + 1 < kMenuItemCount)      { _menuSel++; _bodyDirty = true; } return; }
        return;
    }

    if (_mode == Mode::Picker) {
        if (c == ',') { if (_pickerSel > 0)                       { _pickerSel--; _bodyDirty = true; } return; }
        if (c == '.') { if (_pickerSel + 1 < (int)_models.size()) { _pickerSel++; _bodyDirty = true; } return; }
        return;
    }

    if (_mode == Mode::DepthPicker) {
        if (c == ',') { if (_depthSel > 0)                 { _depthSel--; _bodyDirty = true; } return; }
        if (c == '.') { if (_depthSel + 1 < kDepthCount)   { _depthSel++; _bodyDirty = true; } return; }
        return;
    }

    if (_mode == Mode::Confirm) {
        if (c == 'y' || c == 'Y') { resolveConfirm(true);  return; }
        if (c == 'n' || c == 'N') { resolveConfirm(false); return; }
        return;
    }

    if (_mode == Mode::Info) {
        // any printable does nothing; backspace exits
        return;
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
    if (_mode == Mode::Menu)         { closeMenu();              return; }
    if (_mode == Mode::Picker
     || _mode == Mode::DepthPicker
     || _mode == Mode::Info)         { _mode = _modalReturnTo; _statusDirty = _bodyDirty = _inputDirty = true; return; }
    if (_mode == Mode::Confirm)      { resolveConfirm(false);    return; }
}

void ChatScreen::onEnter() {
    if (_mode == Mode::Chat) {
        if (_input.length() > 0) sendCurrent();
        return;
    }
    if (_mode == Mode::Menu) {
        switch (_menuSel) {
            case 0: openPicker(Mode::Menu); break;
            case 1: confirmDestructive("clear chat?",
                                       "all turns will be discarded.",
                                       [this]{ newChat(); _mode = Mode::Chat;
                                               _statusDirty = _bodyDirty = _inputDirty = true; },
                                       Mode::Menu); break;
            case 2: openDepth(); break;
            case 3: buildSystemPromptLines();
                    openInfoScreen("system prompt"); break;
            case 4: buildWiFiLines();
                    openInfoScreen("wifi info"); break;
            case 5: { // add wifi: run scan/pick/connect flow with cancel allowed
                bool added = wifi_setup::run(/*allowCancel=*/true);
                Serial.printf("[menu] add wifi -> %s\n", added ? "saved" : "cancelled");
                _mode = Mode::Menu;
                _statusDirty = _bodyDirty = _inputDirty = true;
                break;
            }
            case 6: { // set api key: web-form flow with cancel allowed
                if (key_setup::run(/*allowCancel=*/true)) {
                    String k = sdcfg::loadOpenRouterKey();
                    if (k.length() > 0) {
                        _ai->setApiKey(k);
                        Serial.println("[menu] api key updated");
                    }
                }
                _mode = Mode::Menu;
                _statusDirty = _bodyDirty = _inputDirty = true;
                break;
            }
            case 7: buildDiagnosticsLines();
                    openInfoScreen("diagnostics"); break;
            case 8: closeMenu(); break;
        }
        return;
    }
    if (_mode == Mode::Picker) {
        if (_pickerSel != _modelIdx) {
            _pendingModelIdx = _pickerSel;
            String q = String("switch to ") + _models[_pickerSel].label + "?";
            confirmDestructive(q, "this clears the chat.",
                               [this]{
                                   applyModel(_pendingModelIdx);
                                   _mode = Mode::Chat;
                                   _statusDirty = _bodyDirty = _inputDirty = true;
                               },
                               Mode::Picker);
        } else {
            _mode = _modalReturnTo;
            _statusDirty = _bodyDirty = _inputDirty = true;
        }
        return;
    }
    if (_mode == Mode::DepthPicker) {
        applyDepth(kDepthOptions[_depthSel]);
        _mode = _modalReturnTo;
        _statusDirty = _bodyDirty = _inputDirty = true;
        return;
    }
    if (_mode == Mode::Confirm) { resolveConfirm(true); return; }
    if (_mode == Mode::Info)    { _mode = _modalReturnTo; _statusDirty = _bodyDirty = _inputDirty = true; return; }
}

// ============================================================================
// mode transitions
// ============================================================================

void ChatScreen::openMenu() {
    _menuSel = 0;
    _mode    = Mode::Menu;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::closeMenu() {
    _mode = Mode::Chat;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::openPicker(Mode returnTo) {
    _modalReturnTo = returnTo;
    _pickerSel     = _modelIdx;
    _mode          = Mode::Picker;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::openDepth() {
    _modalReturnTo = Mode::Menu;
    // _depthSel was initialized in constructor / updated by applyDepth
    _mode          = Mode::DepthPicker;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::openInfoScreen(const String& title) {
    _modalReturnTo = Mode::Menu;
    _infoTitle     = title;
    _infoScroll    = 0;
    _mode          = Mode::Info;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::confirmDestructive(const String& q, const String& detail,
                                    std::function<void()> onYes, Mode returnTo) {
    _confirmQ      = q;
    _confirmD      = detail;
    _confirmOnYes  = onYes;
    _confirmReturn = returnTo;
    _mode          = Mode::Confirm;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void ChatScreen::resolveConfirm(bool yes) {
    auto cb = _confirmOnYes;
    Mode r  = _confirmReturn;
    _confirmOnYes = nullptr;
    if (yes && cb) cb();
    else {
        _mode = r;
        _statusDirty = _bodyDirty = _inputDirty = true;
    }
}

// ============================================================================
// actions
// ============================================================================

void ChatScreen::sendCurrent() {
    String prompt = _input;
    _input = "";

    _conv.addUserMessage(prompt);
    _conv.addAssistantMessage("");

    if (_sessionFile.length() == 0) {
        _sessionFile = chatstore::newSessionFilename();
        Serial.printf("[chat] new session file: %s\n", _sessionFile.c_str());
    }

    _streaming    = true;
    _scrollOffset = 0;
    _autoScroll   = true;
    _bodyDirty    = _inputDirty = true;
    renderBody();
    renderInput();

    Serial.printf("[chat] send: %s\n", prompt.c_str());
    Serial.printf("[heap] pre-send free=%u min=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());

    std::vector<ESPAI::Message> messages;
    if (_systemPrompt.length() > 0) {
        messages.push_back(ESPAI::Message(ESPAI::Role::System, _systemPrompt));
    }
    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i + 1 < msgs.size(); i++) messages.push_back(msgs[i]);

    ESPAI::ChatOptions options;
    options.maxTokens = 512;

    uint32_t t0 = millis();
    int chunks  = 0;

    bool ok = _ai->chatStream(messages, options,
        [&](const String& chunk, bool done) {
            chunks++;
            auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
            if (!m.empty()) m.back().content += chunk;
            if (_autoScroll) renderBody();
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
    _sessionFile  = "";
    _input        = "";
    _scrollOffset = 0;
    _autoScroll   = true;
    _bodyDirty    = _inputDirty = true;
}

void ChatScreen::applyModel(int idx) {
    if (idx < 0 || idx >= (int)_models.size()) return;
    Serial.printf("[chat] switching model: %s\n", _models[idx].slug);
    _modelIdx = idx;
    _ai->setModel(_models[idx].slug);
    newChat();
}

void ChatScreen::applyDepth(int depth) {
    Serial.printf("[chat] history depth -> %d\n", depth);
    _historyDepth = depth;
    _conv.setMaxMessages((size_t)depth);
    settings::setHistoryDepth(depth);
}

void ChatScreen::scrollUp() {
    _scrollOffset++;
    if (_scrollOffset > 500) _scrollOffset = 500;
    _autoScroll = (_scrollOffset == 0);
    _bodyDirty  = true;
}
void ChatScreen::scrollDown() {
    if (_scrollOffset > 0) _scrollOffset--;
    if (_scrollOffset == 0) _autoScroll = true;
    _bodyDirty = true;
}

// ============================================================================
// rendering
// ============================================================================

void ChatScreen::renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);

    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);

    // Right side: HH:MM · model.  Only includes time if NTP has synced.
    String right;
    struct tm t;
    if (getLocalTime(&t, 5)) {
        char b[8];
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        right  = b;
        right += " ";
        right += (char)0x07;  // we'll color-swap via two prints below
    }
    // Render time + dot in dim, then label in accent so the eye lands on
    // the model. Two-pass print so colors differ within a single line.
    int rxRight = kScreenW - kPadX;
    int labelW  = M5Cardputer.Display.textWidth(_models[_modelIdx].label);
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    M5Cardputer.Display.setCursor(rxRight - labelW, 2);
    M5Cardputer.Display.print(_models[_modelIdx].label);

    if (right.length() > 0) {
        right = String(right.substring(0, right.length() - 2)) + " . ";
        int tw = M5Cardputer.Display.textWidth(right.c_str());
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(rxRight - labelW - tw, 2);
        M5Cardputer.Display.print(right);
    }

    if (_mode != Mode::Chat) {
        const char* hint = "";
        switch (_mode) {
            case Mode::Menu:        hint = "menu";       break;
            case Mode::Picker:      hint = "models";     break;
            case Mode::DepthPicker: hint = "depth";      break;
            case Mode::Confirm:     hint = "confirm";    break;
            case Mode::Info:        hint = _infoTitle.length() ? _infoTitle.c_str() : "info"; break;
            default: break;
        }
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(kPadX, 2);
        M5Cardputer.Display.print("/");
        M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
        M5Cardputer.Display.print(hint);
    } else {
        // Tiny LED-style dot in the corner when idle in chat.
        M5Cardputer.Display.fillRect(kPadX, 4, 3, 3, kStatusAccent);
    }

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
}

void ChatScreen::renderBody() {
    if (!_canvasOk) {
        M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);
        return;
    }
    _bodyCanvas.fillScreen(kBg);
    // Hairline top frame inside the body region.
    _bodyCanvas.drawLine(0, 0, kScreenW, 0, kDivider);

    switch (_mode) {
        case Mode::Chat:
            if (_conv.size() == 0 && !_streaming) renderEmptyChat();
            else                                   renderChatBody();
            break;
        case Mode::Menu:        renderMenuBody();    break;
        case Mode::Picker:      renderPickerBody();  break;
        case Mode::DepthPicker: renderDepthBody();   break;
        case Mode::Confirm:     renderConfirmBody(); break;
        case Mode::Info:        renderInfoBody();    break;
    }
    _bodyCanvas.pushSprite(0, bodyTop());
}

void ChatScreen::renderEmptyChat() {
    // Watermark wordmark, faint, upper third.
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(2);
    _bodyCanvas.setTextColor(0x2104, kBg);
    const char* wm = "CARDPUTER";
    int ww = _bodyCanvas.textWidth(wm);
    _bodyCanvas.setCursor((kScreenW - ww) / 2, 12);
    _bodyCanvas.print(wm);

    _bodyCanvas.setTextSize(1);
    _bodyCanvas.setTextColor(0x4208, kBg);
    const char* sub = "L  L  M";
    int sw = _bodyCanvas.textWidth(sub);
    _bodyCanvas.setCursor((kScreenW - sw) / 2, 44);
    _bodyCanvas.print(sub);

    // Drifting "scan line" — a thin dim horizontal that travels slowly.
    int travel = bodyHeight() - 24;
    int p      = (_animPhase / 3) % (travel * 2);
    int yLine  = (p < travel) ? (12 + p) : (12 + (travel * 2 - p));
    _bodyCanvas.drawLine(18, yLine, kScreenW - 18, yLine, 0x4208);
    // A brighter pip on the line, drifting horizontally too for life.
    int pipX = 18 + (_animPhase * 3) % (kScreenW - 36);
    _bodyCanvas.fillRect(pipX - 1, yLine - 1, 3, 3, kStatusAccent);

    // Bottom hint with blinking dot
    _bodyCanvas.setFont(&fonts::Font0);
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    String hint = "type to begin  /  esc for menu";
    int hw = _bodyCanvas.textWidth(hint.c_str());
    _bodyCanvas.setCursor((kScreenW - hw) / 2, bodyHeight() - 12);
    _bodyCanvas.print(hint);
    if ((_animPhase / 6) % 2 == 0) {
        _bodyCanvas.fillRect((kScreenW - hw) / 2 - 8, bodyHeight() - 10, 3, 3, kStatusAccent);
    }
    // Restore default font
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(1);
}

void ChatScreen::buildLines(std::vector<Line>& out) {
    const int maxPx = kScreenW - 2 * kPadX;
    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i < msgs.size(); i++) {
        const auto& m = msgs[i];
        if (m.role == ESPAI::Role::System) continue;
        uint16_t color = (m.role == ESPAI::Role::User) ? kUserColor : kAsstColor;
        bool right     = (m.role == ESPAI::Role::User);
        wrapIntoCanvas(m.content, maxPx, color, right, out);
        if (i + 1 < msgs.size()) out.push_back({String(""), 0, false});
    }
}

void ChatScreen::renderChatBody() {
    std::vector<Line> lines;
    lines.reserve(_conv.size() * 3);
    buildLines(lines);

    const int lh   = lineHeight();
    const int vis  = visibleLines();
    const int tot  = (int)lines.size();
    int endIdx     = tot - _scrollOffset;
    if (endIdx < 1) endIdx = std::min(1, tot);
    int startIdx   = endIdx - vis;
    if (startIdx < 0) startIdx = 0;

    int y = 2;
    for (int i = startIdx; i < endIdx && i < tot; i++) {
        const auto& ln = lines[i];
        if (ln.text.length() == 0) { y += lh; continue; }
        _bodyCanvas.setTextColor(ln.color, kBg);
        int x = kPadX;
        if (ln.rightAlign) {
            int w = _bodyCanvas.textWidth(ln.text.c_str());
            x = kScreenW - kPadX - w;
            if (x < kPadX) x = kPadX;
        }
        _bodyCanvas.setCursor(x, y);
        _bodyCanvas.print(ln.text);
        y += lh;
    }
}

void ChatScreen::renderMenuBody() {
    int lh = lineHeight() + 2;
    int vis = (bodyHeight() - 6) / lh;
    if (vis < 1) vis = 1;
    int start = 0;
    if (_menuSel >= vis) start = _menuSel - vis + 1;
    int end = start + vis;
    if (end > kMenuItemCount) end = kMenuItemCount;

    int y = 6;
    for (int i = start; i < end; i++) {
        bool sel = (i == _menuSel);
        uint16_t color = sel ? kSelColor : kIdleColor;
        if (sel) {
            // bar-style selection marker, left-of-row
            _bodyCanvas.fillRect(kPadX, y + 1, 3, lh - 4, kSelColor);
        }
        _bodyCanvas.setTextColor(color, kBg);
        _bodyCanvas.setCursor(kPadX + 9, y);
        _bodyCanvas.print(kMenuLabels[i]);
        y += lh;
    }
    if (kMenuItemCount > vis) {
        _bodyCanvas.setTextColor(kStatusDim, kBg);
        if (start > 0) {
            _bodyCanvas.setCursor(kScreenW - 10, 6);
            _bodyCanvas.print("^");
        }
        if (end < kMenuItemCount) {
            _bodyCanvas.setCursor(kScreenW - 10, bodyHeight() - 14);
            _bodyCanvas.print("v");
        }
    }
}

void ChatScreen::renderPickerBody() {
    int y  = 6;
    int lh = lineHeight() + 2;
    for (size_t i = 0; i < _models.size(); i++) {
        bool sel = ((int)i == _pickerSel);
        bool cur = ((int)i == _modelIdx);
        uint16_t color = sel ? kSelColor : kIdleColor;
        if (sel) _bodyCanvas.fillRect(kPadX, y + 1, 3, lh - 4, kSelColor);
        _bodyCanvas.setTextColor(color, kBg);
        _bodyCanvas.setCursor(kPadX + 9, y);
        _bodyCanvas.print(_models[i].label);
        if (cur) {
            _bodyCanvas.setTextColor(kStatusDim, kBg);
            _bodyCanvas.print("  (current)");
        }
        y += lh;
    }
}

void ChatScreen::renderDepthBody() {
    int y  = 6;
    int lh = lineHeight() + 2;
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    _bodyCanvas.setCursor(kPadX, y);
    _bodyCanvas.print("messages kept:");
    y += lh;
    for (int i = 0; i < kDepthCount; i++) {
        bool sel = (i == _depthSel);
        bool cur = (kDepthOptions[i] == _historyDepth);
        uint16_t color = sel ? kSelColor : kIdleColor;
        if (sel) _bodyCanvas.fillRect(kPadX + 6, y + 1, 3, lh - 4, kSelColor);
        _bodyCanvas.setTextColor(color, kBg);
        _bodyCanvas.setCursor(kPadX + 15, y);
        _bodyCanvas.print(String(kDepthOptions[i]));
        if (cur) {
            _bodyCanvas.setTextColor(kStatusDim, kBg);
            _bodyCanvas.print("  (current)");
        }
        y += lh;
    }
}

void ChatScreen::renderConfirmBody() {
    int y  = 8;
    int lh = lineHeight() + 2;
    _bodyCanvas.setTextColor(kUserColor, kBg);
    _bodyCanvas.setCursor(kPadX, y);
    _bodyCanvas.print(_confirmQ);
    y += lh + 2;
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    _bodyCanvas.setCursor(kPadX, y);
    _bodyCanvas.print(_confirmD);
    y += lh + 4;
    _bodyCanvas.setTextColor(kAsstColor, kBg);
    _bodyCanvas.setCursor(kPadX, y);
    _bodyCanvas.print("[enter/y] ok  [del/n] no");
}

void ChatScreen::renderInfoBody() {
    int lh = lineHeight() + 1;
    int y  = 4;
    const int vis = bodyHeight() / lh;
    int start = _infoScroll;
    if (start < 0) start = 0;
    if (start > (int)_infoLines.size()) start = _infoLines.size();
    int end = start + vis;
    if (end > (int)_infoLines.size()) end = _infoLines.size();
    for (int i = start; i < end; i++) {
        const String& s = _infoLines[i];
        bool isLabel = s.startsWith("---");
        _bodyCanvas.setTextColor(isLabel ? kStatusDim : kIdleColor, kBg);
        _bodyCanvas.setCursor(kPadX, y);
        _bodyCanvas.print(s);
        y += lh;
    }
}

void ChatScreen::renderInput() {
    const int y = kScreenH - kInputH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kInputH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);

    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);

    auto footerHint = [&](const char* h) {
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(kPadX, y + 6);
        M5Cardputer.Display.print(h);
    };

    switch (_mode) {
        case Mode::Menu:        footerHint("fn+,/.  enter=open  del=back"); goto restore;
        case Mode::Picker:      footerHint("fn+,/.  enter=switch  del=back"); goto restore;
        case Mode::DepthPicker: footerHint("fn+,/.  enter=apply  del=back"); goto restore;
        case Mode::Confirm:     footerHint("y/enter=yes  n/del=no");        goto restore;
        case Mode::Info:        footerHint("fn+,/. scroll  del=back");      goto restore;
        case Mode::Chat: break;
    }

    // CHAT mode input
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    {
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
            // Animated pulsing dots: " .  " / "  . " / "   ." cycling.
            static const char* kDotFrames[4] = {".  ", " . ", "  .", " . "};
            const char* hint = kDotFrames[_animPhase % 4];
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
    return;

restore:
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
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

void ChatScreen::wrapIntoCanvas(const String& s, int maxPx, uint16_t color,
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
            if (_bodyCanvas.textWidth(probe.c_str()) > maxPx) break;
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

static String kvFmt(const char* k, const String& v) {
    String s = String(k);
    while (s.length() < 9) s += ' ';
    s += v;
    return s;
}

void ChatScreen::buildSystemPromptLines() {
    _infoLines.clear();
    _infoLines.push_back(kvFmt("length", String((unsigned)_systemPrompt.length()) + " chars"));
    _infoLines.push_back("--- text ---");
    // Word-wrap the prompt to ~38 cols for the small display.
    String sp = _systemPrompt;
    while (sp.length() > 0) {
        int take = sp.length() > 28 ? 28 : sp.length();
        // try to break on a space
        if (take < (int)sp.length()) {
            int sp_break = sp.lastIndexOf(' ', take);
            if (sp_break > 4) take = sp_break;
        }
        _infoLines.push_back(sp.substring(0, take));
        sp = sp.substring(take);
        sp.trim();
    }
    _infoLines.push_back("");
    _infoLines.push_back("override on sd:");
    _infoLines.push_back("/CardputerLLM/system.txt");
}

void ChatScreen::buildWiFiLines() {
    _infoLines.clear();
    _infoLines.push_back(kvFmt("ssid", WiFi.SSID()));
    _infoLines.push_back(kvFmt("ip", WiFi.localIP().toString()));
    _infoLines.push_back(kvFmt("rssi", String(WiFi.RSSI()) + " dBm"));
    _infoLines.push_back(kvFmt("gw", WiFi.gatewayIP().toString()));
    _infoLines.push_back(kvFmt("dns", WiFi.dnsIP().toString()));
    _infoLines.push_back(kvFmt("mac", WiFi.macAddress()));
    time_t now = time(nullptr);
    if (now > 1577836800) {
        struct tm t; gmtime_r(&now, &t);
        char b[24];
        snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02dZ",
                 t.tm_year+1900, t.tm_mon+1, t.tm_mday,
                 t.tm_hour, t.tm_min);
        _infoLines.push_back(kvFmt("utc", b));
    } else {
        _infoLines.push_back(kvFmt("utc", "unsynced"));
    }
}

void ChatScreen::buildDiagnosticsLines() {
    _infoLines.clear();
    _infoLines.push_back("--- build ---");
    _infoLines.push_back(kvFmt("version", "ph8-dev"));
    _infoLines.push_back("--- chat ---");
    _infoLines.push_back(kvFmt("model", _models[_modelIdx].label));
    _infoLines.push_back(kvFmt("depth", String(_historyDepth)));
    _infoLines.push_back(kvFmt("msgs", String((unsigned)_conv.size())));
    _infoLines.push_back(kvFmt("session", _sessionFile.length() ? _sessionFile : String("(none)")));
    _infoLines.push_back("--- heap ---");
    _infoLines.push_back(kvFmt("free", String((unsigned)ESP.getFreeHeap())));
    _infoLines.push_back(kvFmt("min", String((unsigned)ESP.getMinFreeHeap())));
    _infoLines.push_back(kvFmt("largest", String((unsigned)ESP.getMaxAllocHeap())));
    _infoLines.push_back("--- uptime ---");
    uint32_t s = millis() / 1000;
    char b[32];
    snprintf(b, sizeof(b), "%uh %um %us",
             (unsigned)(s/3600), (unsigned)((s/60)%60), (unsigned)(s%60));
    _infoLines.push_back(kvFmt("up", b));
}

void ChatScreen::renderAll() {
    M5Cardputer.Display.fillScreen(kBg);
    renderStatus();
    renderBody();
    renderInput();
}
