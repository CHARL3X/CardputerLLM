// Streaming WAV writer for SD card capture.
//
// A 5-minute 16 kHz mono 16-bit recording is ~9.6 MB and cannot live in
// RAM on the FN8 variant -- this writer streams int16 samples straight
// to SD as they come in. The 44-byte PCM header is written as a
// placeholder at begin() and rewritten with the real data size at end()
// once the sample count is known.
#pragma once
#include <Arduino.h>
#include <SD.h>

namespace wav {

class WavWriter {
public:
    // Opens `path` on the SD and writes a 44-byte placeholder header.
    // Default config matches what OpenRouter's Whisper endpoint expects
    // natively (16 kHz, mono, 16-bit PCM) so Phase 3 won't need to
    // resample. Returns false on open failure.
    bool begin(const char* path, uint32_t sampleRate = 16000,
               uint16_t channels = 1, uint16_t bitsPerSample = 16);

    // Append `count` int16 samples (interleaved if stereo, but we
    // never use stereo). Returns false if the SD write is short.
    bool write(const int16_t* samples, size_t count);

    // Seek to 0, rewrite header with the real data size, close the
    // file. Returns false if the rewrite or close fails.
    bool end();

    // Close and delete the file (used when a capture fails mid-stream
    // and we don't want a corrupt placeholder to linger).
    void cancel();

    size_t   samplesWritten() const { return _samplesWritten; }
    uint32_t durationMs()     const;

private:
    File     _file;
    String   _path;
    uint32_t _sampleRate     = 0;
    uint16_t _channels       = 0;
    uint16_t _bitsPerSample  = 0;
    size_t   _samplesWritten = 0;

    bool writeHeader(uint32_t dataBytes);
};

} // namespace wav
