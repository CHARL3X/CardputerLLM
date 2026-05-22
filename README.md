# CardputerLLM (placeholder name)

Keyboard-first LLM client for the M5Stack Cardputer ADV. The keyboard is
the feature. The device is a pocket terminal for talking to large language
models over a wire you control. Not a chatbot toy.

## Status

Phase 2 of 9: ESPAI proof against OpenRouter. Boots, connects WiFi, fires
one BasicChat then one StreamingChat against `openai/gpt-4o-mini`, prints
results to USB serial and streams tokens to the display body. No chat UI
or keyboard input yet.

## Secrets

Phase 2+ needs credentials. Edit `include/secrets.h` (gitignored) and
set:

    #define WIFI_SSID          "your-wifi"
    #define WIFI_PASSWORD      "your-password"
    #define OPENROUTER_API_KEY "sk-or-v1-..."

Template lives at `include/secrets.h.example`. Do not commit real keys.

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
