#include "wav_writer.h"
#include <string.h>

namespace wav {

namespace {

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

} // namespace

bool WavWriter::begin(const char* path, uint32_t sampleRate,
                      uint16_t channels, uint16_t bitsPerSample) {
    _path           = String(path);
    _sampleRate     = sampleRate;
    _channels       = channels;
    _bitsPerSample  = bitsPerSample;
    _samplesWritten = 0;

    // SD on M5Cardputer truncates with FILE_WRITE, which is exactly what
    // we want for a transient ".recording.wav" that gets overwritten.
    if (SD.exists(path)) SD.remove(path);
    _file = SD.open(path, FILE_WRITE);
    if (!_file) return false;
    if (!writeHeader(0)) {
        _file.close();
        SD.remove(path);
        return false;
    }
    return true;
}

bool WavWriter::writeHeader(uint32_t dataBytes) {
    uint32_t byteRate   = _sampleRate * _channels * (_bitsPerSample / 8);
    uint16_t blockAlign = _channels * (_bitsPerSample / 8);
    uint32_t fileSizeM8 = 36 + dataBytes;

    uint8_t hdr[44];
    memcpy(hdr +  0, "RIFF", 4);
    put32 (hdr +  4, fileSizeM8);
    memcpy(hdr +  8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    put32 (hdr + 16, 16);            // fmt chunk size
    put16 (hdr + 20, 1);             // PCM
    put16 (hdr + 22, _channels);
    put32 (hdr + 24, _sampleRate);
    put32 (hdr + 28, byteRate);
    put16 (hdr + 32, blockAlign);
    put16 (hdr + 34, _bitsPerSample);
    memcpy(hdr + 36, "data", 4);
    put32 (hdr + 40, dataBytes);
    return _file.write(hdr, 44) == 44;
}

bool WavWriter::write(const int16_t* samples, size_t count) {
    if (!_file) return false;
    size_t bytes = count * sizeof(int16_t);
    size_t w = _file.write(reinterpret_cast<const uint8_t*>(samples), bytes);
    if (w != bytes) return false;
    _samplesWritten += count;
    return true;
}

bool WavWriter::end() {
    if (!_file) return false;
    uint32_t dataBytes = (uint32_t)_samplesWritten * _channels * (_bitsPerSample / 8);
    if (!_file.seek(0)) {
        _file.close();
        return false;
    }
    bool ok = writeHeader(dataBytes);
    _file.close();
    return ok;
}

void WavWriter::cancel() {
    if (_file) _file.close();
    if (_path.length() > 0) SD.remove(_path.c_str());
    _samplesWritten = 0;
}

uint32_t WavWriter::durationMs() const {
    if (_sampleRate == 0) return 0;
    return (uint32_t)((uint64_t)_samplesWritten * 1000ULL / _sampleRate);
}

} // namespace wav
