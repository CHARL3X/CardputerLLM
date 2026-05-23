// Copied verbatim from CardputerLLM/src/ui/styled_text.cpp. Reused as-is
// for rendering assistant turns in Verbatim's Phase 6 ask mode.
#include "styled_text.h"

namespace styled_text {

namespace {

constexpr uint16_t kHot   = 0xFFE0;
constexpr uint16_t kKey   = 0x9C73;
constexpr uint16_t kVal   = 0xEF7D;
constexpr uint16_t kOk    = 0x4FCA;
constexpr uint16_t kWarn  = 0xFD00;
constexpr uint16_t kErr   = 0xF884;
constexpr uint16_t kAside = 0x4208;
constexpr int      kPadX  = 4;

struct TagDef {
    const char* open;
    const char* close;
    uint16_t    color;
    const char* prefix;
};

const TagDef kTags[] = {
    {"[h]",  "[/h]",  kHot,   nullptr},
    {"[k]",  "[/k]",  kKey,   nullptr},
    {"[v]",  "[/v]",  kVal,   nullptr},
    {"[ok]", "[/ok]", kOk,    "+ "},
    {"[w]",  "[/w]",  kWarn,  "! "},
    {"[!]",  "[/!]",  kErr,   "x "},
    {"[?]",  "[/?]",  kAside, nullptr},
};
constexpr int kNumTags = sizeof(kTags) / sizeof(kTags[0]);

bool startsAt(const String& s, int i, const char* p) {
    int n = strlen(p);
    if (i + n > (int)s.length()) return false;
    for (int k = 0; k < n; k++) if (s.charAt(i + k) != p[k]) return false;
    return true;
}

int detectBar(const String& s) {
    if (!startsAt(s, 0, "[bar:")) return -1;
    int i = 5;
    int n = 0;
    bool any = false;
    while (i < (int)s.length() && isDigit(s.charAt(i))) {
        n = n * 10 + (s.charAt(i) - '0');
        any = true;
        i++;
    }
    if (!any || i >= (int)s.length() || s.charAt(i) != ']') return -1;
    if (n > 100) n = 100;
    return n;
}

std::vector<Segment> inlineExpand(const String& src, uint16_t baseColor) {
    std::vector<Segment> out;
    std::vector<uint16_t> colorStack;
    colorStack.push_back(baseColor);

    Segment cur;
    cur.color = baseColor;
    auto pushCur = [&]() {
        if (cur.text.length() == 0) return;
        if (!out.empty() && out.back().color == cur.color) {
            out.back().text += cur.text;
        } else {
            out.push_back(cur);
        }
        cur.text = "";
    };

    int i = 0;
    const int n = src.length();
    while (i < n) {
        if (src.charAt(i) == '[') {
            bool matched = false;
            for (int t = 0; t < kNumTags; t++) {
                if (startsAt(src, i, kTags[t].open)) {
                    pushCur();
                    colorStack.push_back(kTags[t].color);
                    cur.color = kTags[t].color;
                    if (kTags[t].prefix) cur.text += kTags[t].prefix;
                    i += strlen(kTags[t].open);
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            for (int t = 0; t < kNumTags; t++) {
                if (startsAt(src, i, kTags[t].close)) {
                    pushCur();
                    if (colorStack.size() > 1) colorStack.pop_back();
                    cur.color = colorStack.back();
                    i += strlen(kTags[t].close);
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        cur.text += src.charAt(i);
        i++;
    }
    pushCur();
    return out;
}

void wrapSegments(M5Canvas& canvas,
                  const std::vector<Segment>& input, int maxPx,
                  Block block, bool rightAlign,
                  std::vector<Line>& out) {
    struct CC { char c; uint16_t color; };
    std::vector<CC> flat;
    flat.reserve(64);
    for (auto& seg : input) {
        for (size_t i = 0; i < seg.text.length(); i++) {
            flat.push_back({seg.text.charAt((unsigned)i), seg.color});
        }
    }
    if (flat.empty()) {
        Line l;
        l.block = block;
        l.rightAlign = rightAlign;
        out.push_back(l);
        return;
    }

    int i = 0;
    int n = (int)flat.size();
    while (i < n) {
        int j = i;
        int lastBreak = -1;
        String probe;
        probe.reserve(64);
        while (j < n) {
            char c = flat[j].c;
            if (c == '\n') { lastBreak = j; j++; break; }
            probe += c;
            if (canvas.textWidth(probe.c_str()) > maxPx) {
                probe.remove(probe.length() - 1);
                break;
            }
            if (c == ' ') lastBreak = j;
            j++;
        }
        int end = j;
        if (end < n && flat[end].c != '\n') {
            if (lastBreak > i) end = lastBreak;
        }
        if (end <= i) end = i + 1;

        Line line;
        line.block = block;
        line.rightAlign = rightAlign;
        uint16_t curColor = flat[i].color;
        Segment s;
        s.color = curColor;
        for (int k = i; k < end; k++) {
            if (flat[k].color != curColor) {
                if (s.text.length() > 0) line.segments.push_back(s);
                s = Segment();
                s.color = flat[k].color;
                curColor = flat[k].color;
            }
            s.text += flat[k].c;
        }
        if (s.text.length() > 0) line.segments.push_back(s);

        if (!line.segments.empty()) {
            auto& last = line.segments.back().text;
            if (last.length() > 0 && last.charAt(last.length() - 1) == ' ') {
                last.remove(last.length() - 1);
            }
        }
        out.push_back(line);
        i = end;
        while (i < n && (flat[i].c == ' ' || flat[i].c == '\n')) i++;
    }
}

String markdownToTags(const String& src) {
    String out;
    out.reserve(src.length() + 32);
    const int n = src.length();
    int i = 0;
    while (i < n) {
        char c = src.charAt(i);
        bool atLineStart = (i == 0) || (src.charAt(i - 1) == '\n');

        if (atLineStart && c == '`' && i + 2 < n
            && src.charAt(i + 1) == '`' && src.charAt(i + 2) == '`') {
            int end = src.indexOf("\n```", i + 3);
            int copyEnd = (end < 0) ? n : end + 4;
            out += src.substring(i, copyEnd);
            i = copyEnd;
            continue;
        }

        if (atLineStart && c == '#') {
            int j = i;
            while (j < n && src.charAt(j) == '#') j++;
            int hashCount = j - i;
            if (hashCount >= 1 && hashCount <= 6 && j < n && src.charAt(j) == ' ') {
                int lineEnd = src.indexOf('\n', j);
                if (lineEnd < 0) lineEnd = n;
                String title = src.substring(j + 1, lineEnd);
                title.trim();
                out += "<<";
                out += title;
                out += ">>";
                i = lineEnd;
                continue;
            }
        }

        if (atLineStart && isDigit(c)) {
            int j = i;
            while (j < n && isDigit(src.charAt(j))) j++;
            if (j + 1 < n && src.charAt(j) == '.' && src.charAt(j + 1) == ' ') {
                out += "- ";
                i = j + 2;
                continue;
            }
        }
        if (atLineStart && c == '*' && i + 1 < n && src.charAt(i + 1) == ' ') {
            out += "- ";
            i += 2;
            continue;
        }

        if (c == '*' && i + 1 < n && src.charAt(i + 1) == '*') {
            int searchFrom = i + 2;
            int end = src.indexOf("**", searchFrom);
            if (end > searchFrom && end - searchFrom <= 120) {
                out += "[h]";
                out += src.substring(searchFrom, end);
                out += "[/h]";
                i = end + 2;
                continue;
            }
        }
        if (c == '*' && i + 1 < n
            && src.charAt(i + 1) != '*' && src.charAt(i + 1) != ' ') {
            int searchFrom = i + 1;
            int end = -1;
            for (int k = searchFrom; k < n; k++) {
                if (src.charAt(k) == '*') {
                    if (k + 1 < n && src.charAt(k + 1) == '*') continue;
                    end = k;
                    break;
                }
                if (src.charAt(k) == '\n') break;
            }
            if (end > searchFrom && end - searchFrom <= 120) {
                out += "[v]";
                out += src.substring(searchFrom, end);
                out += "[/v]";
                i = end + 1;
                continue;
            }
        }
        if (c == '`' && (i + 1 >= n || src.charAt(i + 1) != '`')) {
            int end = -1;
            for (int k = i + 1; k < n; k++) {
                if (src.charAt(k) == '`') { end = k; break; }
                if (src.charAt(k) == '\n') break;
            }
            if (end > i + 1 && end - (i + 1) <= 80) {
                out += "[k]";
                out += src.substring(i + 1, end);
                out += "[/k]";
                i = end + 1;
                continue;
            }
        }

        out += c;
        i++;
    }
    return out;
}

} // namespace

void parse(M5Canvas& canvas, const String& src,
           uint16_t baseColor, bool rightAlign, bool plainOnly,
           int maxPxBody, int screenW,
           std::vector<Line>& out) {
    if (src.length() == 0) {
        Line l; l.rightAlign = rightAlign;
        out.push_back(l);
        return;
    }

    if (plainOnly) {
        std::vector<Segment> segs;
        segs.push_back({src, baseColor});
        wrapSegments(canvas, segs, maxPxBody, Block::Body, rightAlign, out);
        return;
    }

    String normalized = markdownToTags(src);
    const String& work = normalized;

    int i = 0;
    const int n = (int)work.length();
    bool inCode = false;

    while (i < n) {
        int e = work.indexOf('\n', i);
        if (e < 0) e = n;
        String raw = work.substring(i, e);

        if (raw.length() > 0 && raw.charAt(raw.length() - 1) == '\r') {
            raw.remove(raw.length() - 1);
        }

        if (raw.length() >= 3 && raw.startsWith("```")) {
            inCode = !inCode;
            i = e + 1;
            continue;
        }
        if (inCode) {
            int maxPxCode = maxPxBody - 8;
            String trimmed = raw;
            while (canvas.textWidth(trimmed.c_str()) > maxPxCode
                   && trimmed.length() > 0) {
                trimmed.remove(trimmed.length() - 1);
            }
            if (trimmed.length() < raw.length()) trimmed += String((char)0x85);
            Line l;
            l.block = Block::Code;
            l.segments.push_back({trimmed, kKey});
            out.push_back(l);
            i = e + 1;
            continue;
        }

        if (raw.length() >= 4 && raw.startsWith("<<") && raw.endsWith(">>")) {
            Line l;
            l.block = Block::Header;
            String title = raw.substring(2, raw.length() - 2);
            title.trim();
            l.segments.push_back({title, baseColor});
            out.push_back(l);
            i = e + 1;
            continue;
        }
        {
            String t = raw; t.trim();
            if (t.length() >= 3) {
                bool allDash = true;
                for (size_t k = 0; k < t.length(); k++) {
                    if (t.charAt((unsigned)k) != '-') { allDash = false; break; }
                }
                if (allDash) {
                    Line l;
                    l.block = Block::Divider;
                    out.push_back(l);
                    i = e + 1;
                    continue;
                }
            }
        }
        if (raw.startsWith("- ")) {
            String content = raw.substring(2);
            auto segs = inlineExpand(content, baseColor);
            wrapSegments(canvas, segs, maxPxBody - 12, Block::Bullet,
                         rightAlign, out);
            i = e + 1;
            continue;
        }
        if (raw.startsWith("> ")) {
            String content = raw.substring(2);
            auto segs = inlineExpand(content, baseColor);
            wrapSegments(canvas, segs, maxPxBody - 8, Block::Quote,
                         rightAlign, out);
            i = e + 1;
            continue;
        }
        int bar = detectBar(raw);
        if (bar >= 0) {
            Line l;
            l.block = Block::Bar;
            l.barPercent = bar;
            out.push_back(l);
            i = e + 1;
            continue;
        }

        auto segs = inlineExpand(raw, baseColor);
        wrapSegments(canvas, segs, maxPxBody, Block::Body, rightAlign, out);
        i = e + 1;
    }
}

void render(M5Canvas& canvas, const Line& line, int y, int lineH,
            int screenW, uint16_t accent, uint16_t dim,
            uint16_t valColor, uint16_t bg) {
    auto drawSegs = [&](int startX, uint16_t fallback) {
        int x = startX;
        for (const auto& seg : line.segments) {
            canvas.setTextColor(seg.color, bg);
            canvas.setCursor(x, y);
            canvas.print(seg.text);
            x += canvas.textWidth(seg.text.c_str());
        }
    };

    switch (line.block) {
        case Block::Body: {
            if (line.rightAlign) {
                int total = 0;
                for (const auto& s : line.segments) total += canvas.textWidth(s.text.c_str());
                int x = screenW - kPadX - total;
                if (x < kPadX) x = kPadX;
                drawSegs(x, valColor);
            } else {
                drawSegs(kPadX, valColor);
            }
            break;
        }
        case Block::Header: {
            String title = line.segments.size() ? line.segments[0].text : String();
            int tw = canvas.textWidth(title.c_str());
            int midY = y + lineH / 2;
            int leftEnd = kPadX + 10;
            canvas.drawLine(kPadX, midY, leftEnd, midY, accent);
            canvas.setTextColor(accent, bg);
            canvas.setCursor(leftEnd + 4, y);
            canvas.print(title);
            int rightStart = leftEnd + 4 + tw + 4;
            if (rightStart < screenW - kPadX) {
                canvas.drawLine(rightStart, midY, screenW - kPadX, midY, accent);
            }
            break;
        }
        case Block::Divider: {
            int midY = y + lineH / 2;
            canvas.drawLine(kPadX, midY, screenW - kPadX, midY, accent);
            break;
        }
        case Block::Bullet: {
            int sqY = y + (lineH - 4) / 2;
            canvas.fillRect(kPadX + 3, sqY, 4, 4, accent);
            drawSegs(kPadX + 12, valColor);
            break;
        }
        case Block::Quote: {
            canvas.fillRect(kPadX, y + 1, 2, lineH - 2, accent);
            drawSegs(kPadX + 8, valColor);
            break;
        }
        case Block::Code: {
            canvas.fillRect(kPadX, y + 1, 2, lineH - 2, dim);
            canvas.fillRect(kPadX + 4, y, screenW - 2*kPadX - 4, lineH, 0x1082);
            drawSegs(kPadX + 8, dim);
            break;
        }
        case Block::Bar: {
            int barX = kPadX + 4;
            int barY = y + 3;
            int barH = lineH - 8;
            int labelW = 28;
            int barW = screenW - barX - labelW - kPadX;
            canvas.drawRect(barX, barY, barW, barH, dim);
            int fill = (barW - 2) * line.barPercent / 100;
            if (fill > 0) canvas.fillRect(barX + 1, barY + 1, fill, barH - 2, accent);
            canvas.setTextColor(valColor, bg);
            canvas.setCursor(barX + barW + 6, y);
            String pct = String(line.barPercent) + "%";
            canvas.print(pct);
            break;
        }
    }
}

} // namespace styled_text
