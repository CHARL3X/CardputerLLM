#include "wifi_setup.h"
#include "../ui/boot_ui.h"
#include "../storage/sd_config.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kHeaderH = 14;
constexpr int kFooterH = 14;

constexpr uint16_t kSel  = 0xFD60;
constexpr uint16_t kIdle = 0xEF7D;
constexpr uint16_t kDim  = 0x6B4D;
constexpr uint16_t kBad  = 0xF800;

struct Net {
    String ssid;
    int    rssi;
    bool   secured;
};

void drawNetList(const std::vector<Net>& nets, int sel, int scrollTop) {
    boot_ui::clear();
    boot_ui::header("select network");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int lh = M5Cardputer.Display.fontHeight() + 2;
    int y0 = kHeaderH + 4;
    int yMax = kScreenH - kFooterH - 2;
    int vis = (yMax - y0) / lh;

    int end = scrollTop + vis;
    if (end > (int)nets.size()) end = nets.size();

    for (int i = scrollTop; i < end; i++) {
        int y = y0 + (i - scrollTop) * lh;
        bool isSel = (i == sel);
        uint16_t color = isSel ? kSel : kIdle;
        M5Cardputer.Display.setTextColor(color, TFT_BLACK);
        M5Cardputer.Display.setCursor(4, y);
        M5Cardputer.Display.print(isSel ? "> " : "  ");
        // Truncate long SSIDs so the lock + rssi can fit
        String s = nets[i].ssid;
        if (s.length() > 18) { s = s.substring(0, 17); s += "."; }
        M5Cardputer.Display.print(s);
        // Right-side suffix: lock + rssi
        String suffix;
        if (nets[i].secured) suffix += " *";
        suffix += " ";
        suffix += String(nets[i].rssi);
        int sw = M5Cardputer.Display.textWidth(suffix.c_str());
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(kScreenW - 4 - sw, y);
        M5Cardputer.Display.print(suffix);
    }

    // Scroll hints
    if (scrollTop > 0) {
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(kScreenW - 10, y0);
        M5Cardputer.Display.print("^");
    }
    if (end < (int)nets.size()) {
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(kScreenW - 10, yMax - lh);
        M5Cardputer.Display.print("v");
    }

    boot_ui::footer(",/.  enter=join  r=rescan  del=back");
}

void drawPasswordEntry(const String& ssid, const String& pw) {
    boot_ui::clear();
    boot_ui::header(ssid);

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setCursor(4, kHeaderH + 6);
    M5Cardputer.Display.print("password:");

    M5Cardputer.Display.setTextColor(kIdle, TFT_BLACK);
    int y = kHeaderH + 26;
    // Show what's been typed so the user can verify; this is a personal device.
    String shown = pw + "_";
    int maxPx = kScreenW - 8;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx && shown.length() > 1) {
        shown.remove(0, 1);
    }
    M5Cardputer.Display.setCursor(4, y);
    M5Cardputer.Display.print(shown);

    boot_ui::footer("enter=connect  del=erase/back");
}

void drawScanning() {
    boot_ui::clear();
    boot_ui::header("scanning");
    boot_ui::centerText("looking for networks...", 60, kIdle);
    boot_ui::footer("");
}

void drawConnecting(const String& ssid) {
    boot_ui::clear();
    boot_ui::header("connecting");
    boot_ui::centerText(ssid, 56, kSel);
    boot_ui::centerText("(up to 15s)", 80, kDim);
    boot_ui::footer("");
}

void drawConnected(const String& ssid, const String& ip) {
    boot_ui::clear();
    boot_ui::header("connected", 0x07E0); // green-ish
    boot_ui::centerText(ssid, 46, kSel);
    boot_ui::centerText(ip, 70, kIdle);
    boot_ui::footer("saved to wifi.txt");
}

void drawConnectFailed(const String& ssid) {
    boot_ui::clear();
    boot_ui::header("failed", 0x7800); // dark red
    boot_ui::centerText(ssid, 46, kBad);
    boot_ui::centerText("wrong password?", 70, kIdle);
    boot_ui::footer("any key to continue");
}

std::vector<Net> doScan() {
    drawScanning();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);
    std::vector<Net> nets;
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            Net e;
            e.ssid    = WiFi.SSID(i);
            e.rssi    = WiFi.RSSI(i);
            e.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            if (e.ssid.length() > 0) nets.push_back(e);
        }
    }
    WiFi.scanDelete();
    std::sort(nets.begin(), nets.end(),
              [](const Net& a, const Net& b){ return a.rssi > b.rssi; });
    // Dedupe by SSID, keep strongest
    std::vector<Net> uniq;
    for (auto& e : nets) {
        bool seen = false;
        for (auto& u : uniq) if (u.ssid == e.ssid) { seen = true; break; }
        if (!seen) uniq.push_back(e);
    }
    return uniq;
}

bool tryConnect(const String& ssid, const String& pw) {
    drawConnecting(ssid);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_STA);
    if (pw.length() > 0) WiFi.begin(ssid.c_str(), pw.c_str());
    else                 WiFi.begin(ssid.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Returns: >=0 = selection index, -1 = cancel, -2 = rescan
int selectFromList(const std::vector<Net>& nets, bool allowCancel) {
    int sel    = 0;
    int scroll = 0;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;

    M5Cardputer.Display.setFont(&fonts::Font2);
    int lh  = M5Cardputer.Display.fontHeight() + 2;
    int vis = (kScreenH - kHeaderH - kFooterH - 4) / lh;

    while (true) {
        // Keep selection in view
        if (sel < scroll)            scroll = sel;
        if (sel >= scroll + vis)     scroll = sel - vis + 1;

        if (dirty) { drawNetList(nets, sel, scroll); dirty = false; }

        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        for (char c : s.word) {
            bool was = false;
            for (char p : prevWord) if (p == c) { was = true; break; }
            if (was) continue;
            if (c == ',' || c == ';') {
                if (sel > 0) { sel--; dirty = true; }
            } else if (c == '.' || c == '/') {
                if (sel + 1 < (int)nets.size()) { sel++; dirty = true; }
            } else if (c == 'r' || c == 'R') {
                return -2;
            }
        }
        if (s.enter && !prevEnter)  return sel;
        if (s.del   && !prevDel && allowCancel) return -1;

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

// Returns true with pw populated, false if user backspaced out of empty input.
bool inputPassword(const String& ssid, String& out) {
    String pw;
    bool dirty = true;

    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;
    uint32_t delFirst = 0, delLast = 0;
    bool delActive = false;

    while (true) {
        if (dirty) { drawPasswordEntry(ssid, pw); dirty = false; }

        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        // newly-pressed chars
        for (char c : s.word) {
            bool was = false;
            for (char p : prevWord) if (p == c) { was = true; break; }
            if (was) continue;
            pw += c;
            dirty = true;
        }
        // Backspace edge + hold-repeat
        if (s.del && !prevDel) {
            if (pw.length() > 0) { pw.remove(pw.length()-1); dirty = true; }
            else                  return false;  // empty + del = cancel back to list
            delFirst = delLast = millis();
            delActive = true;
        } else if (!s.del) {
            delActive = false;
        } else if (delActive) {
            uint32_t now = millis();
            if (now - delFirst >= 400 && now - delLast >= 50) {
                if (pw.length() > 0) { pw.remove(pw.length()-1); dirty = true; }
                delLast = now;
            }
        }

        if (s.enter && !prevEnter && pw.length() > 0) {
            out = pw;
            return true;
        }

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

} // namespace

namespace wifi_setup {

bool run(bool allowCancel) {
    while (true) {
        auto nets = doScan();

        if (nets.empty()) {
            boot_ui::clear();
            boot_ui::header("no networks", 0x7800);
            boot_ui::centerText("(none found)", 56, kBad);
            boot_ui::footer("any key to rescan, del=back");
            // Wait for any key OR backspace for cancel
            std::vector<char> prevWord; bool prevDel = false;
            while (true) {
                M5Cardputer.update();
                auto& s = M5Cardputer.Keyboard.keysState();
                if (s.del && !prevDel && allowCancel) return false;
                bool pressed = !s.word.empty();
                prevDel = s.del;
                prevWord = s.word;
                if (pressed) {
                    while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
                    break;
                }
                delay(20);
            }
            continue;
        }

        int sel = selectFromList(nets, allowCancel);
        if (sel == -1) return false;
        if (sel == -2) continue;

        String pw;
        if (nets[sel].secured) {
            if (!inputPassword(nets[sel].ssid, pw)) continue;
        }

        if (tryConnect(nets[sel].ssid, pw)) {
            sdcfg::appendWiFiCred(nets[sel].ssid, pw);
            drawConnected(nets[sel].ssid, WiFi.localIP().toString());
            delay(1500);
            return true;
        }

        drawConnectFailed(nets[sel].ssid);
        boot_ui::waitForAnyKey();
    }
}

} // namespace wifi_setup
