# CardputerLLM

Keyboard-first LLM terminal for the M5Stack Cardputer ADV. A pocket
device, a real QWERTY, a wire out to a model under your control.

```
    ─── CARDPUTER ────────
              L L M
    ─── phase 8 . dev ────
```

## What it is

- Streams from any OpenAI-compatible API (currently wired to OpenRouter,
  with a curated trio: GPT-5, Claude Sonnet 4.5, Gemini 2.5 Pro)
- Chat history with configurable depth (5..60 messages, persisted in NVS)
- Each session saved to SD as JSON, timestamped via NTP
- A small markup dialect the model is taught via system prompt; the
  device renders inline highlights, headers, dividers, bullets, quotes,
  code blocks, and actual filled progress bars (`[bar:60]`)
- Markdown coexistence: if the model slips and uses `**bold**` or `#`
  the renderer converts it on the fly
- First-run onboarding on-device: scan WiFi networks, paste the API key
  via a tiny HTTP form on the device's IP. No SD prep required to flash
  and run

## Status

`v1.0` — stable. Daily-driver chat UI, on-device onboarding (WiFi scan +
web key entry), markup dialect with markdown coexistence, model picker,
persistent history, slash commands with fuzzy autocomplete, polished
splash and empty-state animations.

## Screenshots

Capture on-device with `/snap`. The 240x135 BMPs land in
`/CardputerLLM/snaps/` on the SD card; copy them into
`docs/screenshots/` here and they render inline below.

<table>
<tr>
  <td><img src="docs/screenshots/splash.bmp" width="240"></td>
  <td><img src="docs/screenshots/empty.bmp" width="240"></td>
</tr>
<tr>
  <td align="center"><sub>cold boot</sub></td>
  <td align="center"><sub>empty state with rotating tips</sub></td>
</tr>
<tr>
  <td><img src="docs/screenshots/chat.bmp" width="240"></td>
  <td><img src="docs/screenshots/autocomplete.bmp" width="240"></td>
</tr>
<tr>
  <td align="center"><sub>styled chat reply</sub></td>
  <td align="center"><sub>slash autocomplete popup</sub></td>
</tr>
<tr>
  <td><img src="docs/screenshots/menu.bmp" width="240"></td>
  <td><img src="docs/screenshots/wifi.bmp" width="240"></td>
</tr>
<tr>
  <td align="center"><sub>menu</sub></td>
  <td align="center"><sub>wifi onboarding</sub></td>
</tr>
</table>

## Hardware

- M5Stack Cardputer ADV (ESP32-S3FN8 via Stamp-S3A, 8 MB flash, no PSRAM)
- 1.14" ST7789V2 IPS, 240x135
- 56-key QWERTY, TCA8418 I²C scanner (the ADV's lighter switches)
- microSD for credentials + chat history
- Runs as a Launcher app via [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)

Should also work on the original Cardputer (M5Cardputer library does
runtime board detection and swaps the keyboard reader); not verified yet.

## Build

Requires PlatformIO. From this directory:

    pio run

Output: `dist/CardputerLLM.bin` (the app-only binary that Launcher loads).

## Deploy (via Launcher)

Copy `dist/CardputerLLM.bin` to `/downloads/` on your microSD, insert,
and install via Launcher's `SD` menu. Launcher handles flashing into
the app partition.

For direct USB flash (dev only, overwrites Launcher):

    pio run -t upload

Power switch OFF, hold G0, plug USB-C, release G0 first.

## First boot (no SD prep)

The first time you run, the device walks you through setup:

1. **WiFi:** a scan list of nearby networks. Pick one, type the password
   on the Cardputer keyboard, Enter.
2. **API key:** the device shows its IP like `http://192.168.x.x`. Open
   it on your phone or laptop and paste your OpenRouter key (format
   `sk-or-v1-...`) into the small form. Submit.

Both creds are saved to `/CardputerLLM/wifi.txt` and `openrouter.txt` on
the SD. Subsequent boots are silent.

To pre-populate the SD instead, drop those files at `/CardputerLLM/`
manually. See [`include/secrets.h.example`](include/secrets.h.example)
(historical) for format; current docs in `NOTES.md`.

## In the app

| Key | Action |
|-----|--------|
| Type + Enter | send to model |
| `` ` `` or `~` | open menu (also closes / backs out) |
| `Fn+S` | open menu (alt shortcut) |
| `Fn+N` | new chat (with confirm) |
| `Fn+M` | model picker |
| `Fn+,` or `Fn+;` | scroll up (hold to repeat) |
| `Fn+.` or `Fn+/` | scroll down (hold to repeat) |
| Backspace | delete (hold to repeat), or back / close menu |

Menu items: models, new chat, history depth, system prompt, wifi info,
add wifi, set api key, diagnostics, exit.

### Slash commands (local, no API call)

| Command | What |
|---------|------|
| `/help` | list these |
| `/clear` | wipe the current conversation |
| `/demo` | render every formatting tag locally |
| `/snap` | save a BMP screenshot to SD |
| `/save` | force-save the current chat to SD |
| `/sys` | show the active system prompt |
| `/diag` | diagnostics (model, wifi, heap, etc.) |
| `/splash` | replay the boot wordmark |
| `/welcome` | replay the first-run welcome |
| `/sound on\|off` | boot chime toggle |
| `/model <name>` | switch model by label or slug |
| `/depth <n>` | set history depth (2..200) |

### Mid-stream cancel

While the assistant is streaming, press `` ` `` / `~` (Esc) or Backspace
to abort. The visible reply stops growing and is marked `[?]cancelled[/?]`.
The underlying HTTPS read finishes draining in the background (ESPAI
doesn't expose async abort), but the UI returns control immediately.

## The markup dialect

The model is taught a small set of tags via system prompt. The device
parses and renders them; markdown is converted on the fly if the model
slips.

**Inline (mid-line):**

| Tag | Render |
|-----|--------|
| `[h]hot[/h]` | yellow highlight |
| `[k]key[/k]` | dim warm label |
| `[v]value[/v]` | cream value |
| `[ok]text[/ok]` | green `+ ` prefix |
| `[w]text[/w]` | orange `! ` prefix |
| `[!]text[/!]` | red `x ` prefix |
| `[?]text[/?]` | very dim aside |

**Block (whole line):**

| Syntax | Render |
|--------|--------|
| `<<title>>` | section header with amber hairlines |
| `---` | horizontal divider |
| `- item` | bullet (tiny amber square) |
| `> quote` | left vertical bar + indented dim text |
| `[bar:NN]` | filled rectangle, NN=0..100, with percent label |
| ` ```code``` ` | code block, dim background, no wrap |

Type `/demo` in the app to see all of them at once.

## SD card layout

```
/CardputerLLM/
  openrouter.txt         single line, your API key
  wifi.txt               ssid + password pairs, line per
  system.txt             optional persona override (format tags always on)
  chats/
    YYYYMMDDTHHMMSSZ.json  timestamped sessions
```

If you delete a file, the next boot's setup flow handles the gap.

## Security note

The web form for entering the API key runs HTTP on port 80 with no auth.
It listens only while no key is present (boot) or during an explicit
"set api key" invocation from the menu. Window equals setup duration.
Do the initial setup on a network you trust.

The compiled binary contains zero credentials.

## License

[MIT](LICENSE).

## Repo

https://github.com/CHARL3X/CardputerLLM
