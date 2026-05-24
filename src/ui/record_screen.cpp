// Recording screen with VU + elapsed counter + 5-minute hard cap.
//
// Audio path: M5Cardputer.Speaker.end() releases the ES8311 codec's
// output side; M5Cardputer.Mic.begin() reconfigures it for input via
// M5Unified's _microphone_enabled_cb_cardputer_adv callback. The Cardputer
// ADV's mic pins (data_in=46, ws=43, bck=41) come from M5Unified's
// board-specific mic_cfg defaults, so we don't override anything.
//
// Loop cadence: Mic.record() is blocking and returns when its buffer
// fills. At 512 samples / 16 kHz that's ~32 ms per iteration -- naturally
// paces the loop, gives us a 30 Hz VU update rate, and stays well under
// the dma_buf_count * dma_buf_len = 1024-sample (~64 ms) internal mic
// queue so we don't drop samples on a busy SD write.
//
// Rendering: 240 x 103 M5Canvas double-buffer for the body region. Status
// row (right-aligned "REC") and hint bar are direct-draw once at entry.
// Honors the rule from memory: never repaint via fillScreen on the bare
// display at >1 Hz.

#include "record_screen.h"
#include "../audio/wav_writer.h"
#include "../audio/transcribe.h"
#include "../audio/title.h"
#include "../storage/note_store.h"
#include "boot_ui.h"
#include "note_detail.h"
#include <M5Cardputer.h>
#include <M5GFX.h>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kStatusH = 12;
constexpr int kHintH   = 20;
constexpr int kPadX    = 4;

constexpr uint16_t kBg      = 0x0000;
constexpr uint16_t kDivider = 0x2104;
constexpr uint16_t kDim     = 0x6B4D;
constexpr uint16_t kAccent  = 0x07FF;
constexpr uint16_t kIdle    = 0xEF7D;
constexpr uint16_t kRed     = 0xF884;
constexpr uint16_t kVU      = 0x4FCA;
constexpr uint16_t kVUWarn  = 0xFD00;
constexpr uint16_t kVUClip  = 0xF884;

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

void drawStaticChrome() {
    M5Cardputer.Display.fillScreen(kBg);

    // Status row: "REC" right-aligned in red
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kRed, kBg);
    const char* label = "REC";
    int lw = M5Cardputer.Display.textWidth(label);
    M5Cardputer.Display.setCursor(kScreenW - kPadX - lw, 2);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.setFont(&fonts::Font2);

    // Hint bar
    int hintY = kScreenH - kHintH;
    M5Cardputer.Display.drawLine(0, hintY, kScreenW, hintY, kDivider);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setTextColor(kDim, kBg);
    M5Cardputer.Display.setCursor(kPadX, hintY + 6);
    M5Cardputer.Display.print("any key to stop  .  5:00 hard cap");
    M5Cardputer.Display.setFont(&fonts::Font2);
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
    boot_ui::sectionHeader("captured", 0x4FCA);

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
    boot_ui::sectionHeader(head, headColor);
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
    boot_ui::sectionHeader("transcribing", kAccent);

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
    boot_ui::sectionHeader("got it", 0x4FCA);

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
    boot_ui::sectionHeader("transcribe failed", 0xF884);

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
    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kAccent, kBg);
    int tw = M5Cardputer.Display.textWidth(line);
    M5Cardputer.Display.setCursor((kScreenW - tw) / 2, 56);
    M5Cardputer.Display.print(line);
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

    // Mic.begin() ran M5Unified's _microphone_enabled_cb_cardputer_adv
    // which pinned ES8311 reg 0x14 = 0x10 (PGA min). Override now.
    if (M5Cardputer.In_I2C.writeRegister8(kEs8311Addr, kAdcReg14, kAdcReg14Val,
                                          400000)) {
        Serial.printf("[record] es8311 pga: reg 0x%02X = 0x%02X (42 dB max)\n",
                      kAdcReg14, kAdcReg14Val);
    } else {
        Serial.println("[record] WARN es8311 pga write failed; using digital gain only");
    }

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

    wav::WavWriter writer;
    if (!writer.begin(kPath, kSampleRate, 1, 16)) {
        Serial.println("[record] wav open failed");
        M5Cardputer.Mic.end();
        drawError("sd write failed", "could not open .recording.wav");
        return false;
    }

    M5Canvas body(&M5Cardputer.Display);
    body.setColorDepth(16);
    int bodyH = kScreenH - kStatusH - kHintH;
    bool canvasOk = body.createSprite(kScreenW, bodyH);
    if (canvasOk) {
        body.setFont(&fonts::Font2);
        body.setTextSize(1);
    } else {
        Serial.printf("[record] WARN canvas alloc %dx%d failed; direct draw fallback\n",
                      kScreenW, bodyH);
    }

    drawStaticChrome();
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

        if (canvasOk && millis() - lastRender >= 33) {
            lastRender = millis();
            bool warn  = elapsed >= kWarnMs;

            body.fillScreen(kBg);
            body.drawLine(0, 0, kScreenW, 0, kDivider);

            // Blink dot, 2 Hz normally, 4 Hz in warn window
            uint32_t halfPeriod = warn ? 125 : 250;
            bool     dotOn      = ((millis() / halfPeriod) & 1) == 0;
            int      dotX       = kPadX + 8;
            int      dotY       = 16;
            if (dotOn) body.fillCircle(dotX, dotY, 6, kRed);
            else       body.drawCircle(dotX, dotY, 6, kRed);

            body.setFont(&fonts::Font2);
            body.setTextSize(1);
            body.setTextColor(warn ? kVUWarn : kIdle, kBg);
            body.setCursor(dotX + 14, 10);
            body.print(warn ? "REC . warn" : "recording");

            // Big elapsed counter (Font2 textSize 3 -> ~24 px tall)
            uint32_t secs = elapsed / 1000;
            char tbuf[8];
            snprintf(tbuf, sizeof(tbuf), "%02u:%02u",
                     (unsigned)(secs / 60), (unsigned)(secs % 60));
            body.setTextSize(3);
            int tw = body.textWidth(tbuf);
            body.setTextColor(warn ? kVUWarn : kAccent, kBg);
            body.setCursor((kScreenW - tw) / 2, 32);
            body.print(tbuf);
            body.setTextSize(1);

            // VU meter
            int vuX = kPadX + 4;
            int vuY = 76;
            int vuH = 10;
            int vuW = kScreenW - vuX - kPadX - 4;
            body.drawRect(vuX - 1, vuY - 1, vuW + 2, vuH + 2, kDim);
            int fill = (int)((int32_t)peakHi * vuW / INT16_MAX);
            if (fill < 0) fill = 0;
            if (fill > vuW) fill = vuW;
            uint16_t fc = kVU;
            if (peakHi > (INT16_MAX * 7) / 8)   fc = kVUWarn;
            if (peakHi > (INT16_MAX * 19) / 20) fc = kVUClip;
            if (fill > 0) body.fillRect(vuX, vuY, fill, vuH, fc);

            body.pushSprite(0, kStatusH);
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
