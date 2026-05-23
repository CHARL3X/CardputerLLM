# NOTES

Merge log. The full design intent and Phase 1-7 history for each app
live in `../CardputerLLM/NOTES.md` and `../Verbatim/NOTES.md`. This
file documents only what's new in the merge.

## Phase M: merge CardputerLLM + Verbatim into one firmware

### Goal

One binary. Boot lands on a mode picker; user chooses LLM (chat) or
Verbatim (voice notes). Shared credentials. Same hardware target.

### Base

Forked from Verbatim's v1.0-ready tree (most-recent code, has lazy
credential reads, has the menu pattern, has the loopTask stack
override). Brought across two files from CardputerLLM:

- `src/ui/chat_screen.{h,cpp}` -- the full chat terminal
- `src/storage/chat_store.{h,cpp}` -- session persistence

### Unified SD + NVS namespaces

- SD root `/Cardputer/` (was `/CardputerLLM/` and `/Verbatim/`)
- NVS namespace `cardputer` (was `cardputerllm` and `verbatim`)

Co-resident data: `/Cardputer/chats/` (LLM), `/Cardputer/notes/`
(Verbatim), `/Cardputer/snaps/` (shared). Credentials shared:
`/Cardputer/{openrouter,wifi,system}.txt`.

Bulk rename done via `sed -i 's|/Verbatim|/Cardputer|g; s|/CardputerLLM|/Cardputer|g'`
across `src/**/*.{h,cpp}`. Followed by spot-edits for HTML branding
in the key_setup form, X-OpenRouter-Title HTTP headers in
transcribe.cpp / title.cpp, and the splash wordmark.

### Merged settings

`src/storage/settings.{h,cpp}` carries every key from both prior
apps plus a new `last_mode` (uint8: 0 = LLM, 1 = Verbatim) for the
picker default. All keys still ≤ 15 chars per NVS limit.

```
welcomed, bootsound       // shared from both
last_mode                 // new
histdepth                 // from CardputerLLM
askdepth, tx_model, title_model  // from Verbatim
```

### Mode picker

`src/ui/mode_picker.{h,cpp}` -- M5Canvas body double-buffer (per
the flicker memory). Two stacked rows: title + description per
mode. Active row gets a 3 px amber bar and an animated chevron
(pulses ~3 Hz) so the screen has ambient life while the user is
deciding.

Keymap:
- `,` / `;` → highlight up
- `.` / `/` → highlight down
- `1` / `2` → jump directly to the matching row
- Enter → launch the highlighted mode

The picker defaults to whatever `settings::lastMode()` returned --
on first boot that's LLM (0).

### Unified splash + welcome

- `splash.cpp` wordmark: `CARDPUTER` (was app-specific). Subtitle:
  `L L M . V O X` -- nods to both apps. Same chime, same animation
  choreography. `v1.0` label.
- `welcome.cpp` copy retargeted to "a pocket terminal for chat &
  voice notes" with `llm / vox / pick a mode next` hint lines.
  Shown once per device before the first mode pick.

### Boot orchestration

`src/main.cpp` rewritten:

1. M5Cardputer.begin, Serial.begin, settings::begin
2. splash::run (unified)
3. boot_ui::startLog
4. SD mount + `sdcfg::ensureDirs()` (creates all of chats/notes/snaps)
5. WiFi scan-first connect (with `wifi_setup::run` onboarding fallback)
6. NTP best-effort
7. Load `/Cardputer/system.txt` (or built-in default with the LLM-
   tag instructions appended)
8. Load `/Cardputer/openrouter.txt` (with `key_setup::run` web
   form fallback)
9. boot_ui::finishLog
10. Mic warm-up cycle -- needed even if user picks LLM mode so the
    codec state is consistent
11. `welcome::run()` if first-run
12. `mode_picker::run(settings::lastMode())` -- get the user's choice
13. `settings::setLastMode(chosen)` -- remember for next boot
14. Dispatch: either construct ESPAI provider + ChatScreen, or
    construct NotesScreen. The remaining loop() just ticks the
    chosen screen until reboot.

### Switching modes

For v1.0: reboot. The picker shows on every boot. (A "switch mode"
menu item that calls `ESP.restart()` is a v1.1 polish item -- both
chat_screen and menu_screen would gain it.)

### Brand swap in key_setup form

The web onboarding page now shows "Cardputer" / "cardputer ·
onboarding" / "Key written to /Cardputer/openrouter.txt." Same trust
model: HTTP on :80, no auth, listens only while setup is active.

### Footprint

```
RAM:   15.4% used (50 KB of 327 KB)
Flash: 39.9% used (1335 KB of 3342 KB)
```

Up from ~1300 KB (Verbatim v1.0) -- the +35 KB is ChatScreen +
chat_store + mode_picker.

### To verify on hardware

- [ ] Splash plays with chime (CARDPUTER wordmark, L L M . V O X
      subtitle, v1.0 label)
- [ ] On a fresh `/Cardputer/` namespace (no existing creds), the
      WiFi + key onboarding flows trigger as expected
- [ ] Welcome screen fires once
- [ ] Mode picker shows two options with the chevron animating on
      the highlighted row
- [ ] Pressing Enter on CardputerLLM launches the chat terminal
- [ ] Pressing Enter on Verbatim launches the notes list
- [ ] Reboot defaults the picker to the previously chosen mode
- [ ] Either app's full feature set still works end-to-end (chat
      streaming, slash commands, model picker; record + transcribe
      + title + save + browse + ask)

### Intentionally not in the merge (v1.1+)

- **One-time SD migration** from `/Verbatim/` or `/CardputerLLM/` to
  `/Cardputer/`. Users currently re-enter wifi + key once after
  flashing the merged firmware.
- **"Switch mode" menu item** that calls `ESP.restart()`. Both
  chat_screen's Menu mode and Verbatim's menu_screen would gain
  this.
- **Persistent mode auto-launch**: skip the picker entirely if the
  user hasn't pressed any key by a short timeout. Could be nice as
  an opt-in setting.
