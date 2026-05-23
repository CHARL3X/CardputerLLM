# Cardputer

Two pocket terminals fused into one firmware. Boot, get a sleek splash,
pick a mode — **CardputerLLM** (chat with any LLM) or **Verbatim**
(voice notes + ask). One binary, one set of credentials, one SD card.

```
       C A R D P U T E R
        L L M . V O X
    ─── v1.0 ─────────────
```

## What it is

- **One firmware**, two apps. Splash plays the chime, then the mode
  picker lets you choose CardputerLLM or Verbatim. Reboot to switch;
  the picker defaults to whichever mode you ran last.
- **Shared onboarding.** WiFi setup + OpenRouter API key live at
  `/Cardputer/wifi.txt` and `/Cardputer/openrouter.txt`. Set them
  once and both modes use them.
- **Co-resident data.** LLM chats live under `/Cardputer/chats/`,
  Verbatim notes under `/Cardputer/notes/`, screenshots under
  `/Cardputer/snaps/`.

Merges the two standalone apps in this repo: CardputerLLM and
Verbatim. Their full design intent and history are preserved in
their individual repos; this firmware is the union.

## Status

`v1.0-merged` — every Phase from both prior apps is included. See
`NOTES.md` for the merge log + everything new.

## Hardware

- M5Stack Cardputer ADV (ESP32-S3FN8, 8 MB flash, **no PSRAM**)
- 1.14" ST7789V2 IPS, 240×135
- 56-key QWERTY, TCA8418 I²C scanner
- ES8311 audio codec + MEMS mic
- microSD for credentials + notes + chats
- Runs as a Launcher app via [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)

## Build

```
pio run
```

Output: `dist/Cardputer.bin`.

## Deploy

Copy `dist/Cardputer.bin` to `/apps/Cardputer.bin` on your microSD and
install via Launcher's `SD` menu.

For direct USB flash (overwrites Launcher):

```
pio run -t upload
```

Power switch OFF, hold G0, plug USB-C, release G0 first.

## First boot

If `/Cardputer/wifi.txt` and `/Cardputer/openrouter.txt` are empty:

1. **WiFi**: scan list, pick, type password, Enter.
2. **API key**: device shows `http://<ip>` — open it on your phone /
   laptop and paste your OpenRouter key.
3. **Welcome screen** (one-shot per device).
4. **Mode picker** — pick CardputerLLM or Verbatim and launch.

Subsequent boots go straight to splash → mode picker. The picker
defaults the highlight to whatever you ran last.

## In the app

### Mode picker

| Key | Action |
|---|---|
| `,` / `;` | Highlight up (CardputerLLM) |
| `.` / `/` | Highlight down (Verbatim) |
| `1` / `2` | Jump directly |
| Enter | Launch the highlighted mode |

### CardputerLLM mode

Full chat terminal with the trio (GPT-5 / Sonnet 4.5 / Gemini 2.5
Pro), slash commands, model picker, history persistence under
`/Cardputer/chats/`. See the [CardputerLLM repo](https://github.com/CHARL3X/CardputerLLM)
for the full keymap.

### Verbatim mode

Voice notes + ask mode. Press SPC to record. Transcripts get titled
by Gemini Flash and saved to `/Cardputer/notes/<ts>-<slug>.md` as
YAML-frontmatter markdown. Multi-select notes (Fn+S → SPC per row)
and Fn+A to ask follow-up questions with them as context.

## SD card layout

```
/Cardputer/
  openrouter.txt           single line, OpenRouter API key
  wifi.txt                 ssid + password line pairs
  system.txt               optional shared system prompt override
  chats/                   LLM chat sessions (.json)
  notes/                   Verbatim transcripts (.md)
  snaps/                   BMP screenshots
```

## NVS

Namespace `cardputer`. Shared keys (`welcomed`, `bootsound`,
`last_mode`) plus per-mode keys (`histdepth` for LLM, `askdepth` +
`tx_model` + `title_model` for Verbatim). Independent of the
`cardputerllm` and `verbatim` namespaces used by the standalone
builds, so flashing this firmware on a device that previously ran
either standalone app starts fresh.

## License

MIT.
