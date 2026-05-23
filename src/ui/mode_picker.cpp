// Mode picker -- first interactive screen on the merged Cardputer
// firmware. M5Canvas body double-buffer per the flicker memory.
//
// Two stacked rows: LLM (chat) and Verbatim (voice). The highlight
// defaults to whatever the user picked last time. Small ambient
// chevron animation on the selected row so the screen doesn't feel
// frozen during the brief moment the user is deciding.

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
constexpr uint16_t kAccent = 0xFD60;
constexpr uint16_t kFaint  = 0x4208;

struct Option {
    const char* title;
    const char* desc;
};

constexpr Option kOptions[] = {
    { "CardputerLLM", "chat with any model" },
    { "Verbatim",     "voice notes + ask"   },
};

void renderStatus() {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kStatusH, kBg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int midY    = kStatusH / 2;
    int leftEnd = kPadX + 10;
    M5Cardputer.Display.drawLine(kPadX, midY, leftEnd, midY, kAccent);
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    M5Cardputer.Display.setCursor(leftEnd + 4, 1);
    const char* h = "pick a mode";
    M5Cardputer.Display.print(h);
    int tw = M5Cardputer.Display.textWidth(h);
    int rightStart = leftEnd + 4 + tw + 4;
    M5Cardputer.Display.drawLine(rightStart, midY, kScreenW - kPadX, midY, kAccent);
}

void renderHint() {
    int y = kScreenH - kHintH;
    M5Cardputer.Display.fillRect(0, y, kScreenW, kHintH, kBg);
    M5Cardputer.Display.drawLine(0, y, kScreenW, y, kDiv);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, y + 4);
    M5Cardputer.Display.print(",/.   switch mode");
    M5Cardputer.Display.setCursor(kPadX, y + 13);
    M5Cardputer.Display.print("[ret] launch");
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void renderBody(M5Canvas& c, int sel, uint32_t animPhase) {
    c.fillScreen(kBg);
    c.drawLine(0, 0, kScreenW, 0, kDiv);

    int rowH = 36;
    int yTop = 8;

    for (int i = 0; i < 2; i++) {
        int y = yTop + i * (rowH + 4);
        bool active = (i == sel);
        uint16_t titleC = active ? kAccent : kIdle;
        uint16_t descC  = active ? kDim    : kFaint;

        // Selection bar
        if (active) {
            c.fillRect(kPadX, y + 4, 3, rowH - 8, kAccent);
        } else {
            c.fillRect(kPadX, y + 4, 3, rowH - 8, 0x2104);
        }

        // Title (Font2, size 1)
        c.setFont(&fonts::Font2);
        c.setTextSize(1);
        c.setTextColor(titleC, kBg);
        c.setCursor(kPadX + 10, y + 4);
        c.print(kOptions[i].title);

        // Pulsing chevron on the active row for ambient life
        if (active) {
            int chevX = kPadX + 10 + c.textWidth(kOptions[i].title) + 6;
            int chevY = y + 8;
            uint16_t cc = ((animPhase / 4) & 1) ? kAccent : kFaint;
            // tiny "▶" via two triangles
            c.fillTriangle(chevX, chevY,
                           chevX + 5, chevY + 4,
                           chevX, chevY + 8, cc);
        }

        // Description (Font0, dim)
        c.setFont(&fonts::Font0);
        c.setTextColor(descC, kBg);
        c.setCursor(kPadX + 10, y + 22);
        c.print(kOptions[i].desc);
    }

    c.setFont(&fonts::Font2);
    c.setTextSize(1);
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
    if (canvasOk) {
        renderBody(body, sel, animPhase);
        body.pushSprite(0, kStatusH);
    }

    bool dirty = false;
    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;
    uint32_t lastAnim = millis();

    while (true) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        // Enter -> commit
        if (s.enter && !prevEnter) {
            if (canvasOk) body.deleteSprite();
            Serial.printf("[picker] selected %s\n", kOptions[sel].title);
            return (uint8_t)sel;
        }

        // Rising-edge char handlers
        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            if (c == ',' || c == ';') {
                if (sel > 0) { sel--; dirty = true; }
            } else if (c == '.' || c == '/') {
                if (sel < 1) { sel++; dirty = true; }
            } else if (c == '1') {
                sel = 0; dirty = true;
            } else if (c == '2') {
                sel = 1; dirty = true;
            }
        }

        // ~80ms anim tick keeps the chevron alive
        if (millis() - lastAnim > 80) {
            lastAnim = millis();
            animPhase++;
            dirty = true;
        }

        if (dirty && canvasOk) {
            renderBody(body, sel, animPhase);
            body.pushSprite(0, kStatusH);
            dirty = false;
        }

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

} // namespace mode_picker
