// Unified welcome screen for the merged Cardputer firmware. Shown
// once per device on first boot, ahead of the mode picker.
#include "welcome.h"
#include "boot_ui.h"
#include <M5Cardputer.h>

namespace welcome {

namespace {
constexpr uint16_t kAccent = 0xFD60;
constexpr uint16_t kCream  = 0xEF7D;
constexpr uint16_t kDim    = 0x6B4D;
} // namespace

void run() {
    boot_ui::clear();
    boot_ui::sectionHeader("welcome");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    int y = 22;
    M5Cardputer.Display.setTextColor(kCream, TFT_BLACK);
    M5Cardputer.Display.setCursor(6, y);  y += 14;
    M5Cardputer.Display.print("a pocket terminal for");
    M5Cardputer.Display.setCursor(6, y);  y += 18;
    M5Cardputer.Display.setTextColor(kAccent, TFT_BLACK);
    M5Cardputer.Display.print("chat & voice notes.");

    y += 2;
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setCursor(6, y);  y += 12;
    M5Cardputer.Display.print("llm  . chat with any model");
    M5Cardputer.Display.setCursor(6, y);  y += 12;
    M5Cardputer.Display.print("vox  . record + ask notes");
    M5Cardputer.Display.setCursor(6, y);  y += 12;
    M5Cardputer.Display.print("pick a mode next.");

    boot_ui::hintBar("press any key to begin");

    boot_ui::waitForAnyKey();
}

} // namespace welcome
