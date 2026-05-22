#include "sd_config.h"
#include <SD.h>
#include <SPI.h>

namespace {

// M5Cardputer / Cardputer ADV SD pinout (from M5 docs / Arduino example).
constexpr int kSdSck  = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs   = 12;

String readFileWhole(const char* path) {
    if (!SD.exists(path)) return String();
    File f = SD.open(path, FILE_READ);
    if (!f) return String();
    String s;
    s.reserve(f.size() + 1);
    while (f.available()) s += (char)f.read();
    f.close();
    return s;
}

String trimWS(const String& s) {
    int start = 0, end = s.length();
    while (start < end && isspace((unsigned char)s.charAt(start))) start++;
    while (end > start && isspace((unsigned char)s.charAt(end - 1))) end--;
    return s.substring(start, end);
}

} // namespace

namespace sdcfg {

bool begin() {
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    return SD.begin(kSdCs, SPI, 25000000);
}

String loadOpenRouterKey() {
    String raw = readFileWhole("/CardputerLLM/openrouter.txt");
    // Take just the first non-blank line, in case the file has trailing junk.
    int nl = raw.indexOf('\n');
    String first = (nl >= 0) ? raw.substring(0, nl) : raw;
    return trimWS(first);
}

std::vector<WiFiCred> loadWiFi() {
    std::vector<WiFiCred> out;
    String raw = readFileWhole("/CardputerLLM/wifi.txt");
    if (raw.length() == 0) return out;

    std::vector<String> lines;
    int i = 0;
    while (i < (int)raw.length()) {
        int e = raw.indexOf('\n', i);
        if (e < 0) e = raw.length();
        String line = trimWS(raw.substring(i, e));
        if (line.length() > 0 && line.charAt(0) != '#') {
            lines.push_back(line);
        }
        i = e + 1;
    }
    for (size_t j = 0; j + 1 < lines.size(); j += 2) {
        out.push_back({lines[j], lines[j + 1]});
    }
    return out;
}

String loadSystemPrompt() {
    String raw = readFileWhole("/CardputerLLM/system.txt");
    return trimWS(raw);
}

void ensureChatsDir() {
    if (!SD.exists("/CardputerLLM")) SD.mkdir("/CardputerLLM");
    if (!SD.exists("/CardputerLLM/chats")) SD.mkdir("/CardputerLLM/chats");
}

bool saveApiKey(const String& key) {
    if (!SD.exists("/CardputerLLM")) SD.mkdir("/CardputerLLM");
    // FILE_WRITE truncates an existing file in Arduino's SD library.
    File f = SD.open("/CardputerLLM/openrouter.txt", FILE_WRITE);
    if (!f) return false;
    f.println(key);
    f.close();
    return true;
}

bool appendWiFiCred(const String& ssid, const String& password) {
    if (!SD.exists("/CardputerLLM")) SD.mkdir("/CardputerLLM");
    File f = SD.open("/CardputerLLM/wifi.txt", FILE_APPEND);
    if (!f) return false;
    // Defensive blank line so we don't accidentally fuse with a prior
    // non-newline-terminated tail.
    f.println();
    f.println(ssid);
    f.println(password);
    f.close();
    return true;
}

} // namespace sdcfg
