#pragma once
#include <Arduino.h>
#include <vector>
#include <M5GFX.h>

// Parses a small markup dialect into renderable "Line" records and draws
// them into an M5Canvas. Used for assistant turns; user turns bypass the
// parser and go through as plain text.
//
// Inline tags (any position within a line):
//   [h]..[/h]      hot/highlight    yellow
//   [k]..[/k]      label/key        warm dim
//   [v]..[/v]      value            cream
//   [ok]..[/ok]    success          green
//   [w]..[/w]      warning          orange
//   [!]..[/!]      error            red
//   [?]..[/?]      quiet/aside      very dim
//
// Block tags (whole line, must appear at start of line):
//   <<title>>      section header   amber title with hairlines
//   --- (or more)  horizontal rule  thin amber line
//   - item         bullet           tiny amber square + indented text
//   > quote        quote            left amber bar + indented dim text
//   [bar:NN]       progress bar     filled rectangle with NN%
//   ```            code fence       multi-line dim block, no wrap

namespace styled_text {

enum class Block : uint8_t {
    Body,
    Header,
    Divider,
    Bullet,
    Quote,
    Code,
    Bar,
};

struct Segment {
    String   text;
    uint16_t color;
};

struct Line {
    std::vector<Segment> segments;
    Block    block      = Block::Body;
    int      barPercent = 0;
    bool     rightAlign = false;
};

// Parses src into wrapped lines. `plainOnly` skips tag parsing for user
// turns. `canvas` is needed for text width measurement.
void parse(M5Canvas& canvas, const String& src,
           uint16_t baseColor, bool rightAlign, bool plainOnly,
           int maxPxBody, int screenW,
           std::vector<Line>& out);

// Renders a single Line into canvas at vertical position `y` (canvas-local).
void render(M5Canvas& canvas, const Line& line, int y, int lineH,
            int screenW, uint16_t accentColor, uint16_t dimColor,
            uint16_t valColor, uint16_t bg);

} // namespace styled_text
