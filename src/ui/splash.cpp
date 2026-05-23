// Unified Cardputer splash. Same cassette-futurism animation as both
// prior apps (chime, corner brackets, typewriter wordmark reveal,
// divider sweep, version), wordmark/subtitle retargeted for the
// merged firmware.
#include "splash.h"
#include "../storage/settings.h"
#include <M5Cardputer.h>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

constexpr uint16_t kBg     = 0x0000;
constexpr uint16_t kAccent = 0xFD60;
constexpr uint16_t kIdle   = 0xEF7D;
constexpr uint16_t kDim    = 0x6B4D;
constexpr uint16_t kFaint  = 0x2104;

void drawCorners(uint16_t color) {
    constexpr int len = 8;
    constexpr int m   = 4;
    M5Cardputer.Display.drawLine(m, m, m + len, m, color);
    M5Cardputer.Display.drawLine(m, m, m, m + len, color);
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, m, kScreenW - 1 - m - len, m, color);
    M5Cardputer.Display.drawLine(kScreenW - 1 - m, m, kScreenW - 1 - m, m + len, color);
    M5Cardputer.Display.drawLine(m, kScreenH - 1 - m, m + len, kScreenH - 1 - m, color);
    M5Cardputer.Display.drawLine(m, kScreenH - 1 - m, m, kScreenH - 1 - m - len, color);
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

    if (settings::bootSound()) {
        M5Cardputer.Speaker.setVolume(40);
        M5Cardputer.Speaker.tone(523,  70);  delay(80);
        M5Cardputer.Speaker.tone(659,  70);  delay(80);
        M5Cardputer.Speaker.tone(784,  70);  delay(80);
        M5Cardputer.Speaker.tone(1047, 110); delay(120);
    }

    drawCorners(kDim);
    delay(140);
    drawCorners(kAccent);
    delay(60);

    const char* title = "CARDPUTER";
    int titleLen = (int)strlen(title);
    M5Cardputer.Display.setTextSize(2);
    int charW = M5Cardputer.Display.textWidth("M");
    int totalW = charW * titleLen;
    int x0 = (kScreenW - totalW) / 2;
    int y0 = 36;
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    for (int i = 0; i < titleLen; i++) {
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

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    // Subtitle nods to both apps: L L M (chat) . V O X (voice notes).
    const char* sub = "L L M . V O X";
    int sw = M5Cardputer.Display.textWidth(sub);
    M5Cardputer.Display.setCursor((kScreenW - sw) / 2, 76);
    M5Cardputer.Display.print(sub);

    delay(150);

    int lineY    = 96;
    int halfMax  = 90;
    int midX     = kScreenW / 2;
    for (int half = 0; half <= halfMax; half += 6) {
        M5Cardputer.Display.drawLine(midX - half, lineY,
                                     midX + half, lineY, kAccent);
        delay(18);
    }

    M5Cardputer.Display.setTextColor(kDim, kBg);
    const char* ver = "v1.0";
    int vw = M5Cardputer.Display.textWidth(ver);
    M5Cardputer.Display.setCursor((kScreenW - vw) / 2, 108);
    M5Cardputer.Display.print(ver);

    delay(750);

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
