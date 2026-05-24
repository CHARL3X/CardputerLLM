#include "menu_screen.h"
#include "boot_ui.h"
#include "../storage/sd_config.h"
#include "../storage/settings.h"
#include "../storage/note_store.h"
#include "../setup/wifi_setup.h"
#include "../setup/key_setup.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include <functional>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kHintH   = 20;
constexpr int kPadX    = 4;

constexpr uint16_t kBg           = 0x0000;
constexpr uint16_t kDivider      = 0x2104;
constexpr uint16_t kStatusDim    = 0x6B4D;
constexpr uint16_t kStatusAccent = 0x57DC;
constexpr uint16_t kIdle         = 0xEF7D;
constexpr uint16_t kAccent       = 0x57DC;
constexpr uint16_t kDim          = 0x6B4D;

enum class Mode { List, Info };

struct MenuItem {
    const char* label;
    // Either action or infoBuilder will be set, not both.
    std::function<void()> action;
    std::function<std::vector<String>()> infoBuilder;
};

String kvFmt(const char* k, const String& v) {
    String s(k);
    while (s.length() < 9) s += ' ';
    s += v;
    return s;
}

std::vector<String> buildDiagnostics() {
    std::vector<String> lines;
    lines.push_back("--- build ---");
    lines.push_back(kvFmt("version", "v1.0"));
    lines.push_back("--- models ---");
    lines.push_back(kvFmt("tx", settings::txModel()));
    lines.push_back(kvFmt("title", settings::titleModel()));
    lines.push_back("--- notes ---");
    int count = (int)notestore::list().size();
    lines.push_back(kvFmt("count", String(count)));
    lines.push_back("--- heap ---");
    lines.push_back(kvFmt("free", String((unsigned)ESP.getFreeHeap())));
    lines.push_back(kvFmt("min", String((unsigned)ESP.getMinFreeHeap())));
    lines.push_back(kvFmt("largest", String((unsigned)ESP.getMaxAllocHeap())));
    lines.push_back("--- uptime ---");
    uint32_t s = millis() / 1000;
    char b[32];
    snprintf(b, sizeof(b), "%uh %um %us",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
    lines.push_back(kvFmt("up", b));
    return lines;
}

std::vector<String> buildWiFiInfo() {
    std::vector<String> lines;
    lines.push_back(kvFmt("ssid", WiFi.SSID()));
    lines.push_back(kvFmt("ip", WiFi.localIP().toString()));
    lines.push_back(kvFmt("rssi", String(WiFi.RSSI()) + " dBm"));
    lines.push_back(kvFmt("gw", WiFi.gatewayIP().toString()));
    lines.push_back(kvFmt("dns", WiFi.dnsIP().toString()));
    lines.push_back(kvFmt("mac", WiFi.macAddress()));
    time_t now = time(nullptr);
    if (now > 1577836800) {
        struct tm t; gmtime_r(&now, &t);
        char b[24];
        snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02dZ",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                 t.tm_hour, t.tm_min);
        lines.push_back(kvFmt("utc", b));
    } else {
        lines.push_back(kvFmt("utc", "unsynced"));
    }
    return lines;
}

std::vector<String> buildSystemPromptInfo() {
    std::vector<String> lines;
    String sp = sdcfg::loadSystemPrompt();
    if (sp.length() == 0) {
        lines.push_back("(built-in default)");
        lines.push_back("");
        lines.push_back("override on sd:");
        lines.push_back("/Cardputer/system.txt");
        return lines;
    }
    lines.push_back(kvFmt("length", String((unsigned)sp.length()) + " chars"));
    lines.push_back("--- text ---");
    // Word-wrap to ~38 cols
    while (sp.length() > 0) {
        int take = sp.length() > 32 ? 32 : sp.length();
        if (take < (int)sp.length()) {
            int sb = sp.lastIndexOf(' ', take);
            if (sb > 4) take = sb;
        }
        lines.push_back(sp.substring(0, take));
        sp = sp.substring(take);
        sp.trim();
    }
    return lines;
}

class MenuScreen {
public:
    void run();

private:
    void renderAll();
    void renderStatus();
    void renderBody();
    void renderHint();
    void renderListBody();
    void renderInfoBody();

    void pollKeyboard();
    void onCharRising(char c, bool fn);
    void onDel();
    void onEnter();

    void enterInfo(const String& title, std::vector<String> lines);
    void exitInfo();
    void doAction(int idx);

    Mode _mode = Mode::List;
    int  _sel  = 0;
    int  _infoScroll = 0;
    bool _exit       = false;

    bool _bodyDirty   = true;
    bool _statusDirty = true;
    bool _hintDirty   = true;

    String              _infoTitle;
    std::vector<String> _infoLines;

    std::vector<MenuItem> _items;

    std::vector<char> _prevWord;
    bool _prevDel   = false;
    bool _prevEnter = false;

    M5Canvas _bodyCanvas;
    bool     _canvasOk = false;
};

void MenuScreen::run() {
    _items = {
        { "diagnostics",   nullptr, []() { return buildDiagnostics(); } },
        { "wifi info",     nullptr, []() { return buildWiFiInfo(); } },
        { "system prompt", nullptr, []() { return buildSystemPromptInfo(); } },
        { "add wifi",      []() { wifi_setup::run(/*allowCancel=*/true); }, nullptr },
        { "set api key",   []() { key_setup::run(/*allowCancel=*/true); },  nullptr },
        { "exit",          [this]() { _exit = true; },                      nullptr },
    };

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);

    _bodyCanvas = M5Canvas(&M5Cardputer.Display);
    _bodyCanvas.setColorDepth(16);
    _canvasOk = _bodyCanvas.createSprite(kScreenW, kScreenH - kStatusH - kHintH);
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
    } else {
        Serial.println("[menu] WARN canvas alloc failed");
    }

    _statusDirty = _bodyDirty = _hintDirty = true;

    while (!_exit) {
        pollKeyboard();
        if (_statusDirty) { renderStatus(); _statusDirty = false; }
        if (_bodyDirty)   { renderBody();   _bodyDirty   = false; }
        if (_hintDirty)   { renderHint();   _hintDirty   = false; }
        delay(15);
    }

    if (_canvasOk) _bodyCanvas.deleteSprite();
}

void MenuScreen::pollKeyboard() {
    M5Cardputer.update();
    auto& s = M5Cardputer.Keyboard.keysState();

    if (s.del && !_prevDel) onDel();
    if (s.enter && !_prevEnter) onEnter();

    for (char c : s.word) {
        bool wasPrev = false;
        for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
        if (!wasPrev) onCharRising(c, s.fn);
    }
    _prevWord  = s.word;
    _prevDel   = s.del;
    _prevEnter = s.enter;
}

void MenuScreen::onCharRising(char c, bool fn) {
    if (c == '`' || c == '~') {
        if (_mode == Mode::Info) { exitInfo(); return; }
        _exit = true;
        return;
    }
    if (_mode == Mode::List) {
        if (c == ',' || c == ';') {
            if (_sel > 0) { _sel--; _bodyDirty = true; }
            return;
        }
        if (c == '.' || c == '/') {
            if (_sel + 1 < (int)_items.size()) { _sel++; _bodyDirty = true; }
            return;
        }
        return;
    }
    if (_mode == Mode::Info) {
        if (c == ',' || c == ';') {
            if (_infoScroll > 0) { _infoScroll--; _bodyDirty = true; }
        } else if (c == '.' || c == '/') {
            _infoScroll++; _bodyDirty = true;
        }
    }
}

void MenuScreen::onDel() {
    if (_mode == Mode::Info) { exitInfo(); return; }
    _exit = true;
}

void MenuScreen::onEnter() {
    if (_mode == Mode::Info) { exitInfo(); return; }
    if (_sel < 0 || _sel >= (int)_items.size()) return;
    doAction(_sel);
}

void MenuScreen::doAction(int idx) {
    const auto& item = _items[idx];
    Serial.printf("[menu] -> %s\n", item.label);

    if (item.infoBuilder) {
        enterInfo(item.label, item.infoBuilder());
        return;
    }
    if (item.action) {
        // Release the body canvas before dispatching anything that owns
        // the screen (wifi/key setup, etc.), so they have heap.
        if (_canvasOk) { _bodyCanvas.deleteSprite(); _canvasOk = false; }
        item.action();
        // Reallocate canvas on return; redraw everything.
        _bodyCanvas = M5Canvas(&M5Cardputer.Display);
        _bodyCanvas.setColorDepth(16);
        _canvasOk = _bodyCanvas.createSprite(kScreenW, kScreenH - kStatusH - kHintH);
        if (_canvasOk) {
            _bodyCanvas.setFont(&fonts::Font2);
            _bodyCanvas.setTextSize(1);
        }
        M5Cardputer.Display.fillScreen(kBg);
        _statusDirty = _bodyDirty = _hintDirty = true;
    }
}

void MenuScreen::enterInfo(const String& title, std::vector<String> lines) {
    _mode       = Mode::Info;
    _infoTitle  = title;
    _infoLines  = std::move(lines);
    _infoScroll = 0;
    _statusDirty = _bodyDirty = _hintDirty = true;
}

void MenuScreen::exitInfo() {
    _mode = Mode::List;
    _statusDirty = _bodyDirty = _hintDirty = true;
}

void MenuScreen::renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);

    // Left: small "menu" tag in dim + LED + RSSI
    M5Cardputer.Display.fillRect(kPadX, 4, 3, 3, kStatusAccent);
    M5Cardputer.Display.setTextColor(kStatusDim, kBg);
    M5Cardputer.Display.setCursor(kPadX + 8, 2);
    M5Cardputer.Display.print("/menu");

    // Right: section label
    String right = (_mode == Mode::Info) ? _infoTitle : String("settings");
    int rw = M5Cardputer.Display.textWidth(right.c_str());
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    M5Cardputer.Display.setCursor(kScreenW - kPadX - rw, 2);
    M5Cardputer.Display.print(right);

    M5Cardputer.Display.setFont(&fonts::Font2);
}

void MenuScreen::renderBody() {
    int bodyTop = kStatusH;
    int bodyH   = kScreenH - kStatusH - kHintH;

    if (!_canvasOk) {
        M5Cardputer.Display.fillRect(0, bodyTop, kScreenW, bodyH, kBg);
        return;
    }
    _bodyCanvas.fillScreen(kBg);
    _bodyCanvas.drawLine(0, 0, kScreenW, 0, kDivider);

    if (_mode == Mode::List) renderListBody();
    else                     renderInfoBody();

    _bodyCanvas.pushSprite(0, bodyTop);
}

void MenuScreen::renderListBody() {
    int lh = 18;
    int vis = (kScreenH - kStatusH - kHintH - 6) / lh;
    if (vis < 1) vis = 1;
    int start = 0;
    if (_sel >= vis) start = _sel - vis + 1;
    int end = std::min(start + vis, (int)_items.size());

    int y = 4;
    for (int i = start; i < end; i++) {
        bool sel = (i == _sel);
        uint16_t color = sel ? kAccent : kIdle;
        if (sel) _bodyCanvas.fillRect(kPadX, y + 1, 3, lh - 4, kAccent);
        _bodyCanvas.setTextColor(color, kBg);
        _bodyCanvas.setCursor(kPadX + 9, y + 1);
        _bodyCanvas.print(_items[i].label);
        y += lh;
    }

    if ((int)_items.size() > vis) {
        _bodyCanvas.setFont(&fonts::Font0);
        _bodyCanvas.setTextColor(kStatusDim, kBg);
        if (start > 0) {
            _bodyCanvas.setCursor(kScreenW - 10, 4);
            _bodyCanvas.print("^");
        }
        if (end < (int)_items.size()) {
            _bodyCanvas.setCursor(kScreenW - 10, kScreenH - kStatusH - kHintH - 14);
            _bodyCanvas.print("v");
        }
        _bodyCanvas.setFont(&fonts::Font2);
    }
}

void MenuScreen::renderInfoBody() {
    int lh = 13;
    int vis = (kScreenH - kStatusH - kHintH - 6) / lh;
    int start = _infoScroll;
    if (start < 0) start = 0;
    if (start > (int)_infoLines.size()) start = _infoLines.size();
    int end = std::min(start + vis, (int)_infoLines.size());

    int y = 4;
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(1);
    for (int i = start; i < end; i++) {
        const String& s = _infoLines[i];
        bool isHeader = s.startsWith("---");
        _bodyCanvas.setTextColor(isHeader ? kStatusDim : kIdle, kBg);
        _bodyCanvas.setCursor(kPadX, y);
        _bodyCanvas.print(s);
        y += lh;
    }
}

void MenuScreen::renderHint() {
    int y = kScreenH - kHintH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kHintH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kStatusDim, kBg);

    const char* l1 = "";
    const char* l2 = "";
    if (_mode == Mode::List) {
        l1 = ",/. navigate   [ret] open";
        l2 = "[del] / [esc]  exit menu";
    } else {
        l1 = ",/. scroll";
        l2 = "[del] / [esc]  back to menu";
    }
    M5Cardputer.Display.setCursor(kPadX, y + 4);
    M5Cardputer.Display.print(l1);
    M5Cardputer.Display.setCursor(kPadX, y + 13);
    M5Cardputer.Display.print(l2);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

} // namespace

namespace menu_screen {

void run() {
    Serial.println("[menu] open");
    MenuScreen s;
    s.run();
    Serial.println("[menu] close");
}

} // namespace menu_screen
