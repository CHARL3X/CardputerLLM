#include "chat_store.h"
#include <SD.h>
#include <time.h>

namespace chatstore {

String newSessionFilename() {
    time_t now = time(nullptr);
    // Anything before 2020 means NTP hasn't synced; fall back.
    if (now < 1577836800) {
        char buf[32];
        snprintf(buf, sizeof(buf), "boot-%lu.json", (unsigned long)millis());
        return String(buf);
    }
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ.json",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return String(buf);
}

bool saveSession(const String& filename,
                 const ESPAI::Conversation& conv,
                 const String& modelSlug) {
    String path = String("/Cardputer/chats/") + filename;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return false;

    f.print("{\"model\":\"");
    f.print(modelSlug);
    f.print("\",\"saved_at\":");
    f.print((unsigned long)time(nullptr));
    f.print(",\"conversation\":");
    f.print(conv.toJson());
    f.print("}");
    f.close();
    return true;
}

} // namespace chatstore
