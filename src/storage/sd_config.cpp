#include "sd_config.h"
#include <SD.h>
#include <SPI.h>

namespace {

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
    String raw = readFileWhole("/Cardputer/openrouter.txt");
    int nl = raw.indexOf('\n');
    String first = (nl >= 0) ? raw.substring(0, nl) : raw;
    return trimWS(first);
}

std::vector<WiFiCred> loadWiFi() {
    std::vector<WiFiCred> out;
    String raw = readFileWhole("/Cardputer/wifi.txt");
    if (raw.length() == 0) return out;

    std::vector<String> lines;
    int i = 0;
    while (i < (int)raw.length()) {
        int e = raw.indexOf('\n', i);
        if (e < 0) e = raw.length();
        String line = trimWS(raw.substring(i, e));
        if (line.length() > 0 && line.charAt(0) != '#') lines.push_back(line);
        i = e + 1;
    }
    for (size_t j = 0; j + 1 < lines.size(); j += 2) {
        out.push_back({lines[j], lines[j + 1]});
    }
    return out;
}

String loadSystemPrompt() {
    return trimWS(readFileWhole("/Cardputer/system.txt"));
}

void ensureDirs() {
    if (!SD.exists("/Cardputer"))        SD.mkdir("/Cardputer");
    if (!SD.exists("/Cardputer/chats"))  SD.mkdir("/Cardputer/chats");
    if (!SD.exists("/Cardputer/notes"))  SD.mkdir("/Cardputer/notes");
    if (!SD.exists("/Cardputer/snaps"))  SD.mkdir("/Cardputer/snaps");
}

bool saveApiKey(const String& key) {
    if (!SD.exists("/Cardputer")) SD.mkdir("/Cardputer");
    File f = SD.open("/Cardputer/openrouter.txt", FILE_WRITE);
    if (!f) return false;
    f.println(key);
    f.close();
    return true;
}

bool appendWiFiCred(const String& ssid, const String& password) {
    if (!SD.exists("/Cardputer")) SD.mkdir("/Cardputer");
    File f = SD.open("/Cardputer/wifi.txt", FILE_APPEND);
    if (!f) return false;
    f.println();
    f.println(ssid);
    f.println(password);
    f.close();
    return true;
}

int countNotes() {
    File dir = SD.open("/Cardputer/notes");
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }
    int n = 0;
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".md")) n++;
        }
        entry.close();
    }
    dir.close();
    return n;
}

} // namespace sdcfg
