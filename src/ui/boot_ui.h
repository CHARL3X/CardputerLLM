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

// Polished section header in chat-style: amber hairlines flanking a title.
// No filled background, matches the in-app block <<header>> style.
void sectionHeader(const String& title, uint16_t accent = 0xFD60);

// Two-line bottom hint area. Lines render in a dim color with a hairline
// separator above. Used by setup screens for key-binding cheat sheets.
void hintBar(const String& line1, const String& line2 = "");

void waitForAnyKey();

// Typewriter-style diagnostic log used during the boot sequence.
// startLog() clears the screen and draws a thin "BOOT" header.
// step() appends one row with a dim dotted leader and a colored
// [ok] / [!] status indicator. Each step takes ~120ms for pacing.
void startLog();
void step(const String& label, bool ok = true, const String& detail = "");
void finishLog();

} // namespace boot_ui
