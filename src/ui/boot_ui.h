#pragma once
#include <Arduino.h>

// Small set of direct-draw helpers used by boot-time and menu-invoked
// setup flows. These bypass the chat-screen canvas because they only run
// once per session and don't need the flicker fix.

namespace boot_ui {

void clear();
void header(const String& msg, uint16_t bg = 0x000F);     // navy default
void footer(const String& msg, uint16_t bg = 0x7BEF);     // dark grey
void centerText(const String& msg, int y, uint16_t color = 0xFFFF);
void leftText(const String& msg, int y, uint16_t color = 0xFFFF);

void waitForAnyKey();

} // namespace boot_ui
