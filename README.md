# CardputerLLM (placeholder name)

Keyboard-first LLM client for the M5Stack Cardputer ADV. The keyboard is
the feature. The device is a pocket terminal for talking to large language
models over a wire you control. Not a chatbot toy.

## Status

Phase 1 of 9: toolchain proof. Hello on screen, keyboard echoes to USB
serial and to the input row. No network, no LLM yet.

## Build

PlatformIO required. From this directory:

    pio run

## Flash

1. Power switch OFF.
2. Hold the G0 button on the back.
3. Plug USB-C.
4. Release G0. Device is in download mode.
5. `pio run -t upload`

## Monitor

    pio device monitor

USB CDC at 115200. Echo prints each pressed character; Enter prints the
full buffered line as `[line] ...`.

## SD-card delivery (eventual)

The device will run [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher)
as the persistent firmware. App `.bin` files go on the SD card and Launcher
loads them into the running app partition. The build target is therefore
an app-only `.bin`, not a merged bootloader+partitions blob.

For Phase 1 we flash directly over USB to confirm the toolchain.

## Name candidates

Three to pick from. Charles picks.

- **Telex** — electromechanical typed messaging, point to point. The
  metaphor for "talking to an LLM over a wire" is exact.
- **Slate** — clean, terminal, fits the hand.
- **Margin** — the conversation happens in the margins of whatever else
  you're doing.

## License

TBD.
