// Adapted from CardputerLLM/src/storage/snapshot.cpp. Output path moved to
// /Cardputer/snaps/.
#include "snapshot.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <time.h>
#include <vector>

namespace snapshot {

namespace {

constexpr int kW = 240;
constexpr int kH = 135;

void put32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}
void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

String tsName() {
    time_t now = time(nullptr);
    if (now < 1577836800) {
        char b[32];
        snprintf(b, sizeof(b), "boot-%lu.bmp", (unsigned long)millis());
        return String(b);
    }
    struct tm t; gmtime_r(&now, &t);
    char b[40];
    snprintf(b, sizeof(b), "%04d%02d%02dT%02d%02d%02dZ.bmp",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    return String(b);
}

} // namespace

String captureToBMP() {
    if (!SD.exists("/Cardputer"))       SD.mkdir("/Cardputer");
    if (!SD.exists("/Cardputer/snaps")) SD.mkdir("/Cardputer/snaps");

    std::vector<uint16_t> buf((size_t)kW * kH, 0);
    M5Cardputer.Display.readRect(0, 0, kW, kH, buf.data());

    const uint32_t rowBytes  = kW * 3;
    const uint32_t pixBytes  = rowBytes * kH;
    const uint32_t fileSize  = 54 + pixBytes;

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    put32(hdr + 2,  fileSize);
    put32(hdr + 10, 54);
    put32(hdr + 14, 40);
    put32(hdr + 18, kW);
    put32(hdr + 22, kH);
    put16(hdr + 26, 1);
    put16(hdr + 28, 24);
    put32(hdr + 34, pixBytes);

    String name = tsName();
    String path = String("/Cardputer/snaps/") + name;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return String();
    f.write(hdr, 54);

    std::vector<uint8_t> row(rowBytes);
    for (int y = kH - 1; y >= 0; y--) {
        for (int x = 0; x < kW; x++) {
            uint16_t p = buf[(size_t)y * kW + x];
            uint8_t r = ((p >> 11) & 0x1F) << 3;
            uint8_t g = ((p >>  5) & 0x3F) << 2;
            uint8_t b = ((p >>  0) & 0x1F) << 3;
            r |= r >> 5;
            g |= g >> 6;
            b |= b >> 5;
            int idx = x * 3;
            row[idx + 0] = b;
            row[idx + 1] = g;
            row[idx + 2] = r;
        }
        f.write(row.data(), rowBytes);
    }
    f.close();
    return name;
}

} // namespace snapshot
