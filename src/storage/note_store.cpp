#include "note_store.h"
#include <SD.h>
#include <time.h>
#include <algorithm>

namespace notestore {

namespace {

constexpr const char* kDir = "/Cardputer/notes";

void ensureDir() {
    if (!SD.exists("/Cardputer"))         SD.mkdir("/Cardputer");
    if (!SD.exists("/Cardputer/notes"))   SD.mkdir("/Cardputer/notes");
}

// Convert ISO createdUtc into the compact stem used in filenames:
// "2026-05-23T14:32:11Z" -> "20260523T143211Z". Pass-through for the
// "boot-NNNN" fallback.
String stemFromIso(const String& iso) {
    if (iso.startsWith("boot-")) return iso;
    String s;
    s.reserve(iso.length());
    for (size_t i = 0; i < iso.length(); i++) {
        char c = iso.charAt(i);
        if (c != '-' && c != ':') s += c;
    }
    return s;
}

bool ciStartsWith(const String& s, const char* prefix) {
    size_t plen = strlen(prefix);
    if (s.length() < plen) return false;
    for (size_t i = 0; i < plen; i++) {
        char a = s.charAt(i);
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

} // namespace

String slugify(const String& title) {
    String s;
    s.reserve(32);
    bool lastDash = false;
    for (size_t i = 0; i < title.length() && s.length() < 30; i++) {
        char c = title.charAt(i);
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) {
            s += c;
            lastDash = false;
        } else if (!lastDash && s.length() > 0) {
            s += '-';
            lastDash = true;
        }
    }
    while (s.length() > 0 && s.charAt(s.length() - 1) == '-') {
        s.remove(s.length() - 1);
    }
    if (s.length() == 0) s = "note";
    return s;
}

String utcIsoNow() {
    time_t now = time(nullptr);
    if (now < 1577836800) {
        char b[32];
        snprintf(b, sizeof(b), "boot-%lu", (unsigned long)millis());
        return String(b);
    }
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char b[32];
    snprintf(b, sizeof(b), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return String(b);
}

String save(const NoteMeta& meta, const String& transcript) {
    ensureDir();
    String stem     = stemFromIso(meta.createdUtc);
    String slug     = slugify(meta.title);
    String filename = stem + "-" + slug + ".md";
    String path     = String(kDir) + "/" + filename;

    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        Serial.printf("[notestore] open fail %s\n", path.c_str());
        return String();
    }

    // YAML frontmatter
    f.print("---\n");
    f.print("created: ");          f.print(meta.createdUtc);        f.print("\n");
    f.print("title: ");            f.print(meta.title);             f.print("\n");
    f.print("duration_sec: ");     f.print(meta.durationSec);       f.print("\n");
    f.print("model_transcribe: "); f.print(meta.modelTranscribe);   f.print("\n");
    f.print("model_title: ");      f.print(meta.modelTitle);        f.print("\n");
    f.print("---\n\n");

    // Body: trim leading whitespace (Whisper prefixes a space; we
    // don't want it in the persisted note).
    int start = 0;
    while (start < (int)transcript.length()
           && (transcript.charAt(start) == ' ' || transcript.charAt(start) == '\n'
               || transcript.charAt(start) == '\t' || transcript.charAt(start) == '\r')) {
        start++;
    }
    if (start > 0) {
        size_t written = f.print(transcript.substring(start));
        (void)written;
    } else {
        f.print(transcript);
    }
    f.print("\n");
    f.close();
    Serial.printf("[notestore] wrote %s (%u-char body)\n",
                  path.c_str(),
                  (unsigned)(transcript.length() - start));
    return filename;
}

bool load(const String& filename, NoteMeta& outMeta, String& outBody) {
    String path = String(kDir) + "/" + filename;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;

    // Slurp the file. Notes are at most a few KB so this is fine.
    String raw;
    raw.reserve(f.size() + 1);
    while (f.available()) raw += (char)f.read();
    f.close();

    if (!raw.startsWith("---\n")) return false;
    int endIdx = raw.indexOf("\n---\n", 4);
    if (endIdx < 0) return false;

    String yaml = raw.substring(4, endIdx);
    String body = raw.substring(endIdx + 5);
    while (body.length() > 0 && body.charAt(0) == '\n') body.remove(0, 1);

    int i = 0;
    while (i < (int)yaml.length()) {
        int nl = yaml.indexOf('\n', i);
        if (nl < 0) nl = yaml.length();
        String line = yaml.substring(i, nl);
        int col = line.indexOf(':');
        if (col > 0) {
            String key = line.substring(0, col);
            String val = line.substring(col + 1);
            val.trim();
            if      (key == "created")          outMeta.createdUtc      = val;
            else if (key == "title")            outMeta.title           = val;
            else if (key == "duration_sec")     outMeta.durationSec     = (uint32_t)val.toInt();
            else if (key == "model_transcribe") outMeta.modelTranscribe = val;
            else if (key == "model_title")      outMeta.modelTitle      = val;
        }
        i = nl + 1;
    }
    outBody = body;
    return true;
}

std::vector<String> list() {
    std::vector<String> out;
    File dir = SD.open(kDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return out;
    }
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            // SD.openNextFile() may return full paths or basenames
            // depending on framework version -- normalize to basename.
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".md")) out.push_back(name);
        }
        entry.close();
    }
    dir.close();
    // Sort descending so newest (highest ISO stem) is first.
    std::sort(out.begin(), out.end(), std::greater<String>());
    return out;
}

bool remove(const String& filename) {
    String path = String(kDir) + "/" + filename;
    return SD.remove(path.c_str());
}

String buildAskContext(const std::vector<String>& filenames, int* loadedOut) {
    String s;
    s.reserve(8192);
    s  = "The following are voice memos the user recorded. Use them as "
         "primary reference material when answering the user's questions. "
         "Cite which memo you're drawing from when relevant.\n\n";

    int loaded = 0;
    for (const auto& fn : filenames) {
        NoteMeta meta;
        String body;
        if (!load(fn, meta, body)) {
            Serial.printf("[notestore] context: skip unparseable %s\n", fn.c_str());
            continue;
        }
        s += "--- MEMO: ";
        s += meta.title.length() ? meta.title : String("Untitled");
        if (meta.createdUtc.length() >= 10 && !meta.createdUtc.startsWith("boot-")) {
            s += " (";
            s += meta.createdUtc.substring(0, 10);
            s += ")";
        }
        s += " ---\n";
        s += body;
        if (!body.endsWith("\n")) s += "\n";
        s += "\n";
        loaded++;
    }
    if (loadedOut) *loadedOut = loaded;
    return s;
}

} // namespace notestore
