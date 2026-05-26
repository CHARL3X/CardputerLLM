// Recording screen with a tape-recorder inspired live UI + 5-minute cap.
//
// Audio path: M5Cardputer.Speaker.end() releases the ES8311 codec's
// output side; M5Cardputer.Mic.begin() reconfigures it for input via
// M5Unified's _microphone_enabled_cb_cardputer_adv callback. The Cardputer
// ADV's mic pins (data_in=46, ws=43, bck=41) come from M5Unified's
// board-specific mic_cfg defaults, so we don't override anything.
//
// Loop cadence: Mic.record() is blocking and returns when its buffer
// fills. At 512 samples / 16 kHz that's ~32 ms per iteration -- naturally
// paces the loop, gives us ~30 Hz render rate, and stays well under
// the dma_buf_count * dma_buf_len = 1024-sample (~64 ms) internal mic
// queue so we don't drop samples on a busy SD write.
//
// Rendering: full-screen 240x135 M5Canvas. Every frame redraws the
// status bar, panel border, tape path + reels, spectrum, format strip,
// mic meter, and hint bar into the sprite, then pushSprite(0,0) once.
// Honors the rule from memory: never fillScreen the bare ST7789 at
// > 1 Hz -- all motion lives inside the canvas.

#include "record_screen.h"
#include "../audio/wav_writer.h"
#include "../audio/transcribe.h"
#include "../audio/title.h"
#include "../storage/note_store.h"
#include "boot_ui.h"
#include "note_detail.h"
#include <M5Cardputer.h>
#include <M5GFX.h>
#include <math.h>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
// 14 / 101 / 20 split: status bar, body, hint bar. Whole-canvas redraw
// happens every frame so the status bar can blink the REC dot and tick
// elapsed time without extra draw paths.
constexpr int kStatusH = 14;
constexpr int kHintH   = 20;
constexpr int kBodyY   = kStatusH;
constexpr int kBodyH   = kScreenH - kStatusH - kHintH;  // 101
constexpr int kPadX    = 4;

constexpr uint16_t kBg      = 0x0000;
constexpr uint16_t kDivider = 0x2104;   // cool dark complement
constexpr uint16_t kPanelBg = 0x0841;   // near-black with a hint of cool
constexpr uint16_t kDim     = 0x6B4D;
constexpr uint16_t kAccent  = 0xFE40;   // golden (Verbatim brand)
constexpr uint16_t kIdle    = 0xEF7D;
constexpr uint16_t kRed     = 0xF884;
constexpr uint16_t kTapeAmb = 0x9281;   // duller amber for tape path
constexpr uint16_t kVU      = 0x4FCA;
constexpr uint16_t kVUWarn  = 0xFD00;
constexpr uint16_t kVUClip  = 0xF884;
constexpr uint16_t kSpecLo  = 0x4FCA;   // cool low band
constexpr uint16_t kSpecMid = 0xFD60;   // amber mid
constexpr uint16_t kSpecHi  = 0xF884;   // red top

// Body panel (the "recorder face"). Rounded outer frame, inset spectrum
// window, two reels flanking, mic meter under the spectrum.
constexpr int kPanelX = 4;
constexpr int kPanelY = 17;
constexpr int kPanelW = 232;
constexpr int kPanelH = 96;
constexpr int kPanelR = 5;   // rounded corner radius

// Tape reels. Centers chosen so the spectrum window sits in the gap.
constexpr int kReelLX = 33;
constexpr int kReelRX = 207;
constexpr int kReelY  = 50;
constexpr int kReelR  = 18;
constexpr int kReelSpokes = 6;

// Spectrum window (front layer, sits over the tape path).
constexpr int kSpecX = 60;
constexpr int kSpecY = 30;
constexpr int kSpecW = 120;
constexpr int kSpecH = 36;
constexpr int kSpecBars = 14;
constexpr int kSpecBarW = 5;     // 14 bars * 5 px + 13 gaps * 3 px = 109
constexpr int kSpecGap  = 3;     // leaves ~5 px of breathing room each side

// Mic level meter under the spectrum panel.
constexpr int kMeterX = 18;
constexpr int kMeterY = 80;
constexpr int kMeterW = 204;
constexpr int kMeterH = 6;

constexpr uint32_t kHardCapMs   = 300000;          // 5 minutes
constexpr uint32_t kWarnMs      = 270000;          // start fast flash @ 4:30
constexpr size_t   kBlockSamples = 512;            // 32 ms @ 16 kHz
constexpr uint32_t kSampleRate  = 16000;
constexpr const char* kPath    = "/Cardputer/.recording.wav";

// ES8311 codec lives on the Cardputer ADV's internal I2C bus at addr 0x18.
// M5Unified initializes it with register 0x14 = 0x10 which selects MIC1
// input but pins the PGA at 0 dB ("minimum" per the comment in
// _microphone_enabled_cb_cardputer_adv). For 30cm conversational speech
// that puts samples at a few hundred LSB out of 32767 -- VU meter barely
// twitches. Rewrite 0x14 after Mic.begin() to keep the MIC1 selection
// but lift PGA into a usable range. Low nibble 0..7 -> 0/6/12/18/24/30/
// 36/42 dB. 24 dB is a starting point that should leave headroom for
// shouts without saturating; bump to 30 or 36 dB if voice still reads
// faint in the saved WAV.
constexpr uint8_t kEs8311Addr   = 0x18;
constexpr uint8_t kAdcReg14     = 0x14;
constexpr uint8_t kAdcReg14Val  = 0x17;  // MIC1 + 42 dB (max PGA)
constexpr uint8_t kMicMagnify   = 32;    // M5Unified default is 16

// After M5Unified's mic_enable_cb finishes powering up the codec analog
// stage and we snap the PGA from 0 dB to 42 dB, the input signal has a
// big transient (capacitor charging, bias point settling, plus the gain
// snap itself). Captured into the WAV that transient sounds like a loud
// pop at t=0 on playback. Discard the first N blocks before any samples
// are written to disk.
constexpr int    kWarmupBlocks  = 10;   // 10 * 32 ms = ~320 ms
constexpr uint32_t kSettleMs    = 150;  // brief analog-settle after PGA write
constexpr uint32_t kCodecReadyMs = 80;  // wait for ES8311 to accept I2C after cold begin()

// Editorial section header in the FreeSerifBoldItalic9pt7b serif --
// same flanking-hairlines pattern as boot_ui::sectionHeader, but with
// Verbatim's launcher-card typography for visual continuity.
void editorialHeader(const char* title, uint16_t color) {
    constexpr int kPad = 6;
    constexpr int kH   = 18;
    M5Cardputer.Display.fillRect(0, 0, kScreenW, kH, kBg);
    M5Cardputer.Display.setTextSize(1);
    int midY    = kH / 2;
    int leftEnd = kPad + 10;
    M5Cardputer.Display.drawLine(kPad, midY, leftEnd, midY, color);
    M5Cardputer.Display.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setTextColor(color, kBg);
    M5Cardputer.Display.drawString(title, leftEnd + 4, 1);
    int tw = M5Cardputer.Display.textWidth(title);
    int rightStart = leftEnd + 4 + tw + 4;
    M5Cardputer.Display.drawLine(rightStart, midY, kScreenW - kPad, midY, color);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

// --- Tape-recorder face helpers. All draw into the supplied canvas; the
// caller pushSprites once per frame. Keeping each helper small means the
// per-frame draw order in run() reads as a composition recipe rather
// than one long block of pixel math.

void drawStatusBar(M5Canvas& c, uint32_t elapsedMs, bool dotOn, bool warn) {
    c.fillRect(0, 0, kScreenW, kStatusH, kBg);

    // Red dot (blinks) + "VERBATIM REC" label. Two-tone: dot is filled
    // when on, hollow when off, so the blink is visible without flashing
    // the entire region.
    constexpr int kDotX = 7, kDotY = 7, kDotR = 3;
    if (dotOn) c.fillCircle(kDotX, kDotY, kDotR, kRed);
    else       c.drawCircle(kDotX, kDotY, kDotR, kRed);

    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kRed, kBg);
    c.setCursor(14, 4);
    c.print("VERBATIM REC");

    // Elapsed time right-aligned, in golden during normal play and in
    // warn-yellow once we're in the last 30 s before the cap.
    char tbuf[8];
    uint32_t secs = elapsedMs / 1000;
    snprintf(tbuf, sizeof(tbuf), "%u:%02u",
             (unsigned)(secs / 60), (unsigned)(secs % 60));
    c.setTextColor(warn ? kVUWarn : kAccent, kBg);
    int tw = c.textWidth(tbuf);
    c.setCursor(kScreenW - tw - kPadX, 4);
    c.print(tbuf);

    // Thin divider between status bar and body
    c.drawLine(0, kStatusH - 1, kScreenW, kStatusH - 1, kDivider);
}

void drawHintBar(M5Canvas& c) {
    int hy = kScreenH - kHintH;
    c.fillRect(0, hy, kScreenW, kHintH, kBg);
    c.drawLine(0, hy, kScreenW, hy, kDivider);

    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kDim, kBg);

    // Left: [any key] stop / save. Right: 5:00 cap. Two-line layout so
    // both can use Font0 without overflowing the 240 px width on the
    // brace characters.
    c.setCursor(kPadX, hy + 5);
    c.print("[any key] stop / save");

    const char* cap = "5:00 cap";
    int cw = c.textWidth(cap);
    c.setCursor(kScreenW - cw - kPadX, hy + 5);
    c.print(cap);
}

void drawPanelFrame(M5Canvas& c) {
    // Outer rounded frame in dim cool. Reads as the recorder's body
    // edge without screaming for attention.
    c.drawRoundRect(kPanelX, kPanelY, kPanelW, kPanelH, kPanelR, kDim);
}

void drawTapePath(M5Canvas& c) {
    // Two thin amber lines connecting reel centers (top + bottom of
    // tape ribbon). The spectrum panel draws over the middle section
    // later, so what's left visible is just the bits flanking the
    // reels -- like a real tape exiting/entering each spool.
    int y1 = kReelY - 3;
    int y2 = kReelY + 3;
    c.drawLine(kReelLX + kReelR, y1, kReelRX - kReelR, y1, kTapeAmb);
    c.drawLine(kReelLX + kReelR, y2, kReelRX - kReelR, y2, kTapeAmb);
}

void drawReel(M5Canvas& c, int cx, int cy, int r, float angleRad) {
    // Outer rim
    c.drawCircle(cx, cy, r, kDim);
    // Inner rim (gives the reel some depth without a heavy gradient)
    c.drawCircle(cx, cy, r - 4, 0x3186);

    // Spokes between hub and outer rim. cosf/sinf per spoke is fine --
    // 6 floats * 2 ops per frame is sub-microsecond on ESP32-S3.
    constexpr float kTwoPi = 6.28318531f;
    int innerR = 4;
    int outerR = r - 1;
    for (int i = 0; i < kReelSpokes; i++) {
        float a = angleRad + i * (kTwoPi / kReelSpokes);
        int x1 = cx + (int)(cosf(a) * innerR);
        int y1 = cy + (int)(sinf(a) * innerR);
        int x2 = cx + (int)(cosf(a) * outerR);
        int y2 = cy + (int)(sinf(a) * outerR);
        c.drawLine(x1, y1, x2, y2, kAccent);
    }

    // Hub: filled golden disc with a single-pixel black bore so it
    // reads as a hole rather than a button.
    c.fillCircle(cx, cy, 3, kAccent);
    c.drawPixel(cx, cy, kBg);
}

// Lightweight visualizer: phase-shifted sinusoids modulated by overall
// mic energy. Animates with real audio (loud -> taller, quiet ->
// shorter) without doing an FFT. Replaceable later by feeding actual
// band energies in via `peakHi` and adding a side-channel for per-band
// magnitudes -- isolated here so the swap is a one-function change.
void drawSpectrum(M5Canvas& c, int16_t peakHi, uint32_t tMs) {
    // Inset window: filled dark panel + thin border. Subtle horizontal
    // mid-rule for a "tape head" baseline feel.
    c.fillRect(kSpecX, kSpecY, kSpecW, kSpecH, kPanelBg);
    c.drawRect(kSpecX, kSpecY, kSpecW, kSpecH, kDim);
    int midY = kSpecY + kSpecH - 1;
    c.drawLine(kSpecX + 1, midY, kSpecX + kSpecW - 2, midY, kDivider);

    float energy = (float)peakHi / (float)INT16_MAX;
    if (energy < 0.05f) energy = 0.05f;   // idle "noise floor" visual

    float tSec = (float)tMs / 1000.0f;

    // Center the bar group horizontally inside the window
    int barsW = kSpecBars * kSpecBarW + (kSpecBars - 1) * kSpecGap;
    int startX = kSpecX + (kSpecW - barsW) / 2;
    int maxH   = kSpecH - 4;       // 2 px padding top + bottom
    int baseY  = kSpecY + kSpecH - 2;

    for (int i = 0; i < kSpecBars; i++) {
        // Each bar gets a different phase + slightly different frequency
        // so the bars don't all rise/fall in unison.
        float phase = i * 0.55f;
        float freq  = 2.2f + (i % 3) * 0.7f;
        float wave  = 0.5f + 0.5f * sinf(tSec * freq + phase);
        float mag   = energy * (0.25f + 0.85f * wave);
        if (mag > 1.0f) mag = 1.0f;

        int barH = (int)(mag * maxH);
        if (barH < 1) barH = 1;

        uint16_t col = kSpecLo;
        if (mag > 0.45f) col = kSpecMid;
        if (mag > 0.78f) col = kSpecHi;

        int bx = startX + i * (kSpecBarW + kSpecGap);
        int by = baseY - barH;
        c.fillRect(bx, by, kSpecBarW, barH, col);
    }
}

void drawFormatStrip(M5Canvas& c, const char* filename) {
    // Strip sits between the panel top and the spectrum window. Three
    // pieces: small REC badge (filled red rect), filename, format spec.
    int y = kPanelY + 4;

    // [REC] badge
    constexpr int kBadgeW = 22;
    constexpr int kBadgeH = 9;
    c.fillRect(kPanelX + 6, y, kBadgeW, kBadgeH, kRed);
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kBg, kRed);
    c.setCursor(kPanelX + 9, y + 2);
    c.print("REC");

    // Filename: kept short to avoid colliding with the format string on
    // the right. The actual on-disk WAV is a hidden scratch file; the
    // user-visible name only exists after Save, so this is a placeholder.
    c.setTextColor(kIdle, kBg);
    c.setCursor(kPanelX + 6 + kBadgeW + 6, y + 2);
    c.print(filename);

    // Format spec right-aligned. "16k . 16b . mono" reads as a stack
    // of mono-spaced facts rather than a sentence.
    c.setTextColor(kDim, kBg);
    const char* fmt = "16k . 16b . mono";
    int fw = c.textWidth(fmt);
    c.setCursor(kPanelX + kPanelW - fw - 6, y + 2);
    c.print(fmt);
}

void drawMicMeter(M5Canvas& c, int16_t peakHi) {
    c.drawRect(kMeterX, kMeterY, kMeterW, kMeterH, kDim);
    int inner = kMeterW - 2;
    int fill = (int)((int32_t)peakHi * inner / INT16_MAX);
    if (fill < 0) fill = 0;
    if (fill > inner) fill = inner;
    uint16_t fc = kVU;
    if (peakHi > (INT16_MAX * 7) / 8)   fc = kVUWarn;
    if (peakHi > (INT16_MAX * 19) / 20) fc = kVUClip;
    if (fill > 0) c.fillRect(kMeterX + 1, kMeterY + 1, fill, kMeterH - 2, fc);

    // Tiny "MIC" label to the left of the meter so the bar has a meaning.
    c.setFont(&fonts::Font0);
    c.setTextSize(1);
    c.setTextColor(kDim, kBg);
    int labelY = kMeterY + kMeterH + 2;
    c.setCursor(kMeterX, labelY);
    c.print("mic");
    // Right-side dB-ish marker so the right edge of the bar means
    // something. 0 dBFS-shaped, not calibrated.
    const char* edge = "0 dB";
    int ew = c.textWidth(edge);
    c.setCursor(kMeterX + kMeterW - ew, labelY);
    c.print(edge);
}

// Composes the whole recorder face. Order matters: panel frame and tape
// path go down first, then reels (which draw over the tape ends), then
// the spectrum window (which sits over the tape's middle section), then
// format strip (top of panel) and mic meter (bottom of panel).
void drawRecordingPanel(M5Canvas& c, const char* filename, int16_t peakHi,
                        uint32_t tMs, float reelAngle) {
    drawPanelFrame(c);
    drawTapePath(c);
    drawReel(c, kReelLX, kReelY, kReelR, reelAngle);
    drawReel(c, kReelRX, kReelY, kReelR, reelAngle);
    drawSpectrum(c, peakHi, tMs);
    drawFormatStrip(c, filename);
    drawMicMeter(c, peakHi);
}

// Forward declaration: drawError is defined later in this namespace,
// but transcribeAndShow (also below) calls it -- forward-declare so
// the compiler doesn't have to walk past every helper before reaching
// drawError's body.
void drawError(const char* head, const char* detail);

enum class Decision { Transcribe, Delete };

// Post-record decision screen. Direct-draw -- static layout, no animation,
// no flicker risk. Discrete-key dispatch (t/enter -> transcribe, d/del ->
// delete) instead of a navigation picker -- one keystroke decides.
Decision askWhatNext(uint32_t durMs, bool hardCapped) {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader("captured", 0x4FCA);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    uint32_t secs = durMs / 1000;
    char dbuf[24];
    snprintf(dbuf, sizeof(dbuf), "%u:%02u recorded",
             (unsigned)(secs / 60), (unsigned)(secs % 60));
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    int tw = M5Cardputer.Display.textWidth(dbuf);
    M5Cardputer.Display.setCursor((kScreenW - tw) / 2, 22);
    M5Cardputer.Display.print(dbuf);

    if (hardCapped) {
        M5Cardputer.Display.setFont(&fonts::Font0);
        M5Cardputer.Display.setTextColor(kVUWarn, kBg);
        const char* h = "(5:00 cap reached)";
        int hw = M5Cardputer.Display.textWidth(h);
        M5Cardputer.Display.setCursor((kScreenW - hw) / 2, 40);
        M5Cardputer.Display.print(h);
        M5Cardputer.Display.setFont(&fonts::Font2);
    }

    // Choice rows
    int y = hardCapped ? 56 : 52;
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    M5Cardputer.Display.setCursor(16, y);
    M5Cardputer.Display.print("[t]");
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    M5Cardputer.Display.setCursor(52, y);
    M5Cardputer.Display.print("transcribe");

    y += 18;
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    M5Cardputer.Display.setCursor(16, y);
    M5Cardputer.Display.print("[d]");
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    M5Cardputer.Display.setCursor(52, y);
    M5Cardputer.Display.print("delete");

    // Hint bar
    int hy = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 4);
    M5Cardputer.Display.print("[t][enter]  transcribe");
    M5Cardputer.Display.setCursor(kPadX, hy + 13);
    M5Cardputer.Display.print("[d][del]    delete");
    M5Cardputer.Display.setFont(&fonts::Font2);

    // Wait for a discrete key choice. Treat keys currently held at entry
    // as already-consumed (the stop-recording key would otherwise trigger
    // an immediate decision).
    M5Cardputer.update();
    auto& s0 = M5Cardputer.Keyboard.keysState();
    std::vector<char> prevWord = s0.word;
    bool prevDel   = s0.del;
    bool prevEnter = s0.enter;

    while (true) {
        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        for (char c : s.word) {
            bool wasPrev = false;
            for (char p : prevWord) if (p == c) { wasPrev = true; break; }
            if (wasPrev) continue;
            if (c == 't' || c == 'T') return Decision::Transcribe;
            if (c == 'd' || c == 'D') return Decision::Delete;
        }
        if (s.enter && !prevEnter) return Decision::Transcribe;
        if (s.del   && !prevDel)   return Decision::Delete;

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

void drawDecisionOutcome(const char* head, const char* line2, uint16_t headColor) {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader(head, headColor);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    int w = M5Cardputer.Display.textWidth(line2);
    M5Cardputer.Display.setCursor((kScreenW - w) / 2, 48);
    M5Cardputer.Display.print(line2);
}

// Targeted-redraw transcribe progress UI. The static frame (header, hint
// bar, bar outline) is drawn once; the progress callback updates only the
// label, fill, and percent text via fillRect on those regions. Avoids
// fillScreen so we don't flicker the unbuffered panel.
void drawTranscribeFrame() {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader("transcribing", kAccent);

    int hy = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 6);
    M5Cardputer.Display.print("first run takes ~10-30s . be patient");
    M5Cardputer.Display.setFont(&fonts::Font2);

    // Progress bar outline
    constexpr int barX = 16, barY = 64, barW = 208, barH = 12;
    M5Cardputer.Display.drawRect(barX - 1, barY - 1, barW + 2, barH + 2, kDim);
}

void drawTranscribeProgress(const transcribe::ProgressInfo& p) {
    constexpr int barX = 16, barY = 64, barW = 208, barH = 12;

    // Label
    M5Cardputer.Display.fillRect(0, 24, kScreenW, 18, kBg);
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);
    const char* label = p.uploading ? "uploading audio" : "waiting for whisper";
    int lw = M5Cardputer.Display.textWidth(label);
    M5Cardputer.Display.setCursor((kScreenW - lw) / 2, 28);
    M5Cardputer.Display.print(label);

    // Bar fill
    uint64_t num = (uint64_t)p.bytesSent * barW;
    int fillW   = (int)(num / (p.totalBytes ? p.totalBytes : 1));
    if (fillW < 0) fillW = 0;
    if (fillW > barW) fillW = barW;
    uint16_t fc = p.uploading ? kVU : kAccent;
    if (fillW > 0)         M5Cardputer.Display.fillRect(barX, barY, fillW, barH, fc);
    if (fillW < barW)      M5Cardputer.Display.fillRect(barX + fillW, barY, barW - fillW, barH, kBg);

    // Percent + bytes
    uint32_t pct = (uint32_t)((uint64_t)p.bytesSent * 100ULL / (p.totalBytes ? p.totalBytes : 1));
    char pb[64];
    snprintf(pb, sizeof(pb), "%u%%  %u / %u kB",
             (unsigned)pct,
             (unsigned)(p.bytesSent / 1024),
             (unsigned)(p.totalBytes / 1024));
    M5Cardputer.Display.fillRect(0, 86, kScreenW, 12, kBg);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    int tw = M5Cardputer.Display.textWidth(pb);
    M5Cardputer.Display.setCursor((kScreenW - tw) / 2, 88);
    M5Cardputer.Display.print(pb);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void drawTranscribeSuccess(const String& text) {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader("got it", 0x4FCA);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, kBg);

    // Word-wrapped preview, max ~3 lines * ~28 cols
    String preview = text;
    if (preview.length() > 120) preview = preview.substring(0, 120) + "...";

    int y = 22;
    int maxW = kScreenW - 16;
    String line;
    String word;
    auto flushLine = [&]() {
        if (line.length() == 0) return;
        M5Cardputer.Display.setCursor(8, y);
        M5Cardputer.Display.print(line);
        y += 16;
        line = "";
    };
    for (size_t i = 0; i <= preview.length(); i++) {
        char c = (i < preview.length()) ? preview.charAt(i) : ' ';
        if (c == ' ' || c == '\n' || i == preview.length()) {
            String probe = line;
            if (probe.length() > 0) probe += ' ';
            probe += word;
            if (M5Cardputer.Display.textWidth(probe.c_str()) > maxW) {
                flushLine();
                line = word;
            } else {
                line = probe;
            }
            word = "";
            if (c == '\n') flushLine();
            if (y > kScreenH - kHintH - 16) break;
        } else {
            word += c;
        }
    }
    flushLine();

    int hy = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 6);
    M5Cardputer.Display.print("full text on serial");
    M5Cardputer.Display.setCursor(kPadX, hy + 13);
    M5Cardputer.Display.print("any key to return");
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void drawTranscribeFailure(const transcribe::Result& r) {
    M5Cardputer.Display.fillScreen(kBg);
    editorialHeader("transcribe failed", 0xF884);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kVUWarn, kBg);
    const char* reason = transcribe::outcomeName(r.outcome);
    int rw = M5Cardputer.Display.textWidth(reason);
    M5Cardputer.Display.setCursor((kScreenW - rw) / 2, 28);
    M5Cardputer.Display.print(reason);

    if (r.httpStatus > 0) {
        char hbuf[16];
        snprintf(hbuf, sizeof(hbuf), "http %d", r.httpStatus);
        M5Cardputer.Display.setTextColor(kIdle, kBg);
        int hw = M5Cardputer.Display.textWidth(hbuf);
        M5Cardputer.Display.setCursor((kScreenW - hw) / 2, 50);
        M5Cardputer.Display.print(hbuf);
    }

    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, 80);
    M5Cardputer.Display.print("wav kept on sd for retry");
    M5Cardputer.Display.setCursor(kPadX, 92);
    M5Cardputer.Display.print("full details on serial");

    int hy = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hy, kScreenW, hy, kDivider);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hy + 6);
    M5Cardputer.Display.print("any key to return");
    M5Cardputer.Display.setFont(&fonts::Font2);
}

// Quick centered single-line status for the in-between "naming.." /
// "saving.." beats. Direct-draw is fine -- the line replaces what
// drawTranscribeProgress last left on screen, and the small fillRect
// only touches the body region.
void drawWorkingStatus(const char* line) {
    M5Cardputer.Display.fillRect(0, kStatusH + 1, kScreenW,
                                 kScreenH - kStatusH - kHintH - 1, kBg);
    M5Cardputer.Display.setFont(&fonts::FreeSerifBoldItalic9pt7b);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextDatum(top_center);
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    M5Cardputer.Display.drawString(line, kScreenW / 2, 54);
    M5Cardputer.Display.setTextDatum(top_left);
    M5Cardputer.Display.setFont(&fonts::Font2);
}

void transcribeAndShow(const String& apiKey,
                       const String& txModel,
                       const String& titleModel,
                       uint32_t      durationMs) {
    drawTranscribeFrame();
    transcribe::Result tr = transcribe::runWav(
        kPath, apiKey, txModel,
        [&](const transcribe::ProgressInfo& p) {
            drawTranscribeProgress(p);
        });

    if (tr.outcome != transcribe::Outcome::Ok) {
        Serial.printf("[transcribe] FAIL outcome=%s http=%d detail=%s\n",
                      transcribe::outcomeName(tr.outcome),
                      tr.httpStatus,
                      tr.errorDetail.c_str());
        drawTranscribeFailure(tr);
        boot_ui::waitForAnyKey();
        return;
    }
    Serial.println("[transcribe] === TRANSCRIPT BEGIN ===");
    Serial.println(tr.text);
    Serial.println("[transcribe] === TRANSCRIPT END ===");

    // ---- Phase 4: title ----
    drawWorkingStatus("naming...");
    title::Result titleResult = title::generate(tr.text, apiKey, titleModel);
    String finalTitle = titleResult.title.length() > 0
                       ? titleResult.title
                       : String("Untitled note");
    Serial.printf("[title] outcome=%s -> '%s'\n",
                  title::outcomeName(titleResult.outcome),
                  finalTitle.c_str());

    // ---- Phase 4: save .md ----
    drawWorkingStatus("saving...");
    notestore::NoteMeta meta;
    meta.createdUtc      = notestore::utcIsoNow();
    meta.title           = finalTitle;
    meta.durationSec     = (durationMs + 500) / 1000;
    meta.modelTranscribe = txModel;
    meta.modelTitle      = titleModel;

    String filename = notestore::save(meta, tr.text);
    if (filename.length() == 0) {
        Serial.println("[record] note save failed; keeping wav for retry");
        drawError("save failed", "could not write .md");
        return;
    }

    // ---- Phase 4: delete WAV (only after .md is safely on disk) ----
    if (!SD.remove(kPath)) {
        Serial.printf("[record] WARN failed to delete %s\n", kPath);
    } else {
        Serial.printf("[record] cleaned up %s\n", kPath);
    }

    // ---- Hand off to detail viewer ----
    // Pass the filename so Fn+D in the viewer can delete the
    // just-saved note inline. Subtitle shows the path for transparency.
    String subtitle = String("/Cardputer/notes/") + filename;
    note_detail::show(finalTitle, tr.text, filename, subtitle);
}

void drawError(const char* head, const char* detail) {
    M5Cardputer.Display.fillScreen(kBg);
    boot_ui::header(head, 0x7800);
    boot_ui::centerText(detail, 56, kRed);
    boot_ui::footer("any key to return");
    boot_ui::waitForAnyKey();
}

} // namespace

namespace record_screen {

bool run(const String& apiKey,
         const String& txModel,
         const String& titleModel) {
    Serial.println("[record] entering");

    // Release speaker so the ES8311 can switch to input mode. Idempotent.
    M5Cardputer.Speaker.end();

    // Bump digital magnification before begin so it takes effect with the
    // newly-configured I2S.
    {
        auto mc = M5Cardputer.Mic.config();
        mc.magnification = kMicMagnify;
        M5Cardputer.Mic.config(mc);
    }

    if (!M5Cardputer.Mic.begin()) {
        Serial.println("[record] mic begin failed");
        drawError("mic init failed", "could not start mic");
        return false;
    }

    // Cold boot path: ES8311 hasn't been powered up since reset, so its
    // I2C interface needs a beat after Mic.begin() returns before it will
    // honor a PGA register write. Without this, the first session records
    // at PGA min; subsequent sessions are fine because the codec stays
    // warm between Mic.end()/Mic.begin() cycles.
    delay(kCodecReadyMs);

    // Mic.begin() ran M5Unified's _microphone_enabled_cb_cardputer_adv
    // which pinned ES8311 reg 0x14 = 0x10 (PGA min). Override now.
    auto writePga = [](const char* tag) {
        if (M5Cardputer.In_I2C.writeRegister8(kEs8311Addr, kAdcReg14, kAdcReg14Val,
                                              400000)) {
            Serial.printf("[record] es8311 pga %s: reg 0x%02X = 0x%02X (42 dB max)\n",
                          tag, kAdcReg14, kAdcReg14Val);
        } else {
            Serial.printf("[record] WARN es8311 pga %s write failed\n", tag);
        }
    };
    writePga("init");

    // Let the analog stage stabilize after the gain snap, then drain
    // the codec's startup transient out of the mic queue before the WAV
    // header is anchored at t=0.
    delay(kSettleMs);
    {
        int16_t warm[kBlockSamples];
        for (int i = 0; i < kWarmupBlocks; i++) {
            if (!M5Cardputer.Mic.record(warm, kBlockSamples, kSampleRate, false)) {
                Serial.println("[record] warm-up record returned false; continuing");
                break;
            }
        }
        Serial.printf("[record] warm-up drained %d blocks (~%u ms)\n",
                      kWarmupBlocks,
                      (unsigned)(kWarmupBlocks * kBlockSamples * 1000 / kSampleRate));
    }

    // Re-assert PGA after the I2S pipeline has been running for a few
    // hundred ms. If the cold-boot write got clobbered by a late codec
    // init step, this catches it.
    writePga("reassert");

    wav::WavWriter writer;
    if (!writer.begin(kPath, kSampleRate, 1, 16)) {
        Serial.println("[record] wav open failed");
        M5Cardputer.Mic.end();
        drawError("sd write failed", "could not open .recording.wav");
        return false;
    }

    // Full-screen sprite -- one fillScreen-equivalent per frame happens
    // in RAM, never on the bare ST7789. ~64 KB at 16-bit color depth.
    M5Canvas body(&M5Cardputer.Display);
    body.setColorDepth(16);
    bool canvasOk = body.createSprite(kScreenW, kScreenH);
    if (canvasOk) {
        body.setFont(&fonts::Font2);
        body.setTextSize(1);
    } else {
        Serial.printf("[record] WARN canvas alloc %dx%d failed; direct draw fallback\n",
                      kScreenW, kScreenH);
        // Clear the display once so the previous screen doesn't bleed
        // through during the recording session.
        M5Cardputer.Display.fillScreen(kBg);
    }

    Serial.printf("[record] heap pre-loop free=%u min=%u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());

    int16_t  buf[kBlockSamples];
    uint32_t t0          = millis();
    int16_t  peakHi      = 0;
    bool     stopped     = false;
    bool     hardCapped  = false;
    bool     writeFailed = false;
    uint32_t lastRender  = 0;

    // Ignore the key that brought us here -- treat the keyboard as "down"
    // initially so the user has to release-then-press to stop.
    M5Cardputer.update();
    bool prevAnyKey = M5Cardputer.Keyboard.isPressed();

    while (!stopped) {
        if (!M5Cardputer.Mic.record(buf, kBlockSamples, kSampleRate, false)) {
            Serial.println("[record] mic record returned false");
            break;
        }

        // Per-block peak with mild decay for VU stability.
        int16_t blockPeak = 0;
        for (size_t i = 0; i < kBlockSamples; i++) {
            int16_t v = buf[i];
            int16_t a = (v < 0) ? (v == INT16_MIN ? INT16_MAX : (int16_t)(-v)) : v;
            if (a > blockPeak) blockPeak = a;
        }
        if (blockPeak >= peakHi) peakHi = blockPeak;
        else                     peakHi = (int16_t)((peakHi * 7 + blockPeak) / 8);

        if (!writer.write(buf, kBlockSamples)) {
            Serial.println("[record] sd write failed mid-stream");
            writeFailed = true;
            break;
        }

        M5Cardputer.update();
        bool anyKey = M5Cardputer.Keyboard.isPressed();
        if (anyKey && !prevAnyKey) stopped = true;
        prevAnyKey = anyKey;

        uint32_t elapsed = millis() - t0;
        if (elapsed >= kHardCapMs) {
            stopped     = true;
            hardCapped  = true;
        }

        if (canvasOk && millis() - lastRender >= 40) {
            lastRender = millis();
            bool warn  = elapsed >= kWarnMs;

            // Dot blink: 2 Hz normal, 4 Hz inside the warn window so the
            // user feels the imminent cap.
            uint32_t halfPeriod = warn ? 125 : 250;
            bool     dotOn      = ((millis() / halfPeriod) & 1) == 0;

            // Reels rotate continuously at ~90 deg/sec. Tying to elapsed
            // rather than absolute millis means the reel orientation is a
            // function of how much tape has rolled, not wall-clock time.
            constexpr float kReelOmega = 1.5708f;  // PI/2 rad/sec
            float reelAngle = (elapsed / 1000.0f) * kReelOmega;

            body.fillScreen(kBg);
            drawStatusBar(body, elapsed, dotOn, warn);
            drawRecordingPanel(body, "recording.wav", peakHi, millis(), reelAngle);
            drawHintBar(body);

            body.pushSprite(0, 0);
        }
    }

    bool savedOk = !writeFailed && writer.end();
    M5Cardputer.Mic.end();

    // After Mic.end() the ES8311 is in CSM power-down AND the shared I2S
    // BCK/WS pins (GPIO 41/43) are no longer driven by any port -- the PA
    // picks up the floating clocks and emits a faint buzz at idle.
    // M5Unified's _speaker_enabled_cb_cardputer_adv has an empty
    // disabled_bulk_data, so Speaker.end() never explicitly silences the
    // codec; the only quiet state we have is "Speaker begun, idle DAC,"
    // which is what boot leaves us in. Restore that here.
    M5Cardputer.Speaker.begin();

    if (canvasOk) body.deleteSprite();

    if (!savedOk) {
        if (writeFailed) writer.cancel();
        drawError("save failed", "wav header rewrite failed");
        return false;
    }

    uint32_t dur = writer.durationMs();
    Serial.printf("[record] saved %s . %u ms . %u samples%s\n",
                  kPath, (unsigned)dur, (unsigned)writer.samplesWritten(),
                  hardCapped ? " (hard cap)" : "");

    Decision d = askWhatNext(dur, hardCapped);
    if (d == Decision::Delete) {
        SD.remove(kPath);
        Serial.printf("[record] deleted %s\n", kPath);
        drawDecisionOutcome("deleted", "wav removed from sd", 0xF884);
        delay(900);
    } else {
        // Phase 4: full capture loop. transcribe -> title -> save .md
        // -> delete WAV -> note_detail. On any failure mid-flow the
        // WAV is preserved so the user can retry.
        Serial.printf("[record] dispatching transcription: %s tx=%s title=%s\n",
                      kPath, txModel.c_str(), titleModel.c_str());
        transcribeAndShow(apiKey, txModel, titleModel, dur);
    }
    return true;
}

} // namespace record_screen
