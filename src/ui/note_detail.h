// Scrollable transcript / note detail viewer.
//
// Same component reused across phases:
//   Phase 3: shows the transcript that just came back from Whisper
//   Phase 4: shows a freshly-titled-and-saved note (passes filename)
//   Phase 5: opened from the notes list for any saved .md
//
// Blocking; owns the screen until the user backs out. Uses M5Canvas
// double-buffering so scrolling doesn't flicker per the cardputer-
// flicker-rules memory.
//
// If `filename` is non-empty, Fn+D triggers an inline delete confirm.
// On confirm-yes the file is removed via notestore::remove() and the
// function returns true ("the note no longer exists, refresh your list").
// On any other exit (Backspace, backtick/tilde, normal back-out), returns
// false.
#pragma once
#include <Arduino.h>

namespace note_detail {

bool show(const String& title,
          const String& body,
          const String& filename = String(),
          const String& subtitle = String());

} // namespace note_detail
