# NOTES

Running log of empirical findings. Append; do not rewrite history.

## Phase 1: scaffold from CardputerLLM

### Goal

Stand up Verbatim as a sibling firmware to CardputerLLM. Same boot
decision tree, same aesthetic, same hardware target. No audio, no
transcription, no ask mode yet. Land on a polished "0 notes" home
screen with the full onboarding fallbacks wired up.

### Repo layout mirrors CardputerLLM

```
Verbatim/
  platformio.ini
  support/copy_dist.py
  src/
    main.cpp
    storage/{sd_config,settings,snapshot}.{h,cpp}
    setup/{wifi_setup,key_setup}.{h,cpp}
    ui/{boot_ui,splash,welcome,styled_text,notes_screen}.{h,cpp}
  docs/screenshots/
```

Every file that came from CardputerLLM carries an attribution comment on
line 1 stating it was copied verbatim or adapted, and what diverged.

### Copied verbatim (zero behaviour change)

- `src/ui/boot_ui.{h,cpp}` — direct-draw helpers (header/footer/
  sectionHeader/hintBar/step log/waitForAnyKey). No project name
  appears anywhere inside; reusable as-is.
- `src/ui/styled_text.{h,cpp}` — markup dialect parser + renderer.
  Doesn't ship in Phase 1 yet but Phase 6 (ask mode) needs it.
- `src/storage/snapshot.h` — interface unchanged; output path moved
  in `.cpp` only.

### Adapted (path / namespace / copy changes)

- `platformio.ini` — `default_envs = verbatim`, env name `[env:verbatim]`,
  `extra_scripts = post:support/copy_dist.py`.
- `support/copy_dist.py` — output filename `Verbatim.bin` (was `CardputerLLM.bin`).
- `src/storage/sd_config.{h,cpp}` — all paths re-namespaced from
  `/CardputerLLM/...` to `/Verbatim/...`. `ensureChatsDir()` renamed to
  `ensureNotesDir()` (creates `/Verbatim/notes/`). Added
  `countNotes()` for the status row.
- `src/storage/settings.{h,cpp}` — NVS namespace `cardputerllm` -> `verbatim`.
  Dropped `histdepth`; added `askdepth` (int, default 10),
  `tx_model` (String, default `openai/whisper-large-v3-turbo`),
  `title_model` (String, default `google/gemini-2.5-flash`). Kept
  `welcomed` and `bootsound`. All keys ≤ 15 chars per NVS limit.
- `src/storage/snapshot.cpp` — output path `/CardputerLLM/snaps/` ->
  `/Verbatim/snaps/`. Otherwise byte-identical.
- `src/setup/wifi_setup.{h,cpp}` — only divergence is the
  "saved to /Verbatim/wifi.txt" hint string in `drawConnected()`. The
  actual save path comes from `sdcfg::appendWiFiCred()`, which is
  namespaced via sd_config.
- `src/setup/key_setup.{h,cpp}` — HTML form title `Verbatim · set api key`,
  eyebrow `verbatim · onboarding`, done-page copy "Cardputer is continuing
  to the notes list." and save path string `/Verbatim/openrouter.txt`.
  Trust model unchanged: HTTP, no auth, server alive only while no key
  is present (boot path) or during explicit "set api key" from the menu.
- `src/ui/splash.{h,cpp}` — wordmark `VERBATIM` (was `CARDPUTER`),
  subtitle `VOX . LOG` (was `L  L  M`), version `v0.1`. Same chime,
  same animation choreography.
- `src/ui/welcome.{h,cpp}` — copy retargeted to "a pocket recorder for
  voice notes." with `spc / ret / esc` keybind cheat sheet.

### New for Verbatim

- `src/ui/notes_screen.{h,cpp}` — Phase 1 shell. Direct-draw (no
  M5Canvas yet) since the screen is static between ticks. Status row
  borrows the LED + RSSI + clock layout from
  `CardputerLLM/src/ui/chat_screen.cpp`, right-aligning `N notes`
  instead of the model label. Empty-state body recreates the
  `renderEmptyChat()` watermark idea with the VERBATIM/VOX.LOG word-
  mark, a drifting hairline pip (~80ms phase), and a center-bottom tip.
  Two-line hint bar at the bottom names the keys Phase 5 will wire up.
  Periodic 5s status refresh so RSSI and clock stay current.

### Boot decision tree (matches CardputerLLM exactly)

```
boot
  M5Cardputer.begin, rotation 1, fill black
  Serial @ 115200
  settings::begin
  splash::run
  boot_ui::startLog
  sdcfg::begin -> mounting sd [ok] or halt
  sdcfg::ensureNotesDir
  load wifi.txt
  scanNow / filterByVisibility / connectWiFiFromList
    -> wifi_setup::run(false) if everything fails
  syncTimeQuiet (best effort, doesn't gate)
  load system.txt (optional)
  load openrouter.txt
    -> key_setup::run(false) if missing
  boot_ui::finishLog (renders "ready.")
  first run -> welcome::run + settings::setWelcomed(true)
  NotesScreen::begin
```

ESPAI provider construction is intentionally deferred to Phase 6
(ask mode) — Phase 1 only verifies the key loads. `g_apiKey` is held
in main scope so phases 3/4/6 can wire it up without re-loading.

### Two-app coexistence

- SD namespace `/Verbatim/` (sibling to `/CardputerLLM/`)
- NVS namespace `verbatim` (sibling to `cardputerllm`)
- Launcher binary at `/apps/Verbatim.bin` per the spec

Neither firmware touches the other's namespaces. They share patterns,
not state. Either can be wiped without affecting the other.

### To verify on hardware (Phase 1 acceptance)

These are the boxes Phase 1 ticks before Phase 2 starts. I can't run
them from this workstation; Charles needs to flash and check.

- Cold boot with a populated SD: splash plays, boot log shows every
  step `[ok]`, lands on the notes screen with `0 notes` and "no notes
  yet . press SPC to record" tip.
- Delete `/Verbatim/openrouter.txt`, reboot: api key step shows `[!!]`,
  device drops into `key_setup::run(false)` showing `http://<ip>` URL
  with corner brackets. Paste a key from a laptop, form confirms,
  device continues into the notes screen.
- Delete `/Verbatim/wifi.txt`, reboot: wifi step shows `[!!]`, device
  drops into `wifi_setup::run(false)` with a polished scan list,
  password entry, connect screen, and "saved to /Verbatim/wifi.txt"
  confirmation. Reboot should now succeed silently.
- Status row updates HH:MM and RSSI bars every ~5s without user input.
- `settings::welcomed()` flips after the first boot so the welcome
  screen only fires once per device.
- Cross-check: flashing CardputerLLM after Verbatim must not touch
  Verbatim's NVS namespace or SD directory, and vice versa.

### Open questions to settle before Phase 4 ships

- Title model default: Phase 4 should hit `/api/v1/models` and confirm
  `google/gemini-2.5-flash` is still the cheapest current fast tier.
  Possible swap: `openai/gpt-5-mini` if it exists.
- Transcription model: Phase 3 should confirm `openai/whisper-large-v3-turbo`
  is still the cheapest turbo Whisper variant on OpenRouter.

### Phase 2 entry conditions

Phase 1 ends with a boot path that reaches NotesScreen cleanly on
hardware. Phase 2 starts with `M5Cardputer.Mic` API discovery: figure
out whether the library exposes a clean recorder for the ADV board,
or whether we configure I2S to the ES8311 directly. CardputerLLM's
NOTES.md flagged that codec init was deferred; this is the first
build that actually has to deal with it.

## Phase 1.5 polish (hardware reveal)

Two issues surfaced on first flash:

1. **NotesScreen flicker.** Empty-state ran an 80 ms watermark
   animation that called `fillScreen(black)` then redrew. On the
   unbuffered ST7789 that reads as visible black-flash at ~13 Hz.
   Phase 1 doesn't need any animation, so removed `_animTick`/
   `_animPhase` entirely. Memory rule saved: any screen that repaints
   more than ~once a second needs M5Canvas, period.

2. **Key-setup URL clipped.** `drawWaitingScreen()` rendered the full
   `http://<ip>` at Font2 textSize 2 (~16 px/char). For an IP whose
   last octet is two digits or more, the rendered width exceeds 240
   and the right side gets silently clipped -- user reads e.g.
   `http://192.168.1.9` when the actual address is `http://192.168.1.93`.
   Visiting the truncated URL from any client hangs (no host responds).
   Fixed by splitting the scheme into a small label above the IP and
   sizing the IP-only line to fit, autoshrinking to textSize 1 for the
   rare 255-style worst case. **CardputerLLM has the same bug**;
   carrying that forward to patch upstream.

## Phase 2: audio capture (16 kHz mono PCM → SD)

### Mic API surprise: zero divergence from M5Unified defaults

`M5Unified::Mic_Class` already knows about the Cardputer ADV. From
`.pio/libdeps/verbatim/M5Unified/src/M5Unified.cpp`:

```
case board_t::board_M5CardputerADV:
  if (cfg.internal_mic) {
    mic_cfg.pin_data_in = GPIO_NUM_46;
    mic_cfg.pin_ws      = GPIO_NUM_43;
    mic_cfg.pin_bck     = GPIO_NUM_41;
    mic_enable_cb = _microphone_enabled_cb_cardputer_adv;
  }
```

`_microphone_enabled_cb_cardputer_adv` does all the ES8311 register
writes over the internal I2C bus (codec at `0x18`). Public API:
`M5.Mic.begin()`, `M5.Mic.record(int16_t*, n, 16000)`, `M5.Mic.end()`.
Mic on I2S_NUM_0, speaker on I2S_NUM_1 — different ports but the
codec is half-duplex, so `Speaker.end()` is required before
`Mic.begin()` (covered in the example).

This was the highest-risk phase per the spec ("If the mic API isn't
clean, budget time. The whole project depends on getting clean
audio.") -- it turned out to be the cleanest part of Phase 2.

### Streaming WAV writer

`src/audio/wav_writer.{h,cpp}`: opens `/Verbatim/.recording.wav` with
`FILE_WRITE` (truncates), writes a 44-byte placeholder PCM header,
appends int16 sample blocks straight to SD as they come in, then
seeks to 0 and rewrites the header with the real data size at
`end()`. Worst-case 5-minute file is ~9.6 MB; the FN8 has no PSRAM
and ~150 kB free heap during capture, so RAM buffering is not an
option.

### record_screen.cpp

`M5Canvas` body double-buffer (240 × 103 = ~49 kB) for the VU meter
and elapsed counter; status row and hint bar are direct-draw once.
Loop cadence is paced by `Mic.record(buf, 512, 16000)` which blocks
~32 ms per call -- gives 30 Hz VU update for free, stays under the
default 1024-sample (~64 ms) DMA queue so SD writes don't drop
samples. Hard cap 5:00, warn faster-flash starts at 4:30.

### Mic gain calibration -- first hardware encounter with deferred risk

CardputerLLM/NOTES.md flagged ES8311 init as deferred. We hit it.

Default M5Unified mic init pins ES8311 register `0x14 = 0x10`
(comment in source: "MIC1p-Mic1n / PGA GAIN (minimum)"). High nibble
selects MIC1 input; low nibble is PGA gain in 6 dB steps (0..7 →
0..42 dB). At 0 dB analog gain plus `mic_config_t.magnification = 16`
(default), normal speech at 30 cm reads as a few hundred LSB out of
32767 -- VU meter barely twitches even on speech. Blowing into the
mic reads, since plosive impulses are -10 dB louder than speech.

Two-knob fix, applied after `Mic.begin()`:

- `M5Cardputer.In_I2C.writeRegister8(0x18, 0x14, 0x14, 400000)`
  -- keep MIC1 input, set PGA to 24 dB (low nibble = 4)
- `mc.magnification = 32` (was 16) for additional digital headroom

If the saved WAV plays at normal level on a PC, we're done. If still
quiet, bump 0x14 value to 0x15 (30 dB) or 0x16 (36 dB). If clipping
on normal speech, drop magnification to 16 first, then PGA to 0x13.

### The post-record buzz

User-reported: a faint buzz emerges from the speaker AFTER a
recording stops. Not at boot, not during recording.

Root cause: `_speaker_enabled_cb_cardputer_adv` in M5Unified has
`disabled_bulk_data = { 0 }` -- literally empty. `Speaker.end()` for
this board tears down I2S_NUM_1's driver but writes NOTHING to the
ES8311, so the codec's DAC + HP-drive state is whatever was last
configured. `Mic.end()`'s `disabled_bulk_data` does write `0x00=0x00`
(CSM power down), but I2S_NUM_0 also tears down, leaving the shared
BCK (GPIO 41) and WS (GPIO 43) pins undriven. The PA picks up
floating clocks and emits noise.

Fix: call `M5Cardputer.Speaker.begin()` immediately after
`M5Cardputer.Mic.end()` to put the codec back in the boot-time
"DAC begun, idle, no signal" state, which is what's quiet at boot.
I2S_NUM_1 resumes driving BCK/WS, clocks are clean again, codec
silence is silent.

### Post-record decision screen (added mid-phase per Charles request)

After `Mic.end()` + writer rewrite, instead of returning straight to
NotesScreen with a 1.5 s confirmation flash, the user gets a discrete
choice:

- `[t]` or `[enter]` -- transcribe (Phase 2 placeholder; Phase 3
  hooks the upload off this branch)
- `[d]` or `[del]` -- delete the WAV from SD now, return

No navigation, one keystroke per choice. The recording's "stop key"
press is consumed before this screen activates so the user doesn't
accidentally trigger immediate dispatch.

### Phase 3 prep

Spec calls Phase 3 (transcription upload) the highest-risk technical
moment in the project. Notes for the next session:

- **Endpoint**: `POST https://openrouter.ai/api/v1/audio/transcriptions`
  -- per the May 1 2026 announcement, accepts JSON body with
  base64-encoded audio. Re-verify the exact field names against
  current OpenRouter docs before coding, since the API is weeks old.
- **The math** (must be exact, off-by-one = silent 400):
  - `wav_n` = file size in bytes (call `f.size()` on the open file)
  - `b64_n = ((wav_n + 2) / 3) * 4`
  - `prefix = "{\"model\":\"<slug>\",\"file\":\""`
  - `suffix = "\"}"`
  - `body_n = strlen(prefix) + b64_n + strlen(suffix)`
- **Connection**: separate `WiFiClientSecure` (don't reuse ESPAI's).
  ESPAI's `HttpTransportESP32.cpp` and `RootCACerts.cpp` are the
  reference for the root-cert bundle.
- **Streaming send**:
  1. Open WAV from SD, get exact size.
  2. Open TLS connection, write headers including `Content-Length: <body_n>`.
  3. Write prefix.
  4. Loop: read 3 bytes from WAV, base64-encode to 4 ASCII chars,
     write to socket. Handle the final partial triplet (1 or 2 bytes
     → 4 chars with `=` padding) correctly.
  5. Write suffix.
  6. Read response, parse JSON streaming with `ArduinoJson` to extract
     `text`.
- **Fallback**: if exact Content-Length is fragile,
  `Transfer-Encoding: chunked` is the standard alternative. Try CL
  first since it's simpler; pivot to chunked only if OpenRouter
  rejects.
- **Error handling**: 401 → "Invalid API key. Reset via menu." Keep
  the WAV. 429 → backoff with visible countdown, 3 retries. Network
  drop → keep WAV, mark note as "pending transcription" in the list
  view (Phase 5 surface). Whisper model unavailable → fall back to
  `openai/whisper-large-v3` (slower, more reliable); record which
  model in the .md frontmatter.
- **Heap check**: log `ESP.getFreeHeap()` before/after the upload.
  If <30 kB free during the call (with WiFi+TLS+SD+ArduinoJson
  parser all active), that's a red flag -- need to reduce buffer
  sizes or switch to a manual JSON parser.

### To verify on hardware (Phase 2 acceptance)

These are the boxes Phase 2 ticks before Phase 3 starts.

- Press SPC from NotesScreen -> record screen with REC dot + counter
  + VU meter. Counter increments 1 Hz.
- Normal speech at 30 cm -> VU peaks visible (~25-50% bar). Silence
  -> VU at floor.
- Hard cap at 5:00 -> auto-stop with "(5:00 cap reached)" subtitle.
- Press any key during capture -> stop, decision screen appears.
- Decision screen: `[t]` or enter -> "kept for transcription" and
  WAV remains at `/Verbatim/.recording.wav`. `[d]` or del -> wav
  removed from SD, "deleted" confirmation.
- Return to NotesScreen after either outcome.
- No post-record buzz.
- SD pulled to PC -> `.recording.wav` plays cleanly in any audio
  app (Audacity, VLC). 16 kHz mono 16-bit PCM. Duration matches
  on-device counter. Voice clearly intelligible at normal listening
  level.

## Phase 2.5 polish (hardware reveal)

- **First-record-after-boot was quiet + popped.** ES8311 cold-state
  vs the warm-state subsequent records. Fix: at end of boot, after
  `boot_ui::finishLog()`, run `Speaker.end -> Mic.begin -> delay 80
  -> Mic.end -> Speaker.begin`. That puts the codec through the
  same cycle a real record does, so the user's first record matches
  every subsequent one. Logged "[boot] mic warm-up complete".

## Phase 3: transcription upload (OpenRouter audio endpoint)

### Confirmed endpoint shape

Reference snippet from current OpenRouter docs (2026-05-23):

```
POST https://openrouter.ai/api/v1/audio/transcriptions
Authorization: Bearer <key>
Content-Type: application/json
{
  "model": "openai/whisper-large-v3",
  "input_audio": { "data": "<base64-wav>", "format": "wav" }
}
-> 200 OK
{"text": " <transcript>", "usage": {"seconds": 10, "cost": 0.0003083333..}}
```

The body has a leading space before the transcript. Phase 4 trim
on save. Useful side-data in `usage`: seconds (input audio length
billed) and cost (USD). Worth surfacing in diagnostics later.

### Content-Length vs chunked: chunked wins

We send with exact `Content-Length: <prefix + b64 + suffix>` and
that works (server accepts the upload). Cloudflare returns the
response with `Transfer-Encoding: chunked` (no `Content-Length`
in the response). Our parser reads the status line + headers,
detects `chunked`, then walks chunk-size-line / data / CRLF pairs
until a zero-size terminator. Verified with a real round-trip
returning 105 bytes of body across one chunk.

### Streaming-base64 implementation

`src/audio/transcribe.cpp`:
- Open WAV with `SD.open(path, FILE_READ)`, get exact size
- Compute `b64_n = ((wav_n + 2) / 3) * 4` then
  `body_n = prefix.length() + b64_n + suffix.length()`
- `WiFiClientSecure` declared `static` at function scope so the
  mbedTLS context is allocated once and reused across attempts
  (avoids progressively fragmenting the heap)
- `setInsecure()` -- Phase 7 polish: pin Cloudflare root
- Write headers, write prefix
- Loop: read 3*256 = 768 bytes from SD per iteration, base64-encode
  to 4*256 = 1024 chars, `client.write` the encoded chunk. All but
  the final read is forced to a multiple of 3 so we never emit `=`
  padding mid-stream
- Write suffix
- Status line + headers parsed by `readHeaderLine` (CRLF-terminated)
- Chunked body parser handles the Cloudflare response
- `ArduinoJson v7 JsonDocument` (heap-allocated, NOT
  `StaticJsonDocument<8192>` which would overflow the loop stack)
- Pulls `doc["text"]`, falls back to
  `doc["choices"][0]["message"]["content"]` for chat-style responses

### The mbedtls loop-task stack canary issue (the big bug)

`WiFiClientSecure::connect()` triggers an mbedtls handshake which
on ECDSA ciphers nests `mbedtls_pk_verify_restartable` ->
`ecdsa_verify_wrap` -> `mbedtls_ecp_muladd_restartable` ->
`ecp_mul_comb_core` -> `mpi_fill_random_internal` ->
`mbedtls_hmac_drbg_random` -> `mbedtls_md_hmac_finish` -> SHA-512.
That stack of frames + temporaries exceeds Arduino's default 8 KB
`loopTask` stack and trips the canary. Symptom: device panics
mid-handshake on the first `[t]` press, reboots cleanly.

`-DCONFIG_ARDUINO_LOOP_STACK_SIZE=N` in `build_flags` does **not**
fix this. PlatformIO ships `framework-arduinoespressif32` as a
precompiled lib; the macro only affects user-code compilation
units, not `cores/esp32/main.cpp` where `loopTask` is actually
spawned. Verified by ELF SHA being byte-identical before/after
the build_flag change.

Fix: override the framework's weak symbol in user code:

```cpp
// src/main.cpp
uint32_t getArduinoLoopTaskStackSize(void) { return 24576; }
```

The arduino-esp32 core declares
`uint32_t __attribute__((weak)) getArduinoLoopTaskStackSize(void)`
specifically so apps can override at link time. With 24 KB the
handshake clears with comfortable headroom for the rest of the
runtime (String, M5GFX, ArduinoJson all on the same task).

Memory `cardputer-tls-loop-stack` documents this for future Cardputer
TLS work.

### Observed latency (one 4.5-second test recording)

```
[transcribe] wav=143404 b64=191208 prefix=58 suffix=18 body=191284
[transcribe] heap pre-tls free=210568 min=163456 largest=151540
[transcribe] connected in 823 ms . heap=167176
[transcribe] upload done 191284/191284 in 1354 ms
[transcribe] <- HTTP/1.1 200 OK
[transcribe] body=105 bytes in 2 ms
[transcribe] === TRANSCRIPT BEGIN ===
 hey what's going on testing one two three
```

So for a 4.5 s clip:
- TLS connect + handshake: 823 ms
- Upload (191 KB body): 1354 ms -> ~140 KB/s effective
- Server inference + response: ~hundreds of ms (subjectively; not
  individually logged)
- Total: ~2.3 s wall-clock

Linear projection for 5-minute audio: ~30 s upload + inference.
Worth testing under Phase 4 before any UX polish.

### Post-transcribe UX

On Ok: `note_detail::show("draft transcript", text, "phase 3 . not
saved yet")`. The same scrollable detail viewer Phase 4 will reuse
after writing the .md, and Phase 5 will use when opening from the
list. Backspace returns to NotesScreen.

On error: dedicated failure screen with `outcomeName()` + http
status + "wav kept on sd for retry" + "full details on serial".

### Wiring forward to Phase 4

NotesScreen ctor now takes `(apiKey, txModel, titleModel)`. The
title model is held but unused in Phase 3. Phase 4 will:

1. Call OpenRouter chat completions on the title model with the
   transcript as the user message (prompt locked in spec)
2. Slugify the result
3. Write `/Verbatim/notes/<ts>-<slug>.md` with YAML frontmatter
4. Delete `/Verbatim/.recording.wav`
5. Hand off to `note_detail::show(title, transcript, path)` --
   same component, just with real data

### To verify on hardware (Phase 3 acceptance) -- DONE

- [x] Press SPC, record ~5s, press a key, press `[t]`
- [x] Progress bar fills during upload, switches to "waiting for
      whisper" once 100%, succeeds
- [x] note_detail viewer shows the transcript with scrollable wrap
- [x] Backspace returns to NotesScreen
- [x] WAV remains on SD after `[t]` (Phase 4 will delete)
- [x] Serial dumps the full transcript between "=== TRANSCRIPT
      BEGIN ===" / "=== TRANSCRIPT END ==="
- [x] No crashes

## Phase 4: title + .md persistence + WAV cleanup

End-to-end capture loop: SPC -> record -> stop -> [t] -> transcribe
-> name -> save -> delete WAV -> note_detail with the generated title.

### title.{h,cpp}

Non-streamed JSON POST to /api/v1/chat/completions with the locked
spec prompt. Same TLS/chunked-response pattern as transcribe.cpp,
just without the base64 streaming. Sanitizer strips edge quotes +
markdown chars, replaces newline/colon/double-quote with spaces (to
keep YAML well-formed), caps at 60 chars, falls back to "Untitled
note" on empty/error. Title generation is convenience; failures
don't gate the save.

### note_store.{h,cpp}

- save(meta, transcript): builds filename "<stem>-<slug>.md" from
  createdUtc + slugify(title), writes YAML frontmatter + transcript
  body to /Verbatim/notes/. Strips Whisper's leading space.
- load(filename, outMeta, outBody): parses frontmatter (key: value
  lines between --- delimiters) and body.
- list(): all .md files sorted descending (newest first).
- slugify(): lowercase, non-alnum -> '-', collapse runs, cap 30
  chars, default to "note" if empty.
- utcIsoNow(): ISO-8601 UTC or "boot-NNN" fallback.

### Verified on hardware

- "Testing." (4-char transcript) -> filename
  20260523T222859Z-testing.md, title "Testing"
- "Testing. 1, 2, 3, 4, 5, 6, 7. 1, 2." -> filename
  20260523T223153Z-audio-test-counting-numbers.md, title "Audio
  test counting numbers"
- Gemini Flash chooses sensible titles; sanitizer doesn't need to
  do much in practice
- Full chain TLS-connect + transcribe + title + save: ~6-8 s for
  short clips (TLS twice = ~1.5 s, two API calls + inference)

## Phase 5: notes list + detail interactions + UX honesty pass

The home screen IS the list. Rewrote NotesScreen as a state machine
modeled after CardputerLLM/src/ui/chat_screen.cpp:

- Mode::List         default; row navigation + open/delete/select/ask/record
- Mode::MultiSelect  checkboxes shown, SPC toggles selection
- Mode::Confirm      generic yes/no overlay for delete from list

### Row layout

Each row 18 px tall (Font2 size 1 + 2 px padding). 5 rows visible
in the 103-px body. Format:

```
[3px amber bar]  Title text (Font2, truncated if needed)        M/DD Ns
```

Title gets the leftover width after reserving ~60 px on the right
for the date+duration in Font0 dim. Selected row's bar is amber
and the title flips from cream to amber. Multi-select adds an 8x8
checkbox at the row start (filled if selected, outlined if not).

### Keymap (Phase 5 honesty pass)

Every advertised key is wired. Anything not wired isn't advertised.
Backtick / tilde stays bound as silent-back (CardputerLLM muscle
memory) but isn't shown in hints.

| Mode        | Key                          | Effect                          |
|-------------|------------------------------|---------------------------------|
| List        | SPC                          | Record (-> record_screen)       |
| List        | bare `,` `;` / Fn+`,` `;`    | Highlight up                    |
| List        | bare `.` `/` / Fn+`.` `/`    | Highlight down                  |
| List        | Enter                        | Open in note_detail             |
| List        | Fn+D                         | Delete confirm                  |
| List        | Fn+S                         | Enter MultiSelect               |
| List        | Fn+A                         | Ask mode (Phase 6 stub)         |
| MultiSelect | arrows                       | Highlight                       |
| MultiSelect | SPC                          | Toggle row selection            |
| MultiSelect | Fn+A                         | Ask with selection (stub)       |
| MultiSelect | Fn+S / Backspace             | Exit MultiSelect                |
| MultiSelect | Enter                        | Open highlighted in note_detail |
| Confirm     | Y / Enter                    | Yes                             |
| Confirm     | N / Backspace                | No                              |

Bare `,`/`.` work because this screen has no text-entry; they don't
need Fn-gating like chat mode does in CardputerLLM.

### Hint bars (per mode, all honest)

```
List populated:  [spc] new  [ret] open  [fn+d] delete
                 [fn+s] select  [fn+a] ask

List empty:      [spc] record
                 no notes yet . press SPC to begin

MultiSelect:     [spc] toggle  [fn+a] ask N
                 [fn+s] exit   [del] back   [ret] open

Confirm:         [y][enter]  yes
                 [n][del]    no
```

### note_detail enhancements

- New `filename` arg. If non-empty, Fn+D triggers an inline confirm
  ("delete this note?" with the title shown) -> notestore::remove
  -> brief "deleted" splash -> returns true to caller.
- Caller (NotesScreen list-mode `openHighlighted`, or
  record_screen's post-save handoff) checks the return value and
  rescans the list if true.
- Hint bar now adapts to whether `deletable` is true and whether
  the user is at top / bottom of the scroll range.

### Status row

In List mode: "N notes" right-aligned, accent color.
In MultiSelect mode: "K of N selected", same alignment.

### Rationale: why a state machine and not a flat key handler

When phase 6's ask mode lands, MultiSelect needs to know its
selection persists into ask mode. When phase 7's menu lands, it's
just another Mode value. The state machine here mirrors
CardputerLLM/src/ui/chat_screen.cpp so a future reader of either
codebase finds the same idiom. Confirm reuses the generic onYes
callback pattern from CardputerLLM's confirmDestructive.

### What's still intentionally absent

- Menu (the backtick/tilde key) -- silently swallowed for now. Hint
  doesn't mention it. Phase 7.
- Re-title (Fn+R) -- user's emphasis is delete, not retitle. Phase
  5 stretch / Phase 7.
- Detail edit mode -- read-only for now. Phase 7.
- Ask mode dispatch -- Fn+A drops to a static "phase 6 will wire
  this up" placeholder that shows the selection count and waits for
  any key. Wiring is there; LLM call lands Phase 6.

## Phase 6: ask mode -- chat with selected memos as context

The Fn+A stub from Phase 5 becomes real.

### Flow

NotesScreen MultiSelect (or List with a highlighted row) -> Fn+A
-> notestore::buildAskContext() concatenates the selected transcripts
into the spec's MEMO format with a header instructing the model to
cite which memo it's drawing from -> token preflight screen
("K memos . ~T tokens . [enter] start [del] back") -> AskScreen
runs the chat -> Backspace from empty input exits -> back to
NotesScreen with multi-select cleared.

### notestore::buildAskContext

Lives in note_store.{h,cpp}. Iterates filenames in the order given,
loads each via existing notestore::load, skips unparseable ones,
emits:

  The following are voice memos the user recorded. Use them as
  primary reference material when answering the user's questions.
  Cite which memo you're drawing from when relevant.

  --- MEMO: <title> (YYYY-MM-DD) ---
  <transcript>

  --- MEMO: ...

`loaded` out-param exposes how many notes actually made it past
parse so the preflight shows an accurate count.

### Token preflight

`runPreflight(memoCount, tokenEstimate)` -- direct-draw confirm. Token
estimate is `context.length() / 4` (crude per spec). Warns visually
in orange + extra line "heavy context . responses may be slow" if
estimate > 50000.

### ask_screen.cpp: AskScreen class

Cloned from CardputerLLM/src/ui/chat_screen.cpp, trimmed:

- Mode state machine: Chat, Picker
- M5Canvas body double-buffer with styled_text rendering of assistant
  turns (plain rendering for user turns, right-aligned in cream)
- ESPAI::OpenAICompatibleProvider constructed lazily per-call; system
  prompt is the built context; Conversation pre-set with it
- Streaming via _ai->chatStream with cancel-on-keypress (` / ~ / del
  during the stream sets _cancelStream)
- Model picker Fn+M with the same trio CardputerLLM uses:
  openai/gpt-5, anthropic/claude-sonnet-4.5, google/gemini-2.5-pro.
  Switching model does NOT clear the conversation (you may want to
  ask the same question of different models with the same context).
- Slash MVP: /clear, /help, /diag, /snap. No autocomplete popup yet
  (Phase 7).
- Backspace from empty input exits the session. Backtick/tilde also
  exits.
- Fn+,/. or ;// for scroll, same as chat_screen.

### Heap profile

Free heap before AskScreen::runLoop: ~150-180 KB after we deleted
NotesScreen's canvas in `enterAskMode`. AskScreen allocates its own
49 KB body canvas + ~25 KB mbedtls TLS context + variable
conversation accumulation. Comfortable.

### What's intentionally absent (Phase 6.5 / Phase 7)

- Slash autocomplete popup (was in scope per spec; deferred to ship
  the MVP)
- Hold-to-repeat for backspace + scrolling
- The other slash commands from CardputerLLM (/sys, /splash, /welcome,
  /sound, /model, /depth) -- not all are relevant in ask mode
- The Menu (backtick) -- landed in Phase 7
- Conversation persistence -- per spec, single-session only. Exiting
  ask wipes everything.

## Phase 7: menu + lazy credential reads + docs + v1.0

The last gaps before tagging v1.0.

### Menu (backtick / tilde)

`src/ui/menu_screen.{h,cpp}` is opened from NotesScreen's backtick
handler (previously silently swallowed). Own state machine: List
mode + Info mode. M5Canvas body buffer. Six items:

- `diagnostics`   -> Info screen with build, models (tx/title),
                     note count, heap (free/min/largest), uptime
- `wifi info`     -> ssid, ip, rssi, gw, dns, mac, utc
- `system prompt` -> /Verbatim/system.txt contents or "(built-in
                     default)" + override hint
- `add wifi`      -> wifi_setup::run(true) inline; on return the
                     menu rebuilds its canvas and redraws
- `set api key`   -> key_setup::run(true) inline; same canvas
                     dance on return
- `exit`          -> just sets _exit and breaks the loop

Hint bar adapts per mode:

```
List:  ,/. navigate  [ret] open
       [del] / [esc]  exit menu

Info:  ,/. scroll
       [del] / [esc]  back to menu
```

### Lazy credential reads

Phase 1–6 cached the API key + model slugs as NotesScreen members
set at construction time. With the menu now able to write a new
key to SD, those cached values would go stale and the next
ask/record would 401.

Fix: NotesScreen's dispatch helpers re-read from SD/NVS each call:

```cpp
String apiKey = sdcfg::loadOpenRouterKey();
record_screen::run(apiKey, settings::txModel(), settings::titleModel());
// and for ask:
String apiKey = sdcfg::loadOpenRouterKey();
ask_screen::run(apiKey, selected);
```

The ctor still takes the args for now (so main.cpp didn't have to
change), but they're no longer used at dispatch. Net effect: any
post-menu key change takes effect immediately, without restarting.

### Home-screen hint refresh

```
[spc] new  [ret] open  [fn+d] del
[fn+s] sel  [fn+a] ask  [esc] menu
```

The `[esc]` label is the spec's "esc" mental model; on the physical
keyboard that's the backtick/tilde key. Consistent with
CardputerLLM's chat hint convention of naming the *effect* rather
than the literal char.

### Splash version label + main.cpp banner -> v1.0

`src/ui/splash.cpp` shows "v1.0" under the wordmark.
`src/main.cpp` logs "[boot] verbatim v1.0" on Serial.

### README rewrite

`README.md` rewritten from scratch for v1.0. Full keymap for every
screen, SD layout with the .md frontmatter example, security note,
defaults, hardware list, build + deploy instructions.

### What's intentionally still absent (post-v1)

- **Slash commands on the home screen** (the `/snap /diag /help /sys
  /welcome /splash /sound /model /depth` set per spec). The menu
  covers the same surface area for v1.0; slash from home is a
  power-user UX layer that can land in v1.1.
- **Slash autocomplete popup in ask** (was nice-to-have per spec)
- **Re-title (Fn+R)** on the detail screen
- **Detail edit mode** — read-only for v1.0
- **Model pickers for tx_model / title_model in menu** — the
  defaults work and the values can be set via NVS if needed
- **Battery readout** (CardputerLLM's same deferral; new ADC topology
  on the ADV)
- **Pixel-smooth scroll** (we jump a row at a time; CardputerLLM has
  the same limitation)

### To verify on hardware (Phase 7 acceptance)

- [ ] Backtick from home opens the menu
- [ ] Each menu item lands on the right screen
- [ ] "add wifi" + "set api key" return to the menu cleanly with
      the body redrawn
- [ ] After "set api key" → return to home → record a new note: the
      new key is in effect for the transcribe call (verified via
      log line "[record] dispatching transcription ...")
- [ ] Splash shows v1.0

## v1.0 tag

`v1.0-ready` tagged on the final Phase 7 commit. Outstanding work
above is v1.1 territory.
