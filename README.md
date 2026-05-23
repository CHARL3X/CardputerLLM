# Verbatim

Voice-native note capture and retrieval for the M5Stack Cardputer ADV.
Press space, talk, get a transcript. Select notes, ask follow-up
questions with them as context. Everything lives on the SD card.

```
       V E R B A T I M
            VOX . LOG
    ─── v1.0 ─────────────
```

## What it is

- **Capture.** Press space from the home screen. Record up to 5
  minutes of speech. Press any key to stop. The device sends the
  WAV to OpenRouter's transcription endpoint, asks a fast chat
  model for a 3–6-word title, persists the result as a timestamped
  markdown file at `/Verbatim/notes/`, and deletes the WAV.
- **Browse.** The home screen IS the notes list. Newest first.
  Title, date, duration per row. Highlight a row and hit Enter to
  read it. Fn+D to delete. Fn+R later in v1.1+.
- **Ask.** Fn+S to toggle multi-select. Space toggles each row.
  Fn+A → token preflight → chat against the selected memos as
  context. Same trio of models as CardputerLLM (GPT-5,
  Claude Sonnet 4.5, Gemini 2.5 Pro). Single-session; exiting
  ask wipes the conversation but not the notes.
- **Menu.** Backtick/tilde opens the settings menu. Diagnostics,
  WiFi info, system-prompt info, "add wifi" (rerun the WiFi
  picker), "set api key" (rerun the web form), "exit."

Sibling firmware to [CardputerLLM](https://github.com/CHARL3X/CardputerLLM)
with the same cassette-futurism aesthetic, the same boot patterns,
and the same on-device WiFi + API-key onboarding. They live in
separate SD namespaces (`/Verbatim/` vs `/CardputerLLM/`) and NVS
namespaces (`verbatim` vs `cardputerllm`); neither knows the other
exists.

## Status

`v1.0` — capture → name → save → browse → ask loop is complete and
verified end-to-end on hardware. Six phases (scaffold, audio,
transcribe, title+persist, list/detail, ask). See `NOTES.md` for the
running build log including every codec gotcha, TLS stack overflow,
and gain calibration finding along the way.

## Hardware

- M5Stack Cardputer ADV (ESP32-S3FN8 via Stamp-S3A, 8 MB flash, **no PSRAM**)
- 1.14" ST7789V2 IPS, 240×135
- 56-key QWERTY, TCA8418 I²C scanner
- **ES8311 audio codec + MEMS mic** — this is the new hardware vs
  the original Cardputer
- microSD for notes + credentials
- Runs as a Launcher app via [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)

## Build

Requires PlatformIO. From this directory:

    pio run

Output: `dist/Verbatim.bin` (the app-only binary that Launcher loads).

## Deploy (via Launcher)

Copy `dist/Verbatim.bin` to `/apps/Verbatim.bin` on your microSD,
insert, and install via Launcher's `SD` menu.

For direct USB flash (dev only, overwrites Launcher):

    pio run -t upload

Power switch OFF, hold G0, plug USB-C, release G0 first.

## First boot (no SD prep)

Same flow as CardputerLLM:

1. **WiFi:** scan list, pick, type password on the Cardputer, Enter.
2. **API key:** the device shows `http://<ip>`. Open it on your
   phone or laptop and paste your OpenRouter key.

Both creds end up on the SD at `/Verbatim/wifi.txt` and
`/Verbatim/openrouter.txt`. Subsequent boots are silent.

You can also rerun either flow later from the in-app menu (backtick
or tilde key → "add wifi" / "set api key").

## In the app

### Home / notes list

| Key | Action |
|---|---|
| Space | Record a new note |
| `,` / `;` / Fn+`,` | Move highlight up |
| `.` / `/` / Fn+`.` | Move highlight down |
| Enter | Open highlighted note |
| Fn+D | Delete highlighted note (with confirm) |
| Fn+S | Enter multi-select |
| Fn+A | Ask mode with highlighted (or selected) notes |
| `` ` `` / `~` | Open the settings menu |

### Multi-select mode

| Key | Action |
|---|---|
| Space | Toggle selection on highlighted row |
| Arrows | Move highlight |
| Enter | Open highlighted |
| Fn+A | Ask with current selection |
| Fn+S | Exit multi-select |
| Backspace | Exit multi-select |

### Note detail

| Key | Action |
|---|---|
| Fn+`,` / Fn+`.` | Scroll up / down |
| Fn+D | Delete this note (with confirm) |
| Backspace | Back to list |

### Ask mode

| Key | Action |
|---|---|
| Type + Enter | Send to model |
| Fn+M | Open model picker |
| Fn+`,` / Fn+`.` | Scroll conversation |
| `` ` `` / `~` / Backspace during stream | Cancel |
| Backspace from empty input | Exit ask, return to list |

Slash commands in ask: `/help`, `/clear`, `/diag`, `/snap`.

### Menu (backtick / tilde from home)

`diagnostics`, `wifi info`, `system prompt`, `add wifi`, `set api key`, `exit`.

## SD card layout

```
/Verbatim/
  openrouter.txt        single line, your OpenRouter API key
  wifi.txt              ssid + password pairs, line per
  system.txt            optional ask-mode persona override
  notes/
    20260523T143211Z-cardputer-firmware-ideas.md
    ...
  snaps/                BMP screenshots from /snap
  .recording.wav        transient during capture (deleted after save)
```

Each note `.md` is YAML frontmatter + transcript body:

```
---
created: 2026-05-23T14:32:11Z
title: Cardputer firmware ideas
duration_sec: 47
model_transcribe: openai/whisper-large-v3
model_title: google/gemini-2.5-flash
---

Hey, so I've been thinking about ...
```

The body is plain prose; no inline tags. Frontmatter is read by the
list view and the ask-mode context builder.

## Defaults

- Transcription: `openai/whisper-large-v3`
- Title generation: `google/gemini-2.5-flash`
- Ask mode (in-session pickable): `openai/gpt-5`,
  `anthropic/claude-sonnet-4.5` (default), `google/gemini-2.5-pro`

Transcription + title models persist via NVS (`verbatim` namespace,
keys `tx_model` / `title_model`). Ask-mode model is in-session only.

## Security note

The API-key web form runs HTTP on port 80 with no auth. It listens
only while no key is present (boot) or during an explicit "set api
key" invocation from the menu. Do the initial setup on a network
you trust.

The compiled binary contains zero credentials.

## License

MIT.
