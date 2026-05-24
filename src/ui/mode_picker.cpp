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
#include <math.h>

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
constexpr uint16_t kLlmAccent     = 0xFD60;  // amber / orange (LLM outline)
constexpr uint16_t kVoxAccent     = 0xFE40;  // golden yellow (Verbatim outline)

// Complementary cool tone for the inset sheen + chevron. Both cards
// share this so the warm outlines (the mode-tellers) read against a
// unified cool interior. Steel-blue family -- the complement of the
// warm amber/gold on the color wheel.
//
// Sheen uses the deeper muted shade; the chevron oscillates between
// invisible (0x0000) and the lighter shade so the pulse reads as an
// actual fade-in / fade-out, not a small brightness flicker.
constexpr uint16_t kComplement      = 0x42BF;  // muted steel-blue (sheen)
constexpr uint16_t kComplementLight = 0xBEFF;  // pale sky-blue (chevron peak)

// Scale an RGB565 color's brightness uniformly. f in 0..255.
uint16_t scaleColor(uint16_t c, uint8_t f) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b = c         & 0x1F;
    r = (r * f) / 255;
    g = (g * f) / 255;
    b = (b * f) / 255;
    return (r << 11) | (g << 5) | b;
}

// Linearly blend two RGB565 colors. t in 0..255 (0 = a, 255 = b).
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
    int rA = (a >> 11) & 0x1F;
    int gA = (a >> 5)  & 0x3F;
    int bA = a         & 0x1F;
    int rB = (b >> 11) & 0x1F;
    int gB = (b >> 5)  & 0x3F;
    int bB = b         & 0x1F;
    int r = (rA * (255 - t) + rB * t) / 255;
    int g = (gA * (255 - t) + gB * t) / 255;
    int bl = (bA * (255 - t) + bB * t) / 255;
    return (r << 11) | (g << 5) | bl;
}

struct Option {
    const char*       title;
    const char*       desc;
    uint16_t          accent;
    const lgfx::IFont* titleFont;
};

const Option kOptions[2] = {
    { "CardputerLLM", "chat with any LLM",
      kLlmAccent, &fonts::FreeSansBold9pt7b },
    { "Verbatim",     "voice notes + ask",
      kVoxAccent, &fonts::FreeSerifBoldItalic9pt7b },
};

// Walk the 4 edges of a rectangle drawing each outline pixel with a
// gradient color sampled along the (i+j)/(w+h-2) diagonal. Top-left
// pixel gets `startColor`; bottom-right pixel gets `endColor`. Used
// to give the card a pencil-thin, angle-lit outline.
void drawGradientOutline(M5Canvas& c, int x, int y, int w, int h,
                         uint16_t startColor, uint16_t endColor) {
    int diag = (w + h - 2);
    if (diag < 1) diag = 1;

    // Top edge (j = 0)
    for (int i = 0; i < w; i++) {
        int t = (i * 255) / diag;
        c.writePixel(x + i, y, blend565(startColor, endColor, t));
    }
    // Bottom edge (j = h - 1)
    for (int i = 0; i < w; i++) {
        int t = ((i + h - 1) * 255) / diag;
        if (t > 255) t = 255;
        c.writePixel(x + i, y + h - 1, blend565(startColor, endColor, t));
    }
    // Left edge (i = 0)
    for (int j = 1; j < h - 1; j++) {
        int t = (j * 255) / diag;
        c.writePixel(x, y + j, blend565(startColor, endColor, t));
    }
    // Right edge (i = w - 1)
    for (int j = 1; j < h - 1; j++) {
        int t = ((j + w - 1) * 255) / diag;
        if (t > 255) t = 255;
        c.writePixel(x + w - 1, y + j, blend565(startColor, endColor, t));
    }
}

// Inset sheen: fill the card interior with a subtle diagonal gradient,
// brightest at the top-left corner, fading to black at the bottom-right.
// Reads as a soft reflection across the card surface. Cheap pixel-fill
// on the canvas (writePixel is RAM-only, ~50 ns per).
void drawInsetSheen(M5Canvas& c, int x, int y, int w, int h, uint16_t accent) {
    uint16_t base = scaleColor(accent, 56);  // ~22% brightness at top-left
    int diag = (w + h - 2);
    if (diag < 1) diag = 1;
    for (int j = 1; j < h - 1; j++) {
        for (int i = 1; i < w - 1; i++) {
            int t = ((i + j) * 255) / diag;
            c.writePixel(x + i, y + j, blend565(base, 0x0000, t));
        }
    }
}

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

// Card layout inside the body region (101 px tall).
//
// Using drawString + top_left datum so the title's TOP-LEFT lands
// exactly at the given coords -- no more guessing whether setCursor
// means baseline or line-top for GFX fonts.
//
//   y=5  -> card 0 (LLM)
//   y=53 -> card 1 (Verbatim)
//   top 5 / card 40 / middle 8 / card 40 / bottom 8 = 101 px
constexpr int kCard0Y = 5;
constexpr int kCard1Y = 53;
constexpr int kCardH  = 40;
constexpr int kCardX  = 8;
constexpr int kCardW  = kScreenW - 2 * kCardX;

void renderCard(M5Canvas& c, int idx, bool active, uint32_t animPhase) {
    const Option& opt = kOptions[idx];
    int y = (idx == 0) ? kCard0Y : kCard1Y;

    // Active card gets the inset sheen first (interior fill). The sheen
    // uses the cool COMPLEMENT to the warm outline -- warm frame, cool
    // fill, like a two-tone cover. Inactive card stays on pure black.
    if (active) {
        drawInsetSheen(c, kCardX, y, kCardW, kCardH, kComplement);
    }

    // Pencil-thin outline masked on a diagonal gradient: bright accent
    // at top-left, fading to invisible at bottom-right. Active card
    // uses the full accent; inactive uses a softened version so it
    // visually recedes but stays present.
    uint16_t startColor = active ? opt.accent : scaleColor(opt.accent, 110);
    drawGradientOutline(c, kCardX, y, kCardW, kCardH, startColor, 0x0000);

    // Title with top_left datum -- TOP of text at (kCardX+10, y+4).
    c.setTextDatum(top_left);
    c.setFont(opt.titleFont);
    c.setTextSize(1);
    c.setTextColor(active ? opt.accent : kDim, kBg);
    c.drawString(opt.title, kCardX + 10, y + 4);

    // Subtitle (Font0 bitmap).
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(active ? kIdle : kDim, kBg);
    c.drawString(opt.desc, kCardX + 10, y + 28);

    // Chevron: sin-eased fade between invisible (kBg) and the lighter
    // complement. Full fade-in / fade-out, 1.2 s period -- reads as a
    // proper pulse rather than a small brightness wobble.
    if (active) {
        int chevX = kCardX + kCardW - 14;
        int chevY = y + kCardH / 2 - 5;
        float t    = (float)(millis() % 1200) / 1200.0f;
        float ease = (sinf(t * 6.283185307f) + 1.0f) * 0.5f;  // 0..1 smooth
        uint8_t bt = (uint8_t)(ease * 255.0f);
        uint16_t cc = blend565(0x0000, kComplementLight, bt);
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
