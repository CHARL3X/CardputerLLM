#include "key_setup.h"
#include "../ui/boot_ui.h"
#include "../storage/sd_config.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WebServer.h>

namespace {

constexpr uint16_t kAccent = 0xFD60;
constexpr uint16_t kIdle   = 0xEF7D;
constexpr uint16_t kDim    = 0x6B4D;

const char* FORM_HTML = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CardputerLLM &middot; set api key</title>
<style>
 :root{color-scheme:dark}
 *{box-sizing:border-box}
 html,body{margin:0;padding:0;background:#0a0606;color:#ffe0a0;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;line-height:1.5}
 body{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1.2em}
 .frame{max-width:520px;width:100%;position:relative;padding:2em 1.6em}
 /* Corner brackets, cassette-futurism vibe */
 .frame::before,.frame::after,.frame > .br::before,.frame > .br::after{content:"";position:absolute;width:14px;height:14px;border:1px solid #fd9a40}
 .frame::before{top:0;left:0;border-right:0;border-bottom:0}
 .frame::after{top:0;right:0;border-left:0;border-bottom:0}
 .frame > .br::before{bottom:0;left:0;border-right:0;border-top:0}
 .frame > .br::after{bottom:0;right:0;border-left:0;border-top:0}
 .eyebrow{font-size:.72em;color:#8a6d4a;letter-spacing:.18em;text-transform:uppercase;margin-bottom:.4em}
 h1{color:#fd9a40;margin:0 0 .9em;font-weight:600;font-size:1.6em;letter-spacing:.04em}
 .rule{height:1px;background:linear-gradient(90deg,#fd9a40,transparent);margin:.4em 0 1.2em}
 p{margin:.4em 0;color:#ddc999}
 label{display:block;margin:1.2em 0 .35em;font-size:.85em;color:#fd9a40;letter-spacing:.05em}
 textarea{width:100%;background:#000;color:#ffe0a0;border:1px solid #5a3416;font-family:inherit;padding:.75em;font-size:14px;resize:vertical;min-height:5.5em;outline:none}
 textarea:focus{border-color:#fd9a40}
 .row{margin-top:1em;display:flex;align-items:center;gap:1em;flex-wrap:wrap}
 button{background:#fd9a40;color:#0a0606;padding:.65em 1.6em;border:0;font-family:inherit;cursor:pointer;font-size:.95em;font-weight:700;letter-spacing:.08em;text-transform:uppercase;transition:transform .08s ease,background .12s ease}
 button:hover{background:#ffb060;transform:translateY(-1px)}
 button:active{transform:translateY(0)}
 .meta{font-size:.78em;color:#8a6d4a}
 .meta strong{color:#fd9a40;font-weight:normal}
 small{color:#8a6d4a;display:block;margin-top:1.6em;font-size:.78em;line-height:1.7}
 a{color:#fd9a40;text-decoration:none;border-bottom:1px dotted #fd9a40}
 a:hover{color:#ffb060}
 code{color:#fd9a40}
</style></head>
<body>
<div class="frame">
  <div class="br"></div>
  <div class="eyebrow">cardputer &middot; onboarding</div>
  <h1>set api key</h1>
  <div class="rule"></div>
  <p>Paste your OpenRouter API key. The device saves it to the SD card and continues to chat.</p>
  <form method="POST" action="/key">
    <label for="key">openrouter key</label>
    <textarea id="key" name="key" rows="3" required placeholder="sk-or-v1-..." autofocus></textarea>
    <div class="row">
      <button type="submit">save &amp; continue</button>
      <span class="meta"><strong>format</strong> <code>sk-or-v1-...</code></span>
    </div>
  </form>
  <small>
    Don't have a key yet? Get one at <a href="https://openrouter.ai/keys" target="_blank" rel="noopener">openrouter.ai/keys</a>.<br>
    The device runs this form only while no key is present. After saving, this page goes away.
  </small>
</div>
</body></html>
)HTML";

const char* DONE_HTML = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>saved</title>
<style>
 html,body{margin:0;padding:0;background:#0a0606;color:#ffe0a0;font-family:ui-monospace,Menlo,monospace;min-height:100vh;display:flex;align-items:center;justify-content:center;line-height:1.6}
 .frame{max-width:440px;text-align:center;padding:2.4em 1.6em;position:relative}
 .frame::before,.frame::after,.br::before,.br::after{content:"";position:absolute;width:14px;height:14px;border:1px solid #4fca80}
 .frame::before{top:0;left:0;border-right:0;border-bottom:0}
 .frame::after{top:0;right:0;border-left:0;border-bottom:0}
 .br::before{bottom:0;left:0;border-right:0;border-top:0}
 .br::after{bottom:0;right:0;border-left:0;border-top:0}
 .eyebrow{font-size:.72em;color:#8a6d4a;letter-spacing:.18em;text-transform:uppercase;margin-bottom:.4em}
 h1{color:#4fca80;margin:0;font-weight:600;font-size:1.8em;letter-spacing:.04em}
 .rule{height:1px;background:linear-gradient(90deg,transparent,#4fca80,transparent);margin:1em 0}
 p{margin:.5em 0;color:#ddc999}
</style>
</head><body>
<div class="frame">
  <div class="br"></div>
  <div class="eyebrow">cardputer &middot; onboarding</div>
  <h1>saved.</h1>
  <div class="rule"></div>
  <p>Key written to <code>/CardputerLLM/openrouter.txt</code>.</p>
  <p>Cardputer is continuing to chat.</p>
  <p style="margin-top:1.4em;font-size:.85em;color:#8a6d4a">You can close this tab.</p>
</div>
</body></html>
)HTML";

WebServer       g_server(80);
volatile bool   g_gotKey = false;
volatile bool   g_saveFailed = false;

void drawWaitingScreen() {
    String ip = WiFi.localIP().toString();
    boot_ui::clear();
    boot_ui::sectionHeader("api key . set");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setCursor(8, 24);
    M5Cardputer.Display.print("visit on any device");

    // Prominent URL with a thin frame for emphasis
    String url = String("http://") + ip;
    M5Cardputer.Display.setTextColor(kAccent, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    int w = M5Cardputer.Display.textWidth(url.c_str());
    int urlY = 44;
    int padding = 6;
    int boxX = (240 - w) / 2 - padding;
    int boxY = urlY - 2;
    int boxW = w + padding * 2;
    int boxH = 22;
    // Corner brackets matching the splash style
    constexpr int brk = 4;
    M5Cardputer.Display.drawLine(boxX, boxY, boxX + brk, boxY, kAccent);
    M5Cardputer.Display.drawLine(boxX, boxY, boxX, boxY + brk, kAccent);
    M5Cardputer.Display.drawLine(boxX + boxW - brk, boxY, boxX + boxW, boxY, kAccent);
    M5Cardputer.Display.drawLine(boxX + boxW, boxY, boxX + boxW, boxY + brk, kAccent);
    M5Cardputer.Display.drawLine(boxX, boxY + boxH - brk, boxX, boxY + boxH, kAccent);
    M5Cardputer.Display.drawLine(boxX, boxY + boxH, boxX + brk, boxY + boxH, kAccent);
    M5Cardputer.Display.drawLine(boxX + boxW - brk, boxY + boxH, boxX + boxW, boxY + boxH, kAccent);
    M5Cardputer.Display.drawLine(boxX + boxW, boxY + boxH - brk, boxX + boxW, boxY + boxH, kAccent);

    M5Cardputer.Display.setCursor((240 - w) / 2, urlY);
    M5Cardputer.Display.print(url);
    M5Cardputer.Display.setTextSize(1);

    M5Cardputer.Display.setTextColor(kIdle, TFT_BLACK);
    M5Cardputer.Display.setCursor(8, 80);
    M5Cardputer.Display.print("paste your key in");
    M5Cardputer.Display.setCursor(8, 94);
    M5Cardputer.Display.print("the form there.");

    boot_ui::hintBar("waiting for browser submit...",
                     "del  cancel and return");
}

void drawDone() {
    boot_ui::clear();
    boot_ui::header("got it!", 0x07E0);
    boot_ui::centerText("key saved.", 50, kAccent);
    boot_ui::centerText("continuing...", 78, kIdle);
    boot_ui::footer("");
}

} // namespace

namespace key_setup {

bool run(bool allowCancel) {
    g_gotKey      = false;
    g_saveFailed  = false;

    drawWaitingScreen();

    g_server.on("/", HTTP_GET, [](){
        g_server.send(200, "text/html", FORM_HTML);
    });
    g_server.on("/key", HTTP_POST, [](){
        if (!g_server.hasArg("key")) {
            g_server.send(400, "text/plain", "missing 'key' field");
            return;
        }
        String k = g_server.arg("key");
        k.trim();
        if (!k.startsWith("sk-or-")) {
            g_server.send(400, "text/plain",
                          "invalid format: expected 'sk-or-...'");
            return;
        }
        if (!sdcfg::saveApiKey(k)) {
            g_saveFailed = true;
            g_server.send(500, "text/plain", "sd write failed");
            return;
        }
        g_server.sendHeader("Location", "/done");
        g_server.send(303, "text/plain", "saved");
        g_gotKey = true;
    });
    g_server.on("/done", HTTP_GET, [](){
        g_server.send(200, "text/html", DONE_HTML);
    });
    g_server.onNotFound([](){
        g_server.sendHeader("Location", "/");
        g_server.send(303, "text/plain", "redirect");
    });
    g_server.begin();
    Serial.printf("[key-setup] http://%s/\n", WiFi.localIP().toString().c_str());

    bool prevDel = false;
    bool cancelled = false;

    while (!g_gotKey && !g_saveFailed) {
        g_server.handleClient();

        if (allowCancel) {
            M5Cardputer.update();
            auto& s = M5Cardputer.Keyboard.keysState();
            if (s.del && !prevDel) {
                cancelled = true;
                break;
            }
            prevDel = s.del;
        }
        delay(8);
    }

    // Let any in-flight response flush before tearing down.
    uint32_t flushUntil = millis() + 400;
    while (millis() < flushUntil) { g_server.handleClient(); delay(5); }
    g_server.stop();

    if (cancelled) return false;
    if (g_saveFailed) {
        boot_ui::clear();
        boot_ui::header("sd write failed", 0x7800);
        boot_ui::centerText("could not save key", 56, 0xF800);
        boot_ui::footer("any key to retry");
        boot_ui::waitForAnyKey();
        return run(allowCancel); // retry
    }

    drawDone();
    delay(1200);
    return true;
}

} // namespace key_setup
