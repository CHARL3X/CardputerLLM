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

### To verify on hardware (deferred to later phases)

- Battery percentage readout math on the ADV (1750mAh, different topology
  from original Cardputer). Launcher v2.6.5 fixed Cardputer battery ADC;
  ADV may need its own correction.
- IR LED on GPIO 44.
- ES8311 audio init quirks (shared I2C bus with TCA8418).
- Whether ESPAI streams reliably over WiFi on ESP32-S3 with TLS to
  OpenRouter (Phase 2 gate; HIGH RISK).
