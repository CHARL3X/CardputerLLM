// Verbatim menu (Phase 7).
//
// Opened from NotesScreen via the backtick / tilde key. Hosts the
// settings / diagnostics / wifi / key flows that don't fit cleanly as
// per-row actions. Blocking; owns the screen until user selects "exit"
// or backs out.
//
// Reads all the state it needs (settings, sd, wifi) inline -- no
// arguments. Action items that change credentials (add wifi, set api
// key) write to SD; callers should re-read on return if they cache
// credentials.
#pragma once

namespace menu_screen {

void run();

} // namespace menu_screen
