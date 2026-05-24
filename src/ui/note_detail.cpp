#include "note_detail.h"
#include "styled_text.h"
#include "boot_ui.h"
#include "../storage/note_store.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <vector>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 14;
constexpr int kHintH   = 20;
constexpr int kPadX    = 4;

constexpr uint16_t kBg     = 0x0000;
constexpr uint16_t kDiv    = 0x2104;
constexpr uint16_t kIdle   = 0xEF7D;
constexpr uint16_t kDim    = 0x6B4D;
constexpr uint16_t kAccent = 0x57DC;
constexpr uint16_t kRed    = 0xF884;
constexpr uint16_t kGreen  = 0x4FCA;

void renderStatusRow(const String& title, int scrollLine, int totalLines) {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int midY    = kStatusH / 2;
    int leftEnd = kPadX + 8;
    M5Cardputer.Display.drawLine(kPadX, midY, leftEnd, midY, kAccent);
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    M5Cardputer.Display.setCursor(kPadX + 12, 1);
    String shown = title;
    int maxTitlePx = kScreenW - 80;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxTitlePx
           && shown.length() > 1) {
        shown.remove(shown.length() - 1);
    }
    if (shown.length() < title.length()) shown += ".";
    M5Cardputer.Display.print(shown);

    if (totalLines > 0) {
        char ind[16];
        snprintf(ind, sizeof(ind), "%d/%d", scrollLine + 1, totalLines);
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(kDim, kBg);
        int iw = M5Cardputer.Display.textWidth(ind);
        M5Cardputer.Display.setCursor(kScreenW - kPadX - iw, 3);
        M5Cardputer.Display.print(ind);
        M5Cardputer.Display.setFont(&fonts::Font2);
    }
}

void renderHintBar(bool atTop, bool atBottom,
                   bool deletable,
                   const String& subtitle) {
    int y = kScreenH - kHintH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kHintH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDiv);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kDim, kBg);

    String l1;
    if (deletable) {
        if (!atTop && !atBottom)     l1 = "fn+,/. scroll  fn+d del  del back";
        else if (atTop && !atBottom) l1 = "fn+. down  fn+d del  del back";
        else if (!atTop && atBottom) l1 = "fn+, up  fn+d del  del back";
        else                          l1 = "fn+d delete  .  del back";
    } else {
        if (!atTop && !atBottom)     l1 = "fn+,/. scroll  .  del back";
        else if (atTop && !atBottom) l1 = "fn+. scroll down  .  del back";
        else if (!atTop && atBottom) l1 = "fn+, scroll up  .  del back";
        else                          l1 = "del / esc to return";
    }
    M5Cardputer.Display.setCursor(kPadX, y + 4);
    M5Cardputer.Display.print(l1);

    if (subtitle.length() > 0) {
        M5Cardputer.Display.setCursor(kPadX, y + 13);
        M5Cardputer.Display.print(subtitle);
    }
    M5Cardputer.Display.setFont(&fonts::Font2);
}

// Inline delete confirm. Direct-draw, static. Returns true if user said
// yes. Consumes its own key release before returning so the next caller
// doesn't see an immediate phantom press.
bool askDeleteConfirm(const String& title) {
    M5Cardputer.Display.fillScreen(kBg);
    boot_ui::sectionHeader("delete this note?", kRed);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    M5Cardputer.Display.setTextColor(kIdle, kBg);
    String shown = title;
    int maxPx = kScreenW - 16;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx
           && shown.length() > 1) {
        shown.remove(shown.length() - 1);
    }
    if (shown.length() < title.length()) shown += ".";
    int w = M5Cardputer.Display.textWidth(shown.c_str());
    M5Cardputer.Display.setCursor((kScreenW - w) / 2, 32);
    M5Cardputer.Display.print(shown);

    M5Cardputer.Display.setTextColor(kDim, kBg);
    const char* warn = "this cannot be undone.";
    int ww = M5Cardputer.Display.textWidth(warn);
    M5Cardputer.Display.setCursor((kScreenW - ww) / 2, 58);
    M5Cardputer.Display.print(warn);

    int hy = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDiv);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 4);
    M5Cardputer.Display.print("[y][enter]  yes");
    M5Cardputer.Display.setCursor(kPadX, hy + 13);
    M5Cardputer.Display.print("[n][del]    no");
    M5Cardputer.Display.setFont(&fonts::Font2);

    M5Cardputer.update();
    auto& s0 = M5Cardputer.Keyboard.keysState();
    std::vector<char> prevWord = s0.word;
    bool prevDel   = s0.del;
    bool prevEnter = s0.enter;

    while (true) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            if (c == 'y' || c == 'Y') return true;
            if (c == 'n' || c == 'N') return false;
        }
        if (s.enter && !prevEnter) return true;
        if (s.del   && !prevDel)   return false;

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

void drawDeleted() {
    M5Cardputer.Display.fillScreen(kBg);
    boot_ui::sectionHeader("deleted", kGreen);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    const char* msg = "note removed";
    int w = M5Cardputer.Display.textWidth(msg);
    M5Cardputer.Display.setCursor((kScreenW - w) / 2, 56);
    M5Cardputer.Display.print(msg);
}

} // namespace

namespace note_detail {

bool show(const String& title,
          const String& body,
          const String& filename,
          const String& subtitle) {
    const bool deletable = filename.length() > 0;
    int bodyTop = kStatusH;
    int bodyH   = kScreenH - kStatusH - kHintH;
    int maxPx   = kScreenW - 2 * kPadX;

    M5Canvas canvas(&M5Cardputer.Display);
    canvas.setColorDepth(16);
    bool canvasOk = canvas.createSprite(kScreenW, bodyH);
    if (canvasOk) {
        canvas.setFont(&fonts::Font2);
        canvas.setTextSize(1);
    } else {
        Serial.println("[note_detail] WARN canvas alloc failed");
    }

    std::vector<styled_text::Line> lines;
    if (canvasOk) {
        styled_text::parse(canvas, body, kIdle,
                           /*rightAlign=*/false, /*plainOnly=*/true,
                           maxPx, kScreenW, lines);
    } else {
        styled_text::Line l;
        styled_text::Segment s{body, kIdle};
        l.segments.push_back(s);
        lines.push_back(l);
    }

    int lineH = (canvasOk ? canvas.fontHeight() : 16) + 2;
    int visible = bodyH / lineH;
    if (visible < 1) visible = 1;
    int totalLines = (int)lines.size();

    int scroll = 0;
    bool dirty = true;
    std::vector<char> prevWord;
    bool prevDel = false;

    M5Cardputer.Display.fillScreen(kBg);

    auto repaint = [&]() {
        renderStatusRow(title, scroll, totalLines);
        renderHintBar(scroll == 0, scroll + visible >= totalLines,
                      deletable, subtitle);
        if (!canvasOk) return;
        canvas.fillScreen(kBg);
        canvas.drawLine(0, 0, kScreenW, 0, kDiv);
        int y = 2;
        for (int i = scroll; i < totalLines && i < scroll + visible; i++) {
            styled_text::render(canvas, lines[i], y, lineH, kScreenW,
                                kAccent, kDim, kIdle, kBg);
            y += lineH;
        }
        canvas.pushSprite(0, bodyTop);
    };

    while (true) {
        if (dirty) { repaint(); dirty = false; }

        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        if (s.del && !prevDel) {
            // Backspace -> back; release the sprite and return.
            if (canvasOk) canvas.deleteSprite();
            return false;
        }

        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;

            // backtick / tilde -> back (kept wired but not advertised)
            if (c == '`' || c == '~') {
                while (M5Cardputer.Keyboard.isPressed()) {
                    M5Cardputer.update(); delay(10);
                }
                if (canvasOk) canvas.deleteSprite();
                return false;
            }

            // Fn+D / Fn+d -> delete confirm
            if (deletable && s.fn && (c == 'd' || c == 'D')) {
                // Wait for the user to release the keys before showing
                // the confirm screen, so they don't immediately answer
                // it with the still-held keypress.
                while (M5Cardputer.Keyboard.isPressed()) {
                    M5Cardputer.update(); delay(10);
                }
                if (canvasOk) canvas.deleteSprite();
                bool yes = askDeleteConfirm(title);
                if (yes) {
                    bool ok = notestore::remove(filename);
                    Serial.printf("[note_detail] delete %s -> %s\n",
                                  filename.c_str(), ok ? "ok" : "FAIL");
                    drawDeleted();
                    delay(900);
                    return true;
                }
                // No: rebuild the canvas + redraw the detail
                canvasOk = canvas.createSprite(kScreenW, bodyH);
                if (canvasOk) {
                    canvas.setFont(&fonts::Font2);
                    canvas.setTextSize(1);
                    lines.clear();
                    styled_text::parse(canvas, body, kIdle,
                                       false, true, maxPx, kScreenW, lines);
                    totalLines = (int)lines.size();
                }
                M5Cardputer.Display.fillScreen(kBg);
                dirty = true;
                prevWord = s.word;
                prevDel = s.del;
                continue;
            }
        }

        if (s.fn) {
            for (char c : s.word) {
                bool wasPrev = false;
                for (char p : prevWord) if (p == c) { wasPrev = true; break; }
                if (wasPrev) continue;
                if (c == ',' || c == ';') {
                    if (scroll > 0) { scroll--; dirty = true; }
                } else if (c == '.' || c == '/') {
                    if (scroll + visible < totalLines) { scroll++; dirty = true; }
                }
            }
        }

        prevWord = s.word;
        prevDel  = s.del;
        delay(15);
    }
}

} // namespace note_detail
