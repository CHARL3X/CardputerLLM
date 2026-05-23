// Note persistence: writes timestamped .md files with YAML
// frontmatter to /Cardputer/notes/, and reads them back for the
// detail viewer + ask-mode context builder.
//
// Filename pattern (matches Verbatim/SPEC):
//   YYYYMMDDTHHMMSSZ-<slug>.md
//
// Frontmatter:
//   ---
//   created: 2026-05-23T14:32:11Z
//   title: Cardputer firmware ideas
//   duration_sec: 47
//   model_transcribe: openai/whisper-large-v3
//   model_title: google/gemini-2.5-flash
//   ---
//
//   <transcript body>

#pragma once
#include <Arduino.h>
#include <vector>

namespace notestore {

struct NoteMeta {
    String   createdUtc;       // "2026-05-23T14:32:11Z" or "boot-NNNN"
    String   title;            // sanitized; "Untitled note" on fallback
    uint32_t durationSec = 0;
    String   modelTranscribe;
    String   modelTitle;
};

// Generates filename from createdUtc + slugified title. Writes the
// .md file to /Cardputer/notes/. Returns the filename (no path) on
// success, empty String on any failure.
String save(const NoteMeta& meta, const String& transcript);

// Reads /Cardputer/notes/<filename>. Splits frontmatter from body.
// Returns false on any parse failure.
bool load(const String& filename, NoteMeta& outMeta, String& outBody);

// Lists all .md filenames in /Cardputer/notes/, sorted descending
// (newest first since filenames are ISO timestamps).
std::vector<String> list();

bool remove(const String& filename);

// Slug helper exposed for callers that want to preview the eventual
// filename (e.g., diagnostics). Lowercases, replaces non-alnum with
// '-', collapses runs, caps at 30 chars, defaults to "note" if empty.
String slugify(const String& title);

// Format the current UTC time as ISO 8601 with seconds precision, or
// "boot-NNN" fallback if NTP hasn't synced.
String utcIsoNow();

// Build the ask-mode system context from a list of note filenames.
// Format matches Verbatim/SPEC:
//   The following are voice memos the user recorded. Use them as
//   primary reference material when answering the user's questions.
//   Cite which memo you're drawing from when relevant.
//
//   --- MEMO: Cardputer firmware ideas (2026-05-23) ---
//   <full transcript>
//
//   --- MEMO: Grocery list (2026-05-23) ---
//   <full transcript>
//
// Notes that fail to load are silently skipped (parseable notes still
// land in the result). `loaded` is set to the number that actually
// landed -- used by the preflight to show an accurate count if some
// notes are corrupt.
String buildAskContext(const std::vector<String>& filenames, int* loadedOut = nullptr);

} // namespace notestore
