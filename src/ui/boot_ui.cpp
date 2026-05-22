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
