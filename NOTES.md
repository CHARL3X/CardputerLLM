# NOTES

Running log of empirical findings. Append; do not rewrite history.

## Phase 1: toolchain

### Decisions

- PlatformIO env: `cardputer-adv`. Board: `esp32-s3-devkitc-1`. Framework: arduino.
- ESP32-S3FN8 is the no-PSRAM variant. Did NOT enable PSRAM. 8MB flash.
- Did not pin platform version. Started with the registry default for
  `platform = espressif32`. If a build breaks because M5Cardputer needs an
  older Arduino-ESP32, fall back to `espressif32 @ 6.7.0` (cited by M5).
- Partition scheme: `default_8MB.csv` for the direct-flash dev build.
  This is irrelevant for the eventual Launcher SD delivery (Launcher's
  own partition table runs on the device; our app drops into its app slot).
- M5 build flags applied verbatim from the M5 docs:
  `-DESP32S3 -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`.

### M5Cardputer / ADV facts that affect the code

- One library covers both Cardputer and Cardputer ADV. The library calls
  `M5.getBoard()` at `Keyboard.begin()` time and injects either an
  `IOMatrixKeyboardReader` (original, GPIO polled) or a
  `TCA8418KeyboardReader` (ADV, I2C 0x34 on internal bus, INT on GPIO 11,
  interrupt driven). Public API is identical: `isChange()`, `keysState()`,
  `isPressed()`, `isKeyPressed()`, `keyList()`. User code does not branch.
- `M5Cardputer.update()` calls `M5.update()` then, if keyboard enabled,
  `Keyboard.updateKeyList()` and `Keyboard.updateKeysState()`. Per the
  TCA8418 reader, `updateKeyList()` is a no-op on ADV unless the ISR set
  the flag, so polling cost is essentially zero.
- `keysState().word` is `std::vector<char>` of the printable characters
  currently pressed, with shift/capslock already applied via the
  `_key_value_map[4][14]` table. Modifier keys (FN, SHIFT, CTRL, OPT, ALT)
  are exposed as bools and never appear in `word`. Backspace and Enter
  are also flags (`del`, `enter`), not in `word`.

### Bootloader entry

Power switch OFF, hold G0, plug USB-C, release G0.

### To verify on hardware (Phase 1)

- That a single keypress produces exactly one char in `state.word`, not a
  doubled echo. The lighter 160gf ADV switches may be debounce-sensitive
  relative to the original Cardputer.
- That holding a key does not auto-repeat (or that it does, with what
  cadence). The TCA8418 has hardware repeat we did not configure; default
  should be off.
- That `M5.getBoard()` returns `board_M5CardputerADV` on this device.
  Logged at boot for inspection.
- That `setRotation(1)` is the correct orientation with the keyboard
  facing the user.
- That USB CDC enumerates and shows up as a serial device immediately
  on Phase 1 boot (the `DARDUINO_USB_CDC_ON_BOOT=1` flag should ensure it).

## Phase 2 (with phase-3 SD config absorbed): ESPAI proof against OpenRouter

### Decisions

- Provider: `OpenAICompatibleProvider` pointed at
  `https://openrouter.ai/api/v1/chat/completions` with `openai/gpt-4o-mini`
  as the test model. Slugs are finalized in Phase 7.
- Credentials are NOT compiled into the binary. They live on the SD card
  as plain text:
    - `/openrouter.txt` (single line, the API key)
    - `/wifi.txt` (ssid, password, ssid, password, ... line pairs)
  Charles asked to skip the secrets.h hardcode step. The Phase 3 SD-config
  work is absorbed into Phase 2 since the same `sdcfg` module serves both.
- Multiple SSIDs supported. Connects to the first one in `/wifi.txt` that
  associates, with a 12s timeout per candidate.
- Phase 2 main.cpp does NOT enable the M5Cardputer keyboard. Pure
  network-and-display proof; keyboard wakes up in Phase 5.
- Test sequence: BasicChat then StreamingChat. Both print full response,
  token counts, timing, and any error code to USB serial; tokens also
  stream to the display body so we can confirm both paths visually.
- SD card SPI pins (M5Cardputer / ADV docs): SCK G40, MISO G39, MOSI G14,
  CS G12, 25 MHz. Lives on a different SPI bus than the display, no
  contention.
- Flash usage 1.13MB now (was 454kB in phase 1). Jump is from pulling in
  WiFi, mbedTLS, SD/FAT, ESPAI's HTTP+SSE stack.

### Local staging area

`dist/sd/` holds the credential files that get copied onto the device's
SD card alongside `dist/CardputerLLM.bin`. It is gitignored. Plaintext
secrets in there; treat the folder accordingly.

### To verify on hardware

- WiFi associates with the user's network at 2.4 GHz. The Cardputer ADV
  has no 5 GHz radio.
- TLS handshake to `openrouter.ai` succeeds. If it fails with cert errors,
  ESPAI's bundled root CA bundle is either missing the right one or has
  stale roots. Workarounds: `setInsecure()` for the test, or add a fresh
  ISRG Root X1 cert. Document the path taken in this file.
- `chat()` returns non-empty content with `success=true`.
- `chatStream()` invokes the callback multiple times with `done=false`
  followed by exactly one `done=true`. Time to first token under 3s on a
  decent network is expected; longer is a red flag.
- No heap exhaustion mid-stream. The ESP32-S3FN8 has no PSRAM; if we OOM
  during streaming we'll need to either chunk-process tokens immediately
  without buffering, or move to the PSRAM variant of the board.

### Pivot trigger

If TLS won't handshake OR `chatStream` returns false consistently OR the
callback never fires with content: stop. Tag the commit
`phase-2-streaming-failed`. Pivot to a hand-rolled SSE client using
`WiFiClientSecure` and an `ArduinoJson` streaming parser. Per spec, do
NOT silently fall back to non-streaming.

## Phase 3+ deferrals

- Battery percentage readout math on the ADV (1750mAh, different topology
  from original Cardputer). Launcher v2.6.5 fixed Cardputer battery ADC;
  ADV may need its own correction.
- IR LED on GPIO 44.
- ES8311 audio init quirks (shared I2C bus with TCA8418).
