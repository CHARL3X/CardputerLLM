#include "splash.h"
#include <M5Cardputer.h>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

constexpr uint16_t kBg     = 0x0000;
constexpr uint16_t kAccent = 0xFD60; // warm amber
constexpr uint16_t kIdle   = 0xEF7D; // cream
constexpr uint16_t kDim    = 0x6B4D; // dim warm grey
constexpr uint16_t kFaint  = 0x2104; // very dim

// Draws four corner brackets to frame the splash. Cassette/CRT feel.
void drawCorners(uint16_t color) {
    constexpr int len = 8;
    constexpr int m   = 4; // margin
    // top-left
    M5Cardputer.Display.drawLine(m, m, m + len, m, color);
    M5Cardputer.Display.drawLine(m, m, m, m + len, color);
    // top-right
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, m, kScreenW - 1 - m - len, m, color);
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, m, kScreenW - 1 - m, m + len, color);
    // bottom-left
    M5Cardputer.Display.drawLine(m, kScreenH - 1 - m, m + len, kScreenH - 1 - m, color);
    M5Cardputer.Display.drawLine(m, kScreenH - 1 - m, m, kScreenH - 1 - m - len, color);
    // bottom-right
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, kScreenH - 1 - m,
                                 kScreenW - 1 - m - len, kScreenH - 1 - m, color);
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, kScreenH - 1 - m,
                                 kScreenW - 1 - m, kScreenH - 1 - m - len, color);
}

} // namespace

namespace splash {

void run() {
    M5Cardputer.Display.fillScreen(kBg);
    M5Cardputer.Display.setFont(&fonts::Font2);

    // 1) Corner brackets ease in
    drawCorners(kDim);
    delay(140);
    drawCorners(kAccent);
    delay(60);

    // 2) Typewriter reveal of CARDPUTER at size 2
    const char* title = "CARDPUTER";
    int titleLen = (int)strlen(title);
    M5Cardputer.Display.setTextSize(2);
    int charW = M5Cardputer.Display.textWidth("M"); // best width estimate
    int totalW = charW * titleLen;
    int x0 = (kScreenW - totalW) / 2;
    int y0 = 36;
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    for (int i = 0; i < titleLen; i++) {
        // erase prior region (defensive against subpixel kerning)
        M5Cardputer.Display.fillRect(x0, y0, totalW, 32, kBg);
        char buf[16];
        memcpy(buf, title, i + 1);
        buf[i + 1] = 0;
        int w = M5Cardputer.Display.textWidth(buf);
        M5Cardputer.Display.setCursor((kScreenW - w) / 2, y0);
        M5Cardputer.Display.print(buf);
        delay(55);
    }

    delay(180);

    // 3) "L L M" subtitle pops in
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    const char* sub = "L  L  M";
    int sw = M5Cardputer.Display.textWidth(sub);
    M5Cardputer.Display.setCursor((kScreenW - sw) / 2, 76);
    M5Cardputer.Display.print(sub);

    delay(150);

    // 4) Horizontal divider draws outward from center
    int lineY    = 96;
    int halfMax  = 90;
    int midX     = kScreenW / 2;
    for (int half = 0; half <= halfMax; half += 6) {
        M5Cardputer.Display.drawLine(midX - half, lineY,
                                     midX + half, lineY, kAccent);
        delay(18);
    }

    // 5) Version label, dim
    M5Cardputer.Display.setTextColor(kDim, kBg);
    const char* ver = "phase 8 . dev";
    int vw = M5Cardputer.Display.textWidth(ver);
    M5Cardputer.Display.setCursor((kScreenW - vw) / 2, 108);
    M5Cardputer.Display.print(ver);

    delay(750);

    // 6) Wipe out: collapse the divider back to center
    for (int half = halfMax; half >= 0; half -= 8) {
        M5Cardputer.Display.fillRect(midX - halfMax, lineY,
                                     2 * halfMax + 1, 1, kBg);
        M5Cardputer.Display.drawLine(midX - half, lineY,
                                     midX + half, lineY, kAccent);
        delay(10);
    }

    M5Cardputer.Display.fillScreen(kBg);
}

} // namespace splash
