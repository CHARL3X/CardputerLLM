#pragma once
#include <Arduino.h>

namespace record_screen {

// Full-screen recording mode. Captures 16 kHz mono 16-bit PCM straight to
// /Cardputer/.recording.wav, then offers a discrete decision screen:
//   [t]/[enter]  transcribe -- streams the WAV to OpenRouter and surfaces
//                              the transcript on serial + a brief
//                              on-screen preview. Phase 3 keeps the WAV;
//                              Phase 4 will save the .md note and delete.
//   [d]/[del]    delete     -- removes the WAV from SD immediately.
//
// Returns true if the loop completed cleanly (mic init OK, WAV saved,
// decision dispatched). Returns false on hardware errors during capture
// (mic begin failed, SD write failed mid-stream, etc.).
bool run(const String& apiKey,
         const String& txModel,
         const String& titleModel);

} // namespace record_screen
