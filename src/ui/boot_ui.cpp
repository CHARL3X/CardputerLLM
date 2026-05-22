#include "boot_ui.h"
#include <M5Cardputer.h>

namespace {
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kHeaderH = 14;
constexpr int kFooterH = 14;
} // namespace

namespace boot_ui {

void clear() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
}

void header(const String& msg, uint16_t bg) {
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kHeaderH, bg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_WHITE, bg);
    M5Cardputer.Display.setCursor(4, 1);
    M5Cardputer.Display.print(msg);
}

void footer(const String& msg, uint16_t bg) {
    M5Cardputer.Display.fillRect(0, kScreenH - kFooterH, kScreenW, kFooterH, bg);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_BLACK, bg);
    M5Cardputer.Display.setCursor(4, kScreenH - kFooterH + 3);
    M5Cardputer.Display.print(msg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
}

void centerText(const String& msg, int y, uint16_t color) {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int w = M5Cardputer.Display.textWidth(msg.c_str());
    M5Cardputer.Display.setTextColor(color, TFT_BLACK);
    M5Cardputer.Display.setCursor((kScreenW - w) / 2, y);
    M5Cardputer.Display.print(msg);
}

void leftText(const String& msg, int y, uint16_t color) {
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(color, TFT_BLACK);
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print(msg);
}

namespace { int g_logY = 0; }

void startLog() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    // "BOOT" header with amber hairlines flanking
    constexpr uint16_t kAccent = 0xFD60;
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kAccent, TFT_BLACK);
    int midY = 8;
    int leftEnd = 6 + 10;
    M5Cardputer.Display.drawLine(6, midY, leftEnd, midY, kAccent);
    M5Cardputer.Display.setCursor(leftEnd + 4, 1);
    M5Cardputer.Display.print("BOOT");
    int tw = M5Cardputer.Display.textWidth("BOOT");
    int rightStart = leftEnd + 4 + tw + 4;
    M5Cardputer.Display.drawLine(rightStart, midY, 240 - 6, midY, kAccent);
    g_logY = 22;
}

void step(const String& label, bool ok, const String& detail) {
    constexpr uint16_t kOk    = 0x4FCA;
    constexpr uint16_t kErr   = 0xF884;
    constexpr uint16_t kIdle  = 0xEF7D;
    constexpr uint16_t kDim   = 0x4208;
    constexpr int padX = 6;
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int y = g_logY;
    int lh = M5Cardputer.Display.fontHeight() + 2;

    // label
    M5Cardputer.Display.setTextColor(kIdle, TFT_BLACK);
    M5Cardputer.Display.setCursor(padX, y);
    M5Cardputer.Display.print(label);
    int labelEnd = M5Cardputer.Display.getCursorX();

    // status text on the right
    const char* statusText = ok ? "[ok]" : "[!!]";
    int statusW = M5Cardputer.Display.textWidth(statusText);
    int statusX = 240 - padX - statusW;

    // dotted leader between label and status
    int leaderStart = labelEnd + 4;
    int leaderEnd = statusX - 4;
    int dotY = y + lh - 6;
    for (int x = leaderStart; x < leaderEnd; x += 4) {
        M5Cardputer.Display.fillRect(x, dotY, 2, 2, kDim);
    }

    // status
    M5Cardputer.Display.setTextColor(ok ? kOk : kErr, TFT_BLACK);
    M5Cardputer.Display.setCursor(statusX, y);
    M5Cardputer.Display.print(statusText);

    // optional detail line (smaller font, dim, indented)
    if (detail.length() > 0) {
        g_logY += lh;
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(padX + 8, g_logY + 2);
        M5Cardputer.Display.print(detail);
        M5Cardputer.Display.setFont(&fonts::Font2);
        g_logY += 10;
    } else {
        g_logY += lh;
    }
    delay(110);
}

void finishLog() {
    // Final "ready." centered, brief pause.
    constexpr uint16_t kAccent = 0xFD60;
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kAccent, TFT_BLACK);
    String r = "ready.";
    int w = M5Cardputer.Display.textWidth(r.c_str());
    int y = g_logY + 4;
    M5Cardputer.Display.setCursor((240 - w) / 2, y);
    M5Cardputer.Display.print(r);
    delay(380);
}

void waitForAnyKey() {
    // Wait for current keys to release, then for any new press.
    while (true) {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) break;
        delay(15);
    }
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) {
            // wait for release before returning so we don't double-fire
            while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
            return;
        }
        delay(15);
    }
}

} // namespace boot_ui
