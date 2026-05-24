// Ask mode chat implementation.
//
// See ask_screen.h for the high-level contract.

#include "ask_screen.h"
#include "boot_ui.h"
#include "styled_text.h"
#include "../storage/note_store.h"
#include "../storage/snapshot.h"
#include "../storage/sd_config.h"
#include "../storage/settings.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <ESPAI.h>
#include <WiFi.h>
#include <vector>
#include <functional>

namespace {

// ---------- layout ----------
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kInputH  = 20;
constexpr int kPadX    = 4;
constexpr int kLinePad = 2;

// ---------- palette (same as CardputerLLM/chat_screen) ----------
constexpr uint16_t kBg          = 0x0000;
constexpr uint16_t kDivider     = 0x2104;
constexpr uint16_t kUserColor   = 0xEF7D;
constexpr uint16_t kAsstColor   = 0xFE40;
constexpr uint16_t kInputColor  = 0xEF7D;
constexpr uint16_t kCursorColor = 0xFE40;
constexpr uint16_t kStreamHint  = 0x83C0;
constexpr uint16_t kStatusDim   = 0x6B4D;
constexpr uint16_t kStatusAccent= 0xFE40;
constexpr uint16_t kSelColor    = 0xFE40;
constexpr uint16_t kIdleColor   = 0xEF7D;
constexpr uint16_t kDim         = 0x6B4D;

constexpr const char* kBaseUrl =
    "https://openrouter.ai/api/v1/chat/completions";

struct ModelChoice {
    const char* slug;
    const char* label;
};

// Same trio CardputerLLM uses for chat. Default to sonnet-4.5 (good
// at instruction-following with quoted context).
const std::vector<ModelChoice> kAskModels = {
    {"openai/gpt-5",                "gpt-5"},
    {"anthropic/claude-sonnet-4.5", "sonnet-4.5"},
    {"google/gemini-2.5-pro",       "gemini-2.5"},
};
constexpr int kDefaultAskModelIdx = 1;

enum class Mode {
    Chat,
    Picker,
};

// ---------------- preflight ----------------

// Editorial section header in the launcher-card serif italic. Same
// flanking-hairlines pattern as boot_ui::sectionHeader; used in the
// Verbatim-mode screens so the editorial treatment carries through.
void editorialHeader(const char* title, uint16_t color) {
    constexpr int kPad = 6;
    constexpr int kH   = 18;
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kH, kBg);
    M5Cardputer.Display.setTextSize(1);
    int midY    = kH / 2;
    int leftEnd = kPad + 10;
    M5Cardputer.Display.drawLine(kPad, midY, leftEnd, midY, color);
    M5Cardputer.Display.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextColor(color, kBg);
    M5Cardputer.Display.drawString(title, leftEnd + 4, 1);
    int tw = M5Cardputer.Display.textWidth(title);
    int rightStart = leftEnd + 4 + tw + 4;
    M5Cardputer.Display.drawLine(rightStart, midY, kScreenW - kPad, midY, color);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

bool runPreflight(int memoCount, uint32_t tokenEstimate) {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader("ask mode . ready?", kStatusAccent);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    char line[48];
    snprintf(line, sizeof(line), "%d memo%s loaded",
             memoCount, memoCount == 1 ? "" : "s");
    M5Cardputer.Display.setTextColor(kIdleColor, kBg);
    int w = M5Cardputer.Display.textWidth(line);
    M5Cardputer.Display.setCursor((kScreenW - w) / 2, 22);
    M5Cardputer.Display.print(line);

    char line2[48];
    snprintf(line2, sizeof(line2), "~%u tokens", (unsigned)tokenEstimate);
    bool warn = tokenEstimate > 50000;
    M5Cardputer.Display.setTextColor(warn ? 0xFD00 : kStatusAccent, kBg);
    int w2 = M5Cardputer.Display.textWidth(line2);
    M5Cardputer.Display.setCursor((kScreenW - w2) / 2, 44);
    M5Cardputer.Display.print(line2);

    if (warn) {
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(0xFD00, kBg);
        const char* h = "heavy context . responses may be slow";
        int hw = M5Cardputer.Display.textWidth(h);
        M5Cardputer.Display.setCursor((kScreenW - hw) / 2, 62);
        M5Cardputer.Display.print(h);
        M5Cardputer.Display.setFont(&fonts::Font2);
    }

    int hy = kScreenH - kInputH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 4);
    M5Cardputer.Display.print("[enter]  start asking");
    M5Cardputer.Display.setCursor(kPadX, hy + 13);
    M5Cardputer.Display.print("[del]    back to notes");
    M5Cardputer.Display.setFont(&fonts::Font2);

    M5Cardputer.update();
    auto& s0 = M5Cardputer.Keyboard.keysState();
    std::vector<char> prevWord = s0.word;
    bool prevDel   = s0.del;
    bool prevEnter = s0.enter;

    while (true) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();
        if (s.enter && !prevEnter) return true;
        if (s.del   && !prevDel)   return false;
        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            if (c == '`' || c == '~') {
                while (M5Cardputer.Keyboard.isPressed()) {
                    M5Cardputer.update(); delay(10);
                }
                return false;
            }
        }
        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

// ---------------- AskScreen ----------------

class AskScreen {
public:
    AskScreen(const String& apiKey,
              const String& systemPrompt,
              int memoCount);

    void runLoop();

private:
    void renderAll();
    void renderStatus();
    void renderBody();
    void renderInput();
    void renderCursorOnly();

    void renderChatBody();
    void renderPickerBody();
    void renderEmptyBody();

    void pollKeyboard();
    void onCharRising(char c, bool fn);
    void onDel();
    void onEnter();

    void scrollUp();
    void scrollDown();

    void sendCurrent();
    bool handleSlashCommand(const String& cmd);
    void addLocalExchange(const String& userMsg, const String& assistantMsg);

    void openPicker();
    void closePicker();
    void applyModel(int idx);

    int  visibleLines() const;
    int  lineHeight()   const;
    int  bodyTop()      const;
    int  bodyHeight()   const;

    // ---- deps ----
    String                                 _apiKey;
    String                                 _systemPrompt;
    int                                    _memoCount;
    ESPAI::OpenAICompatibleProvider*       _ai = nullptr;
    int                                    _modelIdx = kDefaultAskModelIdx;
    ESPAI::Conversation                    _conv;

    // ---- state ----
    Mode  _mode         = Mode::Chat;
    String _input;
    int   _pickerSel    = 0;
    int   _scrollOffset = 0;
    bool  _autoScroll   = true;
    bool  _streaming    = false;
    bool  _cancelStream = false;
    bool  _exit         = false;
    bool  _bodyDirty    = true;
    bool  _statusDirty  = true;
    bool  _inputDirty   = true;

    // ---- keyboard ----
    std::vector<char> _prevWord;
    bool _prevDel   = false;
    bool _prevEnter = false;

    // ---- cursor blink ----
    uint32_t _cursorTick = 0;
    bool     _cursorOn   = true;

    // ---- streaming indicator anim ----
    uint32_t _animTick  = 0;
    uint32_t _animPhase = 0;

    // ---- offscreen body ----
    M5Canvas _bodyCanvas;
    bool     _canvasOk = false;
};

AskScreen::AskScreen(const String& apiKey,
                     const String& systemPrompt,
                     int memoCount)
    : _apiKey(apiKey),
      _systemPrompt(systemPrompt),
      _memoCount(memoCount),
      _conv(40),
      _bodyCanvas(&M5Cardputer.Display) {
    _conv.setSystemPrompt(systemPrompt);

    ESPAI::OpenAICompatibleConfig cfg;
    cfg.name    = "OpenRouter";
    cfg.baseUrl = kBaseUrl;
    cfg.apiKey  = apiKey;
    cfg.model   = kAskModels[_modelIdx].slug;
    _ai = new ESPAI::OpenAICompatibleProvider(cfg);
}

int AskScreen::lineHeight()   const { return _bodyCanvas.fontHeight() + kLinePad; }
int AskScreen::bodyTop()      const { return kStatusH; }
int AskScreen::bodyHeight()   const { return kScreenH - kStatusH - kInputH; }
int AskScreen::visibleLines() const { return bodyHeight() / lineHeight(); }

void AskScreen::runLoop() {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);

    _bodyCanvas.setColorDepth(16);
    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
        _bodyCanvas.fillScreen(kBg);
    } else {
        Serial.println("[ask] WARN canvas alloc failed");
    }

    _statusDirty = _bodyDirty = _inputDirty = true;
    Serial.printf("[ask] heap pre-loop free=%u\n",
                  (unsigned)ESP.getFreeHeap());

    while (!_exit) {
        pollKeyboard();

        if (_mode == Mode::Chat && !_streaming
            && (millis() - _cursorTick) > 450) {
            _cursorTick = millis();
            _cursorOn   = !_cursorOn;
            renderCursorOnly();
        }
        if (_streaming && (millis() - _animTick) > 180) {
            _animTick = millis();
            _animPhase++;
            _inputDirty = true;
        }

        if (_statusDirty) { renderStatus(); _statusDirty = false; }
        if (_bodyDirty)   { renderBody();   _bodyDirty   = false; }
        if (_inputDirty)  { renderInput();  _inputDirty  = false; }
        delay(8);
    }

    if (_canvasOk) _bodyCanvas.deleteSprite();
    delete _ai;
    _ai = nullptr;
    Serial.println("[ask] exit");
}

// ---------- keyboard ----------

void AskScreen::pollKeyboard() {
    M5Cardputer.update();
    if (_streaming) return;

    auto& s = M5Cardputer.Keyboard.keysState();

    for (char c : s.word) {
        bool wasPrev = false;
        for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
        if (!wasPrev) onCharRising(c, s.fn);
    }
    if (s.del   && !_prevDel)   onDel();
    if (s.enter && !_prevEnter) onEnter();

    // Fn+,/. or ;// : scroll
    auto fnHas = [&](char target) {
        if (!s.fn) return false;
        for (char c : s.word) if (c == target) return true;
        return false;
    };
    static uint32_t lastScroll = 0;
    if (millis() - lastScroll > 80) {
        bool up   = fnHas(',') || fnHas(';');
        bool down = fnHas('.') || fnHas('/');
        if (up)   { scrollUp();   lastScroll = millis(); }
        if (down) { scrollDown(); lastScroll = millis(); }
    }

    _prevWord  = s.word;
    _prevDel   = s.del;
    _prevEnter = s.enter;
}

void AskScreen::onCharRising(char c, bool fn) {
    // backtick/tilde -- in chat mode it cancels stream or exits; in
    // picker it backs out.
    if (c == '`' || c == '~') {
        if (_mode == Mode::Picker) { closePicker(); return; }
        // In chat, only triggers exit if NOT streaming. During stream,
        // cancellation happens inside sendCurrent's callback.
        _exit = true;
        return;
    }

    if (_mode == Mode::Picker) {
        if (c == ',' || c == ';') {
            if (_pickerSel > 0) { _pickerSel--; _bodyDirty = true; }
        } else if (c == '.' || c == '/') {
            if (_pickerSel + 1 < (int)kAskModels.size()) {
                _pickerSel++; _bodyDirty = true;
            }
        }
        return;
    }

    // Chat mode
    if (fn) {
        if (c == 'm' || c == 'M') { openPicker(); return; }
        // Fn+x other than scroll/menu: swallow so the bare char doesn't
        // land in input.
        if (c == ',' || c == ';' || c == '.' || c == '/') return;
        return;
    }
    _input += c;
    _inputDirty = true;
}

void AskScreen::onDel() {
    if (_mode == Mode::Picker) { closePicker(); return; }
    if (_input.length() > 0) {
        _input.remove(_input.length() - 1);
        _inputDirty = true;
        return;
    }
    // Input is empty: backspace exits the screen.
    _exit = true;
}

void AskScreen::onEnter() {
    if (_mode == Mode::Picker) {
        if (_pickerSel != _modelIdx) applyModel(_pickerSel);
        closePicker();
        return;
    }
    if (_input.length() == 0) return;
    if (_input.charAt(0) == '/') {
        String trimmed = _input; trimmed.trim();
        if (handleSlashCommand(trimmed)) { _input = ""; _inputDirty = true; return; }
    }
    sendCurrent();
}

// ---------- actions ----------

void AskScreen::scrollUp() {
    _scrollOffset++;
    if (_scrollOffset > 500) _scrollOffset = 500;
    _autoScroll = (_scrollOffset == 0);
    _bodyDirty = true;
}
void AskScreen::scrollDown() {
    if (_scrollOffset > 0) _scrollOffset--;
    if (_scrollOffset == 0) _autoScroll = true;
    _bodyDirty = true;
}

void AskScreen::openPicker() {
    _pickerSel = _modelIdx;
    _mode      = Mode::Picker;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void AskScreen::closePicker() {
    _mode = Mode::Chat;
    _statusDirty = _bodyDirty = _inputDirty = true;
}
void AskScreen::applyModel(int idx) {
    if (idx < 0 || idx >= (int)kAskModels.size()) return;
    Serial.printf("[ask] switching model -> %s\n", kAskModels[idx].slug);
    _modelIdx = idx;
    _ai->setModel(kAskModels[idx].slug);
    // Don't clear conversation -- the user may want to ask the same
    // question of a different model with the same context.
}

void AskScreen::sendCurrent() {
    String prompt = _input;
    _input = "";
    _conv.addUserMessage(prompt);
    _conv.addAssistantMessage("");

    _streaming    = true;
    _cancelStream = false;
    _scrollOffset = 0;
    _autoScroll   = true;
    _bodyDirty = _inputDirty = true;
    renderBody();
    renderInput();

    Serial.printf("[ask] send: %s\n", prompt.c_str());
    Serial.printf("[ask] heap pre-send free=%u min=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());

    std::vector<ESPAI::Message> messages;
    if (_systemPrompt.length() > 0) {
        messages.push_back(ESPAI::Message(ESPAI::Role::System, _systemPrompt));
    }
    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i + 1 < msgs.size(); i++) messages.push_back(msgs[i]);

    ESPAI::ChatOptions options;
    options.maxTokens = 800;

    uint32_t t0 = millis();
    int chunks = 0;
    bool ok = _ai->chatStream(messages, options,
        [&](const String& chunk, bool done) {
            chunks++;
            if (!_cancelStream) {
                M5Cardputer.update();
                auto& s = M5Cardputer.Keyboard.keysState();
                for (char c : s.word) {
                    if (c == '`' || c == '~') { _cancelStream = true; break; }
                }
                if (s.del) _cancelStream = true;
            }
            if (!_cancelStream) {
                auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
                if (!m.empty()) m.back().content += chunk;
                if (_autoScroll) renderBody();
            }
            if (done) {
                Serial.printf("[ask] done %ums chunks=%d%s\n",
                              (unsigned)(millis() - t0), chunks,
                              _cancelStream ? " (cancelled)" : "");
            }
        });

    if (_cancelStream) {
        auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
        if (!m.empty()) m.back().content += "\n[?]cancelled[/?]";
    } else if (!ok) {
        auto& m = const_cast<std::vector<ESPAI::Message>&>(_conv.getMessages());
        if (!m.empty()) m.back().content += " [!]error[/!]";
    }
    _streaming = false;
    _bodyDirty = _inputDirty = true;
}

void AskScreen::addLocalExchange(const String& userMsg, const String& assistantMsg) {
    _conv.addUserMessage(userMsg);
    _conv.addAssistantMessage(assistantMsg);
    _input = "";
    _scrollOffset = 0;
    _autoScroll = true;
    _bodyDirty = _inputDirty = true;
}

bool AskScreen::handleSlashCommand(const String& cmd) {
    if (cmd == "/clear") {
        Serial.println("[ask] /clear");
        _conv.clear();
        _conv.setSystemPrompt(_systemPrompt);
        _input = "";
        _scrollOffset = 0;
        _autoScroll = true;
        _bodyDirty = _inputDirty = true;
        return true;
    }
    if (cmd == "/help" || cmd == "/?") {
        static const char* kHelp =
            "<<commands>>\n"
            "[k]/help[/k]    show this list\n"
            "[k]/clear[/k]   wipe the in-session chat\n"
            "[k]/diag[/k]    diagnostics\n"
            "[k]/snap[/k]    screenshot to sd\n"
            "---\n"
            "[?]hit esc/del during a reply to cancel[/?]\n"
            "[?]backspace from empty input exits ask[/?]";
        addLocalExchange(cmd, kHelp);
        return true;
    }
    if (cmd == "/diag" || cmd == "/info") {
        String r;
        r  = "<<ask diagnostics>>\n";
        r += "[k]memos[/k]  [v]"; r += String(_memoCount); r += "[/v]\n";
        r += "[k]model[/k]  [v]"; r += kAskModels[_modelIdx].label; r += "[/v]\n";
        r += "[k]ssid[/k]   [v]"; r += WiFi.SSID(); r += "[/v]\n";
        r += "[k]ip[/k]     [v]"; r += WiFi.localIP().toString(); r += "[/v]\n";
        r += "[k]rssi[/k]   [v]"; r += String(WiFi.RSSI()); r += " dBm[/v]\n";
        r += "[k]heap[/k]   [v]"; r += String((unsigned)ESP.getFreeHeap()); r += "[/v] free\n";
        r += "[k]ctx[/k]    [v]"; r += String((unsigned)_systemPrompt.length()); r += "[/v] chars\n";
        r += "[k]msgs[/k]   [v]"; r += String((unsigned)_conv.size()); r += "[/v]";
        addLocalExchange(cmd, r);
        return true;
    }
    if (cmd == "/snap") {
        String name = snapshot::captureToBMP();
        if (name.length() > 0) {
            String r = "[ok]snapshot saved[/ok]\n[k]file[/k] [v]/Cardputer/snaps/";
            r += name; r += "[/v]";
            addLocalExchange(cmd, r);
        } else {
            addLocalExchange(cmd, "[!]snap failed[/!]");
        }
        return true;
    }
    // Unknown slash: echo with hint
    addLocalExchange(cmd, "[!]unknown command: " + cmd + "[/!]\ntry [k]/help[/k]");
    return true;
}

// ---------- rendering ----------

void AskScreen::renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);

    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);

    // Right: model label
    int rxRight = kScreenW - kPadX;
    int labelW = M5Cardputer.Display.textWidth(kAskModels[_modelIdx].label);
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    M5Cardputer.Display.setCursor(rxRight - labelW, 2);
    M5Cardputer.Display.print(kAskModels[_modelIdx].label);

    // Left side: LED + bars + "N memos"
    M5Cardputer.Display.fillRect(kPadX, 4, 3, 3, kStatusAccent);
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        int bars = 0;
        if (rssi > -85) bars = 1;
        if (rssi > -75) bars = 2;
        if (rssi > -65) bars = 3;
        if (rssi > -55) bars = 4;
        int baseX = kPadX + 8;
        int baseY = 9;
        for (int b = 0; b < 4; b++) {
            int h = 2 + b;
            int x = baseX + b * 3;
            uint16_t col = (b < bars) ? kStatusAccent : 0x2104;
            M5Cardputer.Display.fillRect(x, baseY - h, 2, h, col);
        }
    }
    String memoLbl = String(_memoCount) + " memo" + (_memoCount == 1 ? "" : "s");
    M5Cardputer.Display.setTextColor(kStatusDim, kBg);
    M5Cardputer.Display.setCursor(kPadX + 22, 2);
    M5Cardputer.Display.print(memoLbl);

    if (_mode == Mode::Picker) {
        // Tiny mode tag in the middle-left between bars and model
        M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
        M5Cardputer.Display.setCursor(kPadX + 22 + M5Cardputer.Display.textWidth(memoLbl.c_str()) + 8, 2);
        M5Cardputer.Display.print(". picker");
    }

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
}

void AskScreen::renderBody() {
    if (!_canvasOk) {
        M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);
        return;
    }
    _bodyCanvas.fillScreen(kBg);
    _bodyCanvas.drawLine(0, 0, kScreenW, 0, kDivider);

    switch (_mode) {
        case Mode::Chat:
            if (_conv.size() == 0 && !_streaming) renderEmptyBody();
            else                                   renderChatBody();
            break;
        case Mode::Picker:
            renderPickerBody();
            break;
    }
    _bodyCanvas.pushSprite(0, bodyTop());
}

void AskScreen::renderEmptyBody() {
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(1);
    _bodyCanvas.setTextColor(0x4208, kBg);
    const char* l1 = "context loaded";
    int w1 = _bodyCanvas.textWidth(l1);
    _bodyCanvas.setCursor((kScreenW - w1) / 2, 18);
    _bodyCanvas.print(l1);

    _bodyCanvas.setTextColor(kStatusAccent, kBg);
    char l2[32];
    snprintf(l2, sizeof(l2), "%d memo%s . %u tokens",
             _memoCount, _memoCount == 1 ? "" : "s",
             (unsigned)(_systemPrompt.length() / 4));
    int w2 = _bodyCanvas.textWidth(l2);
    _bodyCanvas.setCursor((kScreenW - w2) / 2, 40);
    _bodyCanvas.print(l2);

    _bodyCanvas.setFont(&fonts::Font0);
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    const char* tip = "ask a question and press enter";
    int wt = _bodyCanvas.textWidth(tip);
    _bodyCanvas.setCursor((kScreenW - wt) / 2, bodyHeight() - 24);
    _bodyCanvas.print(tip);
    const char* tip2 = "/help for commands . del to exit";
    int wt2 = _bodyCanvas.textWidth(tip2);
    _bodyCanvas.setCursor((kScreenW - wt2) / 2, bodyHeight() - 12);
    _bodyCanvas.print(tip2);
    _bodyCanvas.setFont(&fonts::Font2);
}

void AskScreen::renderChatBody() {
    const int maxPx = kScreenW - 2 * kPadX;
    std::vector<styled_text::Line> lines;
    lines.reserve(_conv.size() * 4);

    const auto& msgs = _conv.getMessages();
    for (size_t i = 0; i < msgs.size(); i++) {
        const auto& m = msgs[i];
        if (m.role == ESPAI::Role::System) continue;
        bool isUser = (m.role == ESPAI::Role::User);
        uint16_t color = isUser ? kUserColor : kAsstColor;
        styled_text::parse(_bodyCanvas, m.content, color, isUser, isUser,
                           maxPx, kScreenW, lines);
        if (i + 1 < msgs.size()) {
            styled_text::Line gap;
            lines.push_back(gap);
        }
    }

    const int lh  = lineHeight();
    const int vis = visibleLines();
    const int tot = (int)lines.size();
    int endIdx = tot - _scrollOffset;
    if (endIdx < 1) endIdx = std::min(1, tot);
    int startIdx = endIdx - vis;
    if (startIdx < 0) startIdx = 0;

    int y = 2;
    for (int i = startIdx; i < endIdx && i < tot; i++) {
        styled_text::render(_bodyCanvas, lines[i], y, lh, kScreenW,
                            kAsstColor, kStatusDim, kUserColor, kBg);
        y += lh;
    }
}

void AskScreen::renderPickerBody() {
    int lh = lineHeight() + 2;
    int y  = 6;
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(1);
    for (size_t i = 0; i < kAskModels.size(); i++) {
        bool sel = ((int)i == _pickerSel);
        bool cur = ((int)i == _modelIdx);
        uint16_t color = sel ? kSelColor : kIdleColor;
        if (sel) _bodyCanvas.fillRect(kPadX, y + 1, 3, lh - 4, kSelColor);
        _bodyCanvas.setTextColor(color, kBg);
        _bodyCanvas.setCursor(kPadX + 9, y);
        _bodyCanvas.print(kAskModels[i].label);
        if (cur) {
            _bodyCanvas.setTextColor(kStatusDim, kBg);
            _bodyCanvas.print("  (current)");
        }
        y += lh;
    }

    _bodyCanvas.setFont(&fonts::Font0);
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    _bodyCanvas.setCursor(kPadX, bodyHeight() - 14);
    _bodyCanvas.print("[,/.] move  [ret] pick  [del] cancel");
    _bodyCanvas.setFont(&fonts::Font2);
}

void AskScreen::renderInput() {
    const int y = kScreenH - kInputH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kInputH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);

    if (_mode == Mode::Picker) {
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(kStatusDim, kBg);
        M5Cardputer.Display.setCursor(kPadX, y + 6);
        M5Cardputer.Display.print("[,/.] move  [ret] pick  [del] cancel");
        M5Cardputer.Display.setFont(&fonts::Font2);
        return;
    }

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
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

void AskScreen::renderCursorOnly() {
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

} // namespace

namespace ask_screen {

void run(const String& apiKey, const std::vector<String>& selectedNotes) {
    if (selectedNotes.empty()) return;
    if (WiFi.status() != WL_CONNECTED) {
        boot_ui::clear();
        boot_ui::header("offline", 0x7800);
        boot_ui::centerText("wifi not connected", 56, 0xF884);
        boot_ui::footer("any key to return");
        boot_ui::waitForAnyKey();
        return;
    }

    int loaded = 0;
    String context = notestore::buildAskContext(selectedNotes, &loaded);
    if (loaded == 0) {
        boot_ui::clear();
        boot_ui::header("ask mode failed", 0x7800);
        boot_ui::centerText("no parseable notes", 56, 0xF884);
        boot_ui::footer("any key to return");
        boot_ui::waitForAnyKey();
        return;
    }

    uint32_t tokenEstimate = context.length() / 4;
    Serial.printf("[ask] ctx %u chars ~%u tokens from %d note(s)\n",
                  (unsigned)context.length(), (unsigned)tokenEstimate, loaded);

    if (!runPreflight(loaded, tokenEstimate)) {
        Serial.println("[ask] preflight cancelled");
        return;
    }

    AskScreen* screen = new AskScreen(apiKey, context, loaded);
    screen->runLoop();
    delete screen;
}

} // namespace ask_screen
