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
<title>CardputerLLM - set api key</title>
<style>
 :root{color-scheme:dark}
 *{box-sizing:border-box}
 body{background:#100808;color:#ffe0a0;font-family:ui-monospace,Menlo,Consolas,monospace;padding:1.5em;max-width:560px;margin:0 auto;line-height:1.5}
 h1{color:#fd9a40;margin:0 0 .8em;font-weight:600;letter-spacing:.05em}
 p{margin:.4em 0}
 textarea{width:100%;background:#000;color:#ffe0a0;border:1px solid #6b3a14;font-family:inherit;padding:.7em;font-size:14px;resize:vertical;min-height:5em}
 button{background:#fd9a40;color:#100808;padding:.6em 1.4em;border:0;font-family:inherit;cursor:pointer;font-size:14px;margin-top:.6em;font-weight:600;letter-spacing:.04em}
 button:hover{background:#ffb060}
 small{color:#8a6d4a;display:block;margin-top:1.2em;font-size:.85em}
 .err{color:#ff8060;margin-top:.6em}
 a{color:#fd9a40}
</style></head>
<body>
<h1>CardputerLLM</h1>
<p>Paste your OpenRouter API key:</p>
<form method="POST" action="/key">
  <textarea name="key" rows="3" required placeholder="sk-or-v1-..." autofocus></textarea>
  <button type="submit">save</button>
</form>
<small>Key format: <code>sk-or-v1-...</code><br>Get one at <a href="https://openrouter.ai/keys" target="_blank">openrouter.ai/keys</a></small>
</body></html>
)HTML";

const char* DONE_HTML = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>saved</title>
<style>body{background:#100808;color:#ffe0a0;font-family:ui-monospace,Menlo,monospace;padding:2.5em;text-align:center;line-height:1.5}h1{color:#fd9a40}</style>
</head><body>
<h1>saved.</h1>
<p>The Cardputer is starting up.</p>
<p>You can close this tab.</p>
</body></html>
)HTML";

WebServer       g_server(80);
volatile bool   g_gotKey = false;
volatile bool   g_saveFailed = false;

void drawWaitingScreen() {
    String ip = WiFi.localIP().toString();
    boot_ui::clear();
    boot_ui::header("set api key");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kIdle, TFT_BLACK);

    M5Cardputer.Display.setCursor(4, 22);
    M5Cardputer.Display.print("visit on any device:");

    M5Cardputer.Display.setTextColor(kAccent, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    String url = String("http://") + ip;
    int w = M5Cardputer.Display.textWidth(url.c_str());
    M5Cardputer.Display.setCursor((240 - w) / 2, 48);
    M5Cardputer.Display.print(url);
    M5Cardputer.Display.setTextSize(1);

    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setCursor(4, 82);
    M5Cardputer.Display.print("paste your openrouter");
    M5Cardputer.Display.setCursor(4, 96);
    M5Cardputer.Display.print("key in the form there.");

    boot_ui::footer("waiting for submit...  del=cancel");
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
