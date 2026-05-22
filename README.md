# CardputerLLM (placeholder name)

Keyboard-first LLM client for the M5Stack Cardputer ADV. The keyboard is
the feature. The device is a pocket terminal for talking to large language
models over a wire you control. Not a chatbot toy.

## Status

Phase 2 of 9: ESPAI proof against OpenRouter. Boots, connects WiFi, fires
one BasicChat then one StreamingChat against `openai/gpt-4o-mini`, prints
results to USB serial and streams tokens to the display body. No chat UI
or keyboard input yet.

## First boot (no SD prep)

You can flash and run without preparing the SD at all. On first boot
the device will:

1. Show a WiFi scan, you pick a network and type the password on the
   Cardputer keyboard.
2. After WiFi connects, show a URL like `http://192.168.x.x`. Open it
   on your phone or computer and paste your OpenRouter key into the form.
3. Both creds are saved to `/CardputerLLM/wifi.txt` and
   `/CardputerLLM/openrouter.txt` automatically.

Subsequent boots are silent: stored creds load, device associates,
chat is ready.

## Credentials live on the SD card, not in the binary

The compiled `dist/CardputerLLM.bin` contains no credentials. Dump the
flash and you get nothing.

If you'd rather pre-populate the SD instead of using the setup UI:

`/CardputerLLM/openrouter.txt` (one line):

    sk-or-v1-...

`/CardputerLLM/wifi.txt` (ssid then password, one per line; pairs tried
in order; lines starting with `#` are ignored):

    MyHomeNetwork
    mypassword
    BackupNetwork
    backuppassword

`dist/sd/CardputerLLM/` is a local staging area (gitignored) if you
want to mirror files onto the card.

## Changing creds later

From the chat screen, press `Fn+S` to open the menu:
- "add wifi" runs the same scan/pick/password flow and appends to wifi.txt
- "set api key" spins up the same web form so you can paste a new key

## Trust note for the web form

The key entry page is HTTP on port 80 with no auth. It only runs while
no key is present (boot) or while you explicitly invoked "set api key"
(transient). Anyone on the same LAN during that window could submit a
key. Acceptable for personal use; do the initial setup on a network
you control.

## Build

PlatformIO required. From this directory:

    pio run

## Deploy via Launcher (primary path)

The Cardputer ADV runs [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)
as its persistent firmware. Each `pio run` drops an app-only binary at:

    dist/CardputerLLM.bin

Copy that file to the microSD card, insert into the Cardputer, and use
Launcher's `SD` menu to install it. Launcher copies it into the device's
app partition and launches it.

SD card requirements (from Launcher docs):

- SDHC, not SDXC
- 32 GB or smaller
- FAT32
- MBR partition scheme

## Monitor

    pio device monitor

USB CDC at 115200. Echo prints each pressed character; Enter prints the
full buffered line as `[line] ...`.

## Direct USB flash (alternative, dev-only)

If you ever want to bypass Launcher and flash this build directly:

1. Power switch OFF.
2. Hold the G0 button on the back.
3. Plug USB-C.
4. Release G0. Device is in download mode.
5. `pio run -t upload`

This will overwrite Launcher. Not the normal workflow.

## Name candidates

Three to pick from. Charles picks.

- **Telex** — electromechanical typed messaging, point to point. The
  metaphor for "talking to an LLM over a wire" is exact.
- **Slate** — clean, terminal, fits the hand.
- **Margin** — the conversation happens in the margins of whatever else
  you're doing.

## License

TBD.
