#include "wifi_setup.h"
#include "../ui/boot_ui.h"
#include "../storage/sd_config.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <algorithm>

namespace {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

constexpr uint16_t kSel    = 0xFD60;
constexpr uint16_t kIdle   = 0xEF7D;
constexpr uint16_t kDim    = 0x6B4D;
constexpr uint16_t kFaint  = 0x2104;
constexpr uint16_t kBad    = 0xF884;
constexpr uint16_t kGood   = 0x4FCA;

// Body region between section header and hint bar
constexpr int kBodyTop    = 18;
constexpr int kBodyBottom = 135 - 22;

void drawRssiBars(int x, int yMid, int rssi, uint16_t color, uint16_t dim) {
    // 4 stair-step bars, brightest if rssi >= threshold[i]
    int bars = 0;
    if (rssi > -85) bars = 1;
    if (rssi > -75) bars = 2;
    if (rssi > -65) bars = 3;
    if (rssi > -55) bars = 4;
    for (int b = 0; b < 4; b++) {
        int h = 2 + b * 2;
        uint16_t c = (b < bars) ? color : dim;
        M5Cardputer.Display.fillRect(x + b * 3, yMid - h / 2, 2, h, c);
    }
}

void drawNetList(const std::vector<wifi_setup::ScanResult>& nets,
                 int sel, int scrollTop) {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . select");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);
    int lh = M5Cardputer.Display.fontHeight() + 3;
    int y0 = kBodyTop + 2;
    int yMax = kBodyBottom - lh;
    int vis = (yMax - y0) / lh;
    int end = std::min<int>(scrollTop + vis, (int)nets.size());

    for (int i = scrollTop; i < end; i++) {
        int y = y0 + (i - scrollTop) * lh;
        bool isSel = (i == sel);

        // Selection bar (3px wide, full row height) on the left
        if (isSel) M5Cardputer.Display.fillRect(4, y + 1, 3, lh - 4, kSel);

        // SSID
        M5Cardputer.Display.setTextColor(isSel ? kSel : kIdle, TFT_BLACK);
        M5Cardputer.Display.setCursor(12, y);
        String s = nets[i].ssid;
        // Reserve space on right for: lock (10px) + bars (12px) + padding
        const int rightReserve = 30;
        int maxPx = kScreenW - 12 - rightReserve;
        while (M5Cardputer.Display.textWidth(s.c_str()) > maxPx
               && s.length() > 1) {
            s.remove(s.length() - 1);
        }
        if (s.length() < nets[i].ssid.length()) s += ".";
        M5Cardputer.Display.print(s);

        // Lock glyph
        if (nets[i].secured) {
            M5Cardputer.Display.fillRect(kScreenW - 26, y + 4, 6, 6, isSel ? kSel : kDim);
        }
        // RSSI bars
        drawRssiBars(kScreenW - 16, y + lh / 2, nets[i].rssi,
                     isSel ? kSel : kIdle, kFaint);
    }

    // Scroll markers
    if (scrollTop > 0) {
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(kScreenW - 8, y0);
        M5Cardputer.Display.print("^");
    }
    if (end < (int)nets.size()) {
        M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
        M5Cardputer.Display.setCursor(kScreenW - 8, yMax);
        M5Cardputer.Display.print("v");
    }

    boot_ui::hintBar(",/.  navigate    enter  join",
                     "r    rescan      del    back");
}

void drawPasswordEntry(const String& ssid, const String& pw, bool secured) {
    boot_ui::clear();
    boot_ui::sectionHeader(secured ? "wifi . password" : "wifi . connect");

    M5Cardputer.Display.setFont(&fonts::Font2);
    M5Cardputer.Display.setTextSize(1);

    // "for: <ssid>" subtitle
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setCursor(8, 22);
    M5Cardputer.Display.print("network");
    M5Cardputer.Display.setTextColor(kSel, TFT_BLACK);
    M5Cardputer.Display.setCursor(64, 22);
    M5Cardputer.Display.print(ssid);

    // Frame around the input area
    int boxY = 48;
    int boxH = 24;
    M5Cardputer.Display.drawRect(4, boxY, kScreenW - 8, boxH, kDim);
    // Inner top hairline accent
    M5Cardputer.Display.drawFastHLine(6, boxY + 1, kScreenW - 12, kFaint);

    // The typed text with trailing cursor block
    M5Cardputer.Display.setTextColor(kIdle, TFT_BLACK);
    String shown = pw;
    int maxPx = kScreenW - 16;
    while (M5Cardputer.Display.textWidth(shown.c_str()) > maxPx && shown.length() > 0) {
        shown.remove(0, 1);
    }
    M5Cardputer.Display.setCursor(8, boxY + 5);
    M5Cardputer.Display.print(shown);
    int cx = M5Cardputer.Display.getCursorX();
    int ch = M5Cardputer.Display.fontHeight();
    M5Cardputer.Display.fillRect(cx + 1, boxY + 5, 6, ch, kSel);

    // Char-count hint
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setFont(&fonts::Font0);
    String cc = String(pw.length()) + " char";
    if (pw.length() != 1) cc += "s";
    M5Cardputer.Display.setCursor(8, 80);
    M5Cardputer.Display.print(cc);
    M5Cardputer.Display.setFont(&fonts::Font2);

    boot_ui::hintBar("type the password",
                     "enter  connect   del  erase / back");
}

void drawScanning() {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . scanning");
    boot_ui::centerText("looking for networks", 60, kIdle);
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setFont(&fonts::Font0);
    M5Cardputer.Display.setCursor(60, 84);
    M5Cardputer.Display.print("(2-3 seconds)");
    M5Cardputer.Display.setFont(&fonts::Font2);
    boot_ui::hintBar("");
}

void drawConnecting(const String& ssid) {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . connecting");
    boot_ui::centerText(ssid, 52, kSel);
    M5Cardputer.Display.setTextColor(kDim, TFT_BLACK);
    M5Cardputer.Display.setFont(&fonts::Font0);
    boot_ui::centerText("up to 15 seconds", 76, kDim);
    M5Cardputer.Display.setFont(&fonts::Font2);
    boot_ui::hintBar("");
}

void drawConnected(const String& ssid, const String& ip) {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . connected", kGood);
    boot_ui::centerText(ssid, 46, kSel);
    boot_ui::centerText(ip, 70, kIdle);
    boot_ui::hintBar("saved to /CardputerLLM/wifi.txt");
}

void drawConnectFailed(const String& ssid) {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . failed", kBad);
    boot_ui::centerText(ssid, 46, kBad);
    boot_ui::centerText("wrong password?", 70, kIdle);
    boot_ui::hintBar("any key to retry");
}

void drawNoNetworks() {
    boot_ui::clear();
    boot_ui::sectionHeader("wifi . scan", kBad);
    boot_ui::centerText("no networks found", 56, kBad);
    boot_ui::hintBar("any key to rescan    del  back");
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

// Returns: >=0 selection index, -1 cancel, -2 rescan
int selectFromList(const std::vector<wifi_setup::ScanResult>& nets, bool allowCancel) {
    int sel = 0, scroll = 0;
    bool dirty = true;
    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;

    M5Cardputer.Display.setFont(&fonts::Font2);
    int lh = M5Cardputer.Display.fontHeight() + 3;
    int vis = (kBodyBottom - kBodyTop - 4) / lh;

    while (true) {
        if (sel < scroll)        scroll = sel;
        if (sel >= scroll + vis) scroll = sel - vis + 1;

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
        if (s.enter && !prevEnter) return sel;
        if (s.del && !prevDel && allowCancel) return -1;

        prevWord  = s.word;
        prevDel   = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

bool inputPassword(const String& ssid, String& out, bool secured) {
    String pw;
    bool dirty = true;
    std::vector<char> prevWord;
    bool prevDel = false, prevEnter = false;
    uint32_t delFirst = 0, delLast = 0;
    bool delActive = false;

    while (true) {
        if (dirty) { drawPasswordEntry(ssid, pw, secured); dirty = false; }

        M5Cardputer.update();
        auto& s = M5Cardputer.Keyboard.keysState();

        for (char c : s.word) {
            bool was = false;
            for (char p : prevWord) if (p == c) { was = true; break; }
            if (was) continue;
            pw += c;
            dirty = true;
        }
        if (s.del && !prevDel) {
            if (pw.length() > 0) { pw.remove(pw.length() - 1); dirty = true; }
            else                  return false;
            delFirst = delLast = millis();
            delActive = true;
        } else if (!s.del) {
            delActive = false;
        } else if (delActive) {
            uint32_t now = millis();
            if (now - delFirst >= 400 && now - delLast >= 50) {
                if (pw.length() > 0) { pw.remove(pw.length() - 1); dirty = true; }
                delLast = now;
            }
        }
        if (s.enter && !prevEnter && pw.length() > 0) {
            out = pw;
            return true;
        }
        prevWord = s.word;
        prevDel = s.del;
        prevEnter = s.enter;
        delay(15);
    }
}

} // namespace

namespace wifi_setup {

std::vector<ScanResult> scanNow(bool showUI) {
    if (showUI) drawScanning();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);
    std::vector<ScanResult> result;
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            ScanResult e;
            e.ssid    = WiFi.SSID(i);
            e.rssi    = WiFi.RSSI(i);
            e.secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            if (e.ssid.length() > 0) result.push_back(e);
        }
    }
    WiFi.scanDelete();
    std::sort(result.begin(), result.end(),
              [](const ScanResult& a, const ScanResult& b){ return a.rssi > b.rssi; });
    // Dedupe by SSID, keep strongest
    std::vector<ScanResult> uniq;
    for (auto& e : result) {
        bool seen = false;
        for (auto& u : uniq) if (u.ssid == e.ssid) { seen = true; break; }
        if (!seen) uniq.push_back(e);
    }
    return uniq;
}

std::vector<WiFiCred> filterByVisibility(
    const std::vector<WiFiCred>& saved,
    const std::vector<ScanResult>& visible) {
    std::vector<std::pair<WiFiCred, int>> pairs;
    for (auto& c : saved) {
        for (auto& v : visible) {
            if (v.ssid == c.ssid) {
                pairs.push_back({c, v.rssi});
                break;
            }
        }
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<WiFiCred,int>& a, const std::pair<WiFiCred,int>& b){
                  return a.second > b.second;
              });
    std::vector<WiFiCred> result;
    result.reserve(pairs.size());
    for (auto& p : pairs) result.push_back(p.first);
    return result;
}

bool run(bool allowCancel) {
    while (true) {
        auto nets = scanNow(true);

        if (nets.empty()) {
            drawNoNetworks();
            bool prevDel = false;
            while (true) {
                M5Cardputer.update();
                auto& s = M5Cardputer.Keyboard.keysState();
                if (s.del && !prevDel && allowCancel) return false;
                if (!s.word.empty()) {
                    while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(10); }
                    break;
                }
                prevDel = s.del;
                delay(20);
            }
            continue;
        }

        int sel = selectFromList(nets, allowCancel);
        if (sel == -1) return false;
        if (sel == -2) continue;

        String pw;
        if (nets[sel].secured) {
            if (!inputPassword(nets[sel].ssid, pw, true)) continue;
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
