// Mode picker -- card-style launcher. Each mode gets its own card
// with mode-specific color, typography, and ambient micro-interaction.
//
// - LLM card: warm amber, FreeSansBold12pt7b (modern sans)
// - Verbatim card: mint/teal, FreeSerifBoldItalic12pt7b (editorial serif)
//
// Active card: solid border in mode color, faint background tint
// (~1/4 brightness of the accent), and a pulsing chevron at the right
// edge so the screen has ambient life while the user decides.
//
// Inactive card: dim grey border, no tint, dim title color. Still
// readable, but visually receded.
//
// M5Canvas body double-buffer per the flicker memory.

#include "mode_picker.h"
#include "boot_ui.h"
#include <M5Cardputer.h>
#include <M5GFX.h>

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
constexpr uint16_t kFaint  = 0x4208;

// Mode brand colors. LLM keeps the original cassette amber so users
// flashing from the CardputerLLM standalone get continuity; Verbatim
// goes mint/teal to feel like a different tool.
constexpr uint16_t kLlmAccent     = 0xFD60;  // amber
constexpr uint16_t kLlmAccentDim  = 0x3940;  // amber at ~1/4 brightness (card tint)
constexpr uint16_t kVoxAccent     = 0x57DC;  // soft teal-mint
constexpr uint16_t kVoxAccentDim  = 0x11E7;  // mint at ~1/4 brightness

struct Option {
    const char*       title;
    const char*       desc;
    uint16_t          accent;
    uint16_t          accentDim;
    const lgfx::IFont* titleFont;
};

const Option kOptions[2] = {
    { "CardputerLLM", "chat with any LLM",
      kLlmAccent, kLlmAccentDim, &fonts::FreeSansBold12pt7b },
    { "Verbatim",     "voice notes + ask",
      kVoxAccent, kVoxAccentDim, &fonts::FreeSerifBoldItalic12pt7b },
};

void renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    // Centered "pick a mode" with amber hairlines flanking, like the
    // styled_text <<title>> block in CardputerLLM. Cool-toned center
    // dot would be wrong here since the picker is mode-neutral.
    constexpr uint16_t kHeaderColor = 0xFD60;
    const char* h = "pick a mode";
    int tw = M5Cardputer.Display.textWidth(h);
    int titleX = (kScreenW - tw) / 2;
    int midY   = kStatusH / 2;
    M5Cardputer.Display.drawLine(kPadX, midY, titleX - 4, midY, kHeaderColor);
    M5Cardputer.Display.setTextColor(kHeaderColor, kBg);
    M5Cardputer.Display.setCursor(titleX, 1);
    M5Cardputer.Display.print(h);
    M5Cardputer.Display.drawLine(titleX + tw + 4, midY,
                                 kScreenW - kPadX, midY, kHeaderColor);
}

void renderHint() {
    int y = kScreenH - kHintH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kHintH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDiv);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, y + 4);
    M5Cardputer.Display.print(",/. switch    1/2 jump");
    M5Cardputer.Display.setCursor(kPadX, y + 13);
    M5Cardputer.Display.print("[ret] launch the highlighted mode");
    M5Cardputer.Display.setFont(&fonts::Font2);
}

// Card layout inside the body region (101 px tall):
//   y=4   -> top of card 0 (LLM)
//   y=48  -> top of card 1 (Verbatim)
//   each card is 44 px tall, 4 px gaps top/middle/bottom
constexpr int kCard0Y = 4;
constexpr int kCard1Y = 52;
constexpr int kCardH  = 44;
constexpr int kCardX  = 6;
constexpr int kCardW  = kScreenW - 2 * kCardX;

void renderCard(M5Canvas& c, int idx, bool active, uint32_t animPhase) {
    const Option& opt = kOptions[idx];
    int y = (idx == 0) ? kCard0Y : kCard1Y;

    uint16_t borderColor = active ? opt.accent : kFaint;

    // Faint mode-tinted background fill on the active card. Subtle --
    // ~1/4 brightness so it reads as "highlighted" not "blasting."
    if (active) {
        c.fillRoundRect(kCardX, y, kCardW, kCardH, 6, opt.accentDim);
    }
    c.drawRoundRect(kCardX, y, kCardW, kCardH, 6, borderColor);
    if (active) {
        // Double-stroke the inactive-row outline for a slight glow
        c.drawRoundRect(kCardX + 1, y + 1, kCardW - 2, kCardH - 2, 5,
                        opt.accent);
    }

    // Title in mode font + mode color
    c.setFont(opt.titleFont);
    c.setTextSize(1);
    c.setTextColor(active ? opt.accent : kDim, active ? opt.accentDim : kBg);
    int titleBaseline = y + 22;
    c.setCursor(kCardX + 10, titleBaseline);
    c.print(opt.title);

    // Description in Font0 underneath
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(active ? kIdle : kDim, active ? opt.accentDim : kBg);
    c.setCursor(kCardX + 10, y + kCardH - 12);
    c.print(opt.desc);

    // Active card: pulsing chevron at the right edge
    if (active) {
        int chevX = kCardX + kCardW - 14;
        int chevY = y + kCardH / 2 - 5;
        uint16_t cc = ((animPhase / 4) & 1) ? opt.accent : opt.accentDim;
        c.fillTriangle(chevX, chevY,
                       chevX + 7, chevY + 5,
                       chevX, chevY + 10, cc);
    }
}

} // namespace

namespace mode_picker {

uint8_t run(uint8_t defaultMode) {
    if (defaultMode > 1) defaultMode = 0;
    int sel = defaultMode;

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(kBg);

    M5Canvas body(&M5Cardputer.Display);
    body.setColorDepth(16);
    int bodyH = kScreenH - kStatusH - kHintH;
    bool canvasOk = body.createSprite(kScreenW, bodyH);
    if (canvasOk) {
        body.setFont(&fonts::Font2);
        body.setTextSize(1);
    }

    renderStatus();
    renderHint();

    uint32_t animPhase = 0;
    auto repaint = [&]() {
        if (!canvasOk) return;
        body.fillScreen(kBg);
        renderCard(body, 0, sel == 0, animPhase);
        renderCard(body, 1, sel == 1, animPhase);
        body.pushSprite(0, kStatusH);
    };
    repaint();

    bool dirty = false;
    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;
    uint32_t lastAnim = millis();

    while (true) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        if (s.enter && !prevEnter) {
            if (canvasOk) body.deleteSprite();
            Serial.printf("[picker] selected %s\n", kOptions[sel].title);
            return (uint8_t)sel;
        }

        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            if (c == ',' || c == ';') {
                if (sel > 0) { sel--; dirty = true; }
            } else if (c == '.' || c == '/') {
                if (sel < 1) { sel++; dirty = true; }
            } else if (c == '1') {
                if (sel != 0) { sel = 0; dirty = true; }
            } else if (c == '2') {
                if (sel != 1) { sel = 1; dirty = true; }
            }
        }

        // ~80 ms anim tick for the chevron
        if (millis() - lastAnim > 80) {
            lastAnim = millis();
            animPhase++;
            dirty = true;
        }
        if (dirty) { repaint(); dirty = false; }

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

} // namespace mode_picker
