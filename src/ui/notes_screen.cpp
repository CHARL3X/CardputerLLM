#include "notes_screen.h"
#include "record_screen.h"
#include "note_detail.h"
#include "ask_screen.h"
#include "menu_screen.h"
#include "boot_ui.h"
#include "../storage/sd_config.h"
#include "../storage/note_store.h"
#include "../storage/settings.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <time.h>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kHintH   = 20;
constexpr int kPadX    = 4;

constexpr uint16_t kBg           = 0x0000;
constexpr uint16_t kDivider      = 0x2104;
constexpr uint16_t kStatusDim    = 0x6B4D;
constexpr uint16_t kStatusAccent = 0x07FF;
constexpr uint16_t kIdle         = 0xEF7D;
constexpr uint16_t kAccent       = 0x07FF;
constexpr uint16_t kFaint        = 0x2104;
constexpr uint16_t kMuted        = 0x4208;
constexpr uint16_t kRed          = 0xF884;

constexpr int kRowH = 18;   // Font2 (16px) + 2px padding

} // namespace

NotesScreen::NotesScreen(const String& apiKey,
                         const String& txModel,
                         const String& titleModel)
    : _apiKey(apiKey), _txModel(txModel), _titleModel(titleModel),
      _bodyCanvas(&M5Cardputer.Display) {}

void NotesScreen::begin() {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);

    _bodyCanvas.setColorDepth(16);
    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
    } else {
        Serial.println("[notes] WARN body canvas alloc failed");
    }

    rescan();
    _bodyDirty = _statusDirty = _hintDirty = true;
}

int NotesScreen::bodyTop()    const { return kStatusH; }
int NotesScreen::bodyHeight() const { return kScreenH - kStatusH - kHintH; }
int NotesScreen::visibleRowCount() const {
    int n = (bodyHeight() - 4) / kRowH;
    return n < 1 ? 1 : n;
}

int NotesScreen::selectedCount() const {
    int n = 0;
    for (auto& r : _rows) if (r.selected) n++;
    return n;
}

void NotesScreen::clampScroll() {
    int vis = visibleRowCount();
    if (_highlight < 0)                   _highlight = 0;
    if (_highlight >= (int)_rows.size())  _highlight = std::max(0, (int)_rows.size() - 1);
    if (_highlight < _scrollTop)          _scrollTop = _highlight;
    if (_highlight >= _scrollTop + vis)   _scrollTop = _highlight - vis + 1;
    if (_scrollTop < 0)                   _scrollTop = 0;
}

void NotesScreen::rescan() {
    Serial.println("[notes] rescan");
    _rows.clear();
    auto names = notestore::list();
    for (auto& name : names) {
        notestore::NoteMeta meta;
        String body;
        if (!notestore::load(name, meta, body)) {
            Serial.printf("[notes] skip unparseable %s\n", name.c_str());
            continue;
        }
        NoteRow row;
        row.filename    = name;
        row.title       = meta.title.length() > 0 ? meta.title : String("Untitled note");
        row.createdUtc  = meta.createdUtc;
        row.durationSec = meta.durationSec;
        row.selected    = false;
        _rows.push_back(row);
    }
    Serial.printf("[notes] loaded %u row(s)\n", (unsigned)_rows.size());

    if (_rows.empty())              _highlight = 0;
    else if (_highlight >= (int)_rows.size()) _highlight = _rows.size() - 1;
    clampScroll();

    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::tick() {
    pollKeyboard();
    if (_statusDirty) { renderStatus(); _statusDirty = false; }
    if (_bodyDirty)   { renderBody();   _bodyDirty   = false; }
    if (_hintDirty)   { renderHint();   _hintDirty   = false; }
    delay(15);
}

// ---------------------------------------------------------------------
// keyboard
// ---------------------------------------------------------------------

bool NotesScreen::isUpKey(char c, bool fn) const {
    if (_mode == Mode::List || _mode == Mode::MultiSelect) {
        // Bare ,/; works (no text entry on this screen); Fn+,/; also works.
        return c == ',' || c == ';';
    }
    return false;
}

bool NotesScreen::isDownKey(char c, bool fn) const {
    if (_mode == Mode::List || _mode == Mode::MultiSelect) {
        return c == '.' || c == '/';
    }
    return false;
}

void NotesScreen::pollKeyboard() {
    M5Cardputer.update();
    auto& s = M5Cardputer.Keyboard.keysState();

    if (s.del && !_prevDel) onDel();
    if (s.enter && !_prevEnter) onEnter();

    for (char c : s.word) {
        bool wasPrev = false;
        for (char p : _prevWord) if (p == c) { wasPrev = true; break; }
        if (wasPrev) continue;
        onCharRising(c, s.fn);
    }

    _prevWord  = s.word;
    _prevDel   = s.del;
    _prevEnter = s.enter;
}

void NotesScreen::onCharRising(char c, bool fn) {
    if (_mode == Mode::Confirm) {
        if (c == 'y' || c == 'Y') { resolveConfirm(true);  return; }
        if (c == 'n' || c == 'N') { resolveConfirm(false); return; }
        return;
    }

    // backtick/tilde -- open the settings menu (Phase 7). In multi-select
    // mode it backs out of multi-select first.
    if (c == '`' || c == '~') {
        if (_mode == Mode::MultiSelect) {
            exitMultiSelect();
            return;
        }
        // Release our canvas so menu_screen has the heap.
        if (_canvasOk) { _bodyCanvas.deleteSprite(); _canvasOk = false; }
        menu_screen::run();
        _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
        if (_canvasOk) {
            _bodyCanvas.setFont(&fonts::Font2);
            _bodyCanvas.setTextSize(1);
        }
        M5Cardputer.Display.fillScreen(kBg);
        rescan();
        _bodyDirty = _statusDirty = _hintDirty = true;
        return;
    }

    if (isUpKey(c, fn)) {
        if (_highlight > 0) {
            _highlight--;
            clampScroll();
            _bodyDirty = _statusDirty = true;
        }
        return;
    }
    if (isDownKey(c, fn)) {
        if (_highlight + 1 < (int)_rows.size()) {
            _highlight++;
            clampScroll();
            _bodyDirty = _statusDirty = true;
        }
        return;
    }

    if (c == ' ') { onSpc(); return; }

    if (fn) {
        if (c == 'd' || c == 'D') {
            if (_mode == Mode::List && !_rows.empty()) deleteHighlightedConfirm();
            return;
        }
        if (c == 's' || c == 'S') {
            if (_mode == Mode::List && !_rows.empty()) enterMultiSelect();
            else if (_mode == Mode::MultiSelect)       exitMultiSelect();
            return;
        }
        if (c == 'a' || c == 'A') {
            if (!_rows.empty()) enterAskMode();
            return;
        }
    }
}

void NotesScreen::onEnter() {
    if (_mode == Mode::Confirm) { resolveConfirm(true); return; }
    if (_mode == Mode::List || _mode == Mode::MultiSelect) {
        if (!_rows.empty()) openHighlighted();
    }
}

void NotesScreen::onDel() {
    if (_mode == Mode::Confirm)     { resolveConfirm(false); return; }
    if (_mode == Mode::MultiSelect) { exitMultiSelect();      return; }
    // In List mode at home there's nowhere to back to; ignore.
}

void NotesScreen::onSpc() {
    if (_mode == Mode::MultiSelect) {
        toggleHighlightedSelection();
        return;
    }
    if (_mode == Mode::List) {
        recordNew();
    }
}

// ---------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------

void NotesScreen::recordNew() {
    Serial.println("[notes] -> record");
    if (_canvasOk) _bodyCanvas.deleteSprite();
    // Read SD/NVS fresh each call so the menu's "set api key" / any
    // future model swap takes effect without ctor re-init.
    String apiKey = sdcfg::loadOpenRouterKey();
    record_screen::run(apiKey, settings::txModel(), settings::titleModel());
    // Reallocate canvas (record_screen may have shifted heap a lot)
    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
    }
    // Newly saved note (if any) is at index 0 since list() sorts desc.
    rescan();
    _highlight = 0;
    clampScroll();
    M5Cardputer.Display.fillScreen(kBg);
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::openHighlighted() {
    if (_highlight < 0 || _highlight >= (int)_rows.size()) return;
    const NoteRow& row = _rows[_highlight];
    notestore::NoteMeta meta;
    String body;
    if (!notestore::load(row.filename, meta, body)) {
        Serial.printf("[notes] open: load failed for %s\n", row.filename.c_str());
        return;
    }
    if (_canvasOk) _bodyCanvas.deleteSprite();

    String subtitle = String("/Cardputer/notes/") + row.filename;
    bool deleted = note_detail::show(meta.title, body, row.filename, subtitle);

    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
    }
    M5Cardputer.Display.fillScreen(kBg);
    if (deleted) rescan();
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::deleteHighlightedConfirm() {
    if (_highlight < 0 || _highlight >= (int)_rows.size()) return;
    String t = _rows[_highlight].title;
    String detail = String("\"") + t + "\"";
    confirm("delete this note?", detail, [this]() { doDeleteHighlighted(); });
}

void NotesScreen::doDeleteHighlighted() {
    if (_highlight < 0 || _highlight >= (int)_rows.size()) return;
    String name = _rows[_highlight].filename;
    Serial.printf("[notes] deleting %s\n", name.c_str());
    bool ok = notestore::remove(name);
    Serial.printf("[notes] delete -> %s\n", ok ? "ok" : "FAIL");
    _mode = Mode::List;
    rescan();
    M5Cardputer.Display.fillScreen(kBg);
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::enterMultiSelect() {
    _mode = Mode::MultiSelect;
    for (auto& r : _rows) r.selected = false;
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::exitMultiSelect() {
    _mode = Mode::List;
    for (auto& r : _rows) r.selected = false;
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::toggleHighlightedSelection() {
    if (_highlight < 0 || _highlight >= (int)_rows.size()) return;
    _rows[_highlight].selected = !_rows[_highlight].selected;
    _bodyDirty = _statusDirty = true;
}

void NotesScreen::enterAskMode() {
    // Resolve which notes the user wants in context. In MultiSelect
    // mode, use the checked rows. In List mode (Fn+A pressed without
    // selecting first), fall back to just the highlighted row -- it's
    // a reasonable "ask about this one note" shortcut.
    std::vector<String> selected;
    if (_mode == Mode::MultiSelect) {
        for (auto& r : _rows) if (r.selected) selected.push_back(r.filename);
        if (selected.empty() && _highlight >= 0 && _highlight < (int)_rows.size()) {
            selected.push_back(_rows[_highlight].filename);
        }
    } else if (_highlight >= 0 && _highlight < (int)_rows.size()) {
        selected.push_back(_rows[_highlight].filename);
    }

    if (selected.empty()) {
        Serial.println("[notes] ask: no notes to load");
        return;
    }
    Serial.printf("[notes] ask: %u note(s) selected\n", (unsigned)selected.size());

    // Release our canvas so ask_screen has the heap.
    if (_canvasOk) { _bodyCanvas.deleteSprite(); _canvasOk = false; }

    // Read fresh in case menu changed the key while we were running.
    String apiKey = sdcfg::loadOpenRouterKey();
    ask_screen::run(apiKey, selected);

    // Reallocate canvas + clean up multi-select state on return.
    _canvasOk = _bodyCanvas.createSprite(kScreenW, bodyHeight());
    if (_canvasOk) {
        _bodyCanvas.setFont(&fonts::Font2);
        _bodyCanvas.setTextSize(1);
    }
    if (_mode == Mode::MultiSelect) exitMultiSelect();
    M5Cardputer.Display.fillScreen(kBg);
    rescan();
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::confirm(const String& q,
                          const String& detail,
                          std::function<void()> onYes) {
    _confirmQ      = q;
    _confirmDetail = detail;
    _confirmOnYes  = onYes;
    _confirmReturn = _mode;
    _mode          = Mode::Confirm;
    _bodyDirty = _statusDirty = _hintDirty = true;
}

void NotesScreen::resolveConfirm(bool yes) {
    auto cb = _confirmOnYes;
    Mode r  = _confirmReturn;
    _confirmOnYes = nullptr;
    if (yes && cb) {
        cb();
        // doDeleteHighlighted already sets the mode and dirties
    } else {
        _mode = r;
        _bodyDirty = _statusDirty = _hintDirty = true;
    }
}

// ---------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------

String NotesScreen::formatRightLabel(const NoteRow& r) const {
    String out;
    // Date
    if (r.createdUtc.length() >= 10 && !r.createdUtc.startsWith("boot-")) {
        int month = r.createdUtc.substring(5, 7).toInt();
        int day   = r.createdUtc.substring(8, 10).toInt();
        char b[16];
        snprintf(b, sizeof(b), "%d/%d", month, day);
        out = b;
    } else {
        out = "?";
    }
    out += " ";
    // Duration: "Ns" if < 60, else "M:SS"
    if (r.durationSec < 60) {
        out += String(r.durationSec) + "s";
    } else {
        uint32_t mm = r.durationSec / 60;
        uint32_t ss = r.durationSec % 60;
        char b[16];
        snprintf(b, sizeof(b), "%u:%02u", (unsigned)mm, (unsigned)ss);
        out += b;
    }
    return out;
}

void NotesScreen::renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);

    String right;
    if (_mode == Mode::MultiSelect) {
        int sel = selectedCount();
        right = String(sel) + " of " + String((unsigned)_rows.size()) + " selected";
    } else {
        right = String((unsigned)_rows.size()) + " note";
        if (_rows.size() != 1) right += "s";
    }
    int rx = kScreenW - kPadX - M5Cardputer.Display.textWidth(right.c_str());
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    M5Cardputer.Display.setCursor(rx, 2);
    M5Cardputer.Display.print(right);

    // Persistent VOX brand badge -- always visible so the user knows
    // they're in Verbatim mode (vs the LLM chat).
    M5Cardputer.Display.setTextColor(kStatusAccent, kBg);
    M5Cardputer.Display.setCursor(kPadX, 2);
    M5Cardputer.Display.print("VOX");

    // WiFi signal bars to the right of the brand badge.
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        int bars = 0;
        if (rssi > -85) bars = 1;
        if (rssi > -75) bars = 2;
        if (rssi > -65) bars = 3;
        if (rssi > -55) bars = 4;
        int baseX = kPadX + 24;
        int baseY = 9;
        for (int b = 0; b < 4; b++) {
            int h = 2 + b;
            int x = baseX + b * 3;
            uint16_t col = (b < bars) ? kStatusAccent : 0x2104;
            M5Cardputer.Display.fillRect(x, baseY - h, 2, h, col);
        }
    }
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void NotesScreen::renderBody() {
    if (!_canvasOk) {
        M5Cardputer.Display.fillRect(0, bodyTop(), kScreenW, bodyHeight(), kBg);
        return;
    }
    _bodyCanvas.fillScreen(kBg);
    _bodyCanvas.drawLine(0, 0, kScreenW, 0, kDivider);

    switch (_mode) {
        case Mode::Confirm:     renderConfirmBody();     break;
        case Mode::MultiSelect: renderMultiSelectBody(); break;
        case Mode::List:
        default:
            if (_rows.empty()) renderEmptyBody();
            else               renderListBody();
            break;
    }

    _bodyCanvas.pushSprite(0, bodyTop());
}

void NotesScreen::renderEmptyBody() {
    int bodyH = bodyHeight();

    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(2);
    _bodyCanvas.setTextColor(kFaint, kBg);
    const char* wm = "VERBATIM";
    int ww = _bodyCanvas.textWidth(wm);
    _bodyCanvas.setCursor((kScreenW - ww) / 2, 14);
    _bodyCanvas.print(wm);

    _bodyCanvas.setTextSize(1);
    _bodyCanvas.setTextColor(kMuted, kBg);
    const char* sub = "VOX . LOG";
    int sw = _bodyCanvas.textWidth(sub);
    _bodyCanvas.setCursor((kScreenW - sw) / 2, 44);
    _bodyCanvas.print(sub);

    _bodyCanvas.setFont(&fonts::Font0);
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    const char* tip = "no notes yet . press SPC to record";
    int tw = _bodyCanvas.textWidth(tip);
    _bodyCanvas.setCursor((kScreenW - tw) / 2, bodyH - 14);
    _bodyCanvas.print(tip);
    _bodyCanvas.setFont(&fonts::Font2);
}

void NotesScreen::renderListBody() {
    int vis = visibleRowCount();
    int start = _scrollTop;
    int end   = std::min(start + vis, (int)_rows.size());

    int y = 4;
    for (int i = start; i < end; i++) {
        renderRow(_rows[i], y, kRowH, i == _highlight, /*showCheckbox=*/false);
        y += kRowH;
    }

    // Scroll indicators
    if (start > 0) {
        _bodyCanvas.setFont(&fonts::Font0);
        _bodyCanvas.setTextColor(kStatusDim, kBg);
        _bodyCanvas.setCursor(kScreenW - 8, 4);
        _bodyCanvas.print("^");
        _bodyCanvas.setFont(&fonts::Font2);
    }
    if (end < (int)_rows.size()) {
        _bodyCanvas.setFont(&fonts::Font0);
        _bodyCanvas.setTextColor(kStatusDim, kBg);
        _bodyCanvas.setCursor(kScreenW - 8, bodyHeight() - 12);
        _bodyCanvas.print("v");
        _bodyCanvas.setFont(&fonts::Font2);
    }
}

void NotesScreen::renderMultiSelectBody() {
    int vis = visibleRowCount();
    int start = _scrollTop;
    int end   = std::min(start + vis, (int)_rows.size());

    int y = 4;
    for (int i = start; i < end; i++) {
        renderRow(_rows[i], y, kRowH, i == _highlight, /*showCheckbox=*/true);
        y += kRowH;
    }
}

void NotesScreen::renderRow(const NoteRow& r, int y, int lineH,
                            bool highlighted, bool showCheckbox) {
    int leftX = 4;
    if (highlighted) {
        _bodyCanvas.fillRect(leftX, y + 1, 3, lineH - 2, kAccent);
    }
    leftX = 12;

    if (showCheckbox) {
        int boxY = y + (lineH - 8) / 2;
        if (r.selected) {
            _bodyCanvas.fillRect(leftX, boxY, 8, 8, kAccent);
        } else {
            _bodyCanvas.drawRect(leftX, boxY, 8, 8, kStatusDim);
        }
        leftX += 12;
    }

    // Right-side date+duration in Font0
    String right = formatRightLabel(r);
    _bodyCanvas.setFont(&fonts::Font0);
    int rw  = _bodyCanvas.textWidth(right.c_str());
    int rightX = kScreenW - 4 - rw;
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    _bodyCanvas.setCursor(rightX, y + 5);
    _bodyCanvas.print(right);

    // Title in Font2
    _bodyCanvas.setFont(&fonts::Font2);
    String title = r.title;
    int maxTitlePx = rightX - leftX - 6;
    while (_bodyCanvas.textWidth(title.c_str()) > maxTitlePx
           && title.length() > 1) {
        title.remove(title.length() - 1);
    }
    if (title.length() < r.title.length()) title += ".";
    uint16_t color = highlighted ? kAccent : kIdle;
    _bodyCanvas.setTextColor(color, kBg);
    _bodyCanvas.setCursor(leftX, y + 1);
    _bodyCanvas.print(title);
}

void NotesScreen::renderConfirmBody() {
    int y  = 12;
    _bodyCanvas.setFont(&fonts::Font2);
    _bodyCanvas.setTextSize(1);

    _bodyCanvas.setTextColor(kRed, kBg);
    int qw = _bodyCanvas.textWidth(_confirmQ.c_str());
    _bodyCanvas.setCursor((kScreenW - qw) / 2, y);
    _bodyCanvas.print(_confirmQ);
    y += 22;

    if (_confirmDetail.length() > 0) {
        String shown = _confirmDetail;
        int maxPx = kScreenW - 16;
        while (_bodyCanvas.textWidth(shown.c_str()) > maxPx
               && shown.length() > 1) {
            shown.remove(shown.length() - 1);
        }
        if (shown.length() < _confirmDetail.length()) shown += ".";
        _bodyCanvas.setTextColor(kIdle, kBg);
        int dw = _bodyCanvas.textWidth(shown.c_str());
        _bodyCanvas.setCursor((kScreenW - dw) / 2, y);
        _bodyCanvas.print(shown);
        y += 20;
    }

    _bodyCanvas.setFont(&fonts::Font0);
    _bodyCanvas.setTextColor(kStatusDim, kBg);
    const char* warn = "this cannot be undone.";
    int ww = _bodyCanvas.textWidth(warn);
    _bodyCanvas.setCursor((kScreenW - ww) / 2, y);
    _bodyCanvas.print(warn);
    _bodyCanvas.setFont(&fonts::Font2);
}

void NotesScreen::renderHint() {
    int y = kScreenH - kHintH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kHintH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kStatusDim, kBg);

    const char* l1 = "";
    const char* l2 = "";

    switch (_mode) {
        case Mode::List:
            if (_rows.empty()) {
                l1 = "[spc] record    [esc] menu";
                l2 = "no notes yet . press SPC to begin";
            } else {
                l1 = "[spc] new  [ret] open  [fn+d] del";
                l2 = "[fn+s] sel  [fn+a] ask  [esc] menu";
            }
            break;
        case Mode::MultiSelect: {
            int sel = selectedCount();
            static char l1buf[48];
            static char l2buf[48];
            snprintf(l1buf, sizeof(l1buf),
                     "[spc] toggle  [fn+a] ask %d", sel);
            snprintf(l2buf, sizeof(l2buf),
                     "[fn+s] exit   [del] back   [ret] open");
            l1 = l1buf; l2 = l2buf;
            break;
        }
        case Mode::Confirm:
            l1 = "[y][enter]  yes";
            l2 = "[n][del]    no";
            break;
    }

    M5Cardputer.Display.setCursor(kPadX, y + 4);
    M5Cardputer.Display.print(l1);
    if (l2[0] != 0) {
        M5Cardputer.Display.setCursor(kPadX, y + 13);
        M5Cardputer.Display.print(l2);
    }
    M5Cardputer.Display.setFont(&fonts::Font2);
}
