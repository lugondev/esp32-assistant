# Wokwi simulation

Lets you boot this firmware in [Wokwi](https://wokwi.com) (VS Code extension or
`wokwi-cli`) without real hardware. Useful for board-select, display, WiFi, and
button logic. **Not** useful for voice — see [Limitations](#limitations).

## Files

| File | Purpose |
|---|---|
| `wokwi.toml` | Points Wokwi at `build-wokwi/` (a separate build dir from real-hardware `build/`) and registers the two custom chips. |
| `diagram.json` | Wiring: ESP32-S3-DevKitC-1 + SSD1306 OLED (I2C) + INMP441/MAX98357A pin stubs + 4 buttons (wake/vol-up/vol-down/emotion), each active-low to GND against the firmware's internal pull-up. |
| `sdkconfig.wokwi` | Wokwi-only Kconfig overrides, layered on top of `sdkconfig.defaults(.esp32s3)`. Never used for real hardware. |
| `inmp441.chip.c` / `.chip.json` | Custom-chip pin stub for the mic. Carries correct pin names so wiring renders; logs once when WS starts toggling. No real I2S decode (Wokwi's chip API doesn't support it). |
| `max98357a.chip.c` / `.chip.json` | Same, for the speaker. |
| `.vscode/launch.json` | GDB debug config wired to `wokwi.toml`'s `gdbServerPort`. `miDebuggerPath` is a hardcoded path to this machine's `xtensa-esp32s3-elf-gdb` — adjust if the ESP-IDF tools install location differs. |

`build-wokwi/`, `sdkconfig.wokwi.generated`, `*.chip.wasm`, and `wokwi-api.h` are
gitignored (generated/build output).

## Building for Wokwi

Real-hardware `build/` + `sdkconfig` are untouched by any of this — Wokwi uses
a fully separate build dir and sdkconfig path.

One-time setup (already done; only needed again if `build-wokwi/` is deleted):

```bash
idf.py -B build-wokwi -D SDKCONFIG=sdkconfig.wokwi.generated \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.wokwi" \
  set-target esp32s3
```

After any firmware change, rebuild before testing in Wokwi:

```bash
idf.py -B build-wokwi build
```

## Running

**VS Code**: open this folder, `F1` → `Wokwi: Start Simulator`.

**CLI** (no VS Code needed — useful for quick checks or from an agent):

```bash
WOKWI_CLI_TOKEN=<your token from https://wokwi.com/dashboard/ci> \
  wokwi-cli . --timeout 20000
```

Install `wokwi-cli` with `curl -L https://wokwi.com/ci/install.sh | sh` if not
already on PATH.

## Custom chips

`chip-inmp441` / `chip-max98357a` are **not** built into Wokwi — they're
project-local custom chips (Wokwi's Chips API), compiled to WASM:

```bash
wokwi-cli chip compile inmp441.chip.c -o inmp441.chip.wasm
wokwi-cli chip compile max98357a.chip.c -o max98357a.chip.wasm
```

Re-run this if you edit either `.chip.c`. `wokwi.toml`'s `[[chip]]` blocks map
`type: "chip-<name>"` in `diagram.json` to the compiled `<name>.chip.wasm` —
without that mapping, Wokwi shows "Missing chip Breakout" for the part.

## Limitations

Found the hard way this session — see git history / conversation for the full
debugging trail if any of this needs revisiting:

- **No I2S support in Wokwi, at two separate levels:**
  1. Wokwi's Chips API (used to write `inmp441.chip.c`/`max98357a.chip.c`) has
     no I2S primitives — open feature request wokwi-features #213. The two
     custom chips above are pin stubs only; they cannot decode real audio.
  2. The ESP32-S3's own **built-in** I2S HAL hangs in Wokwi's simulator:
     `i2s_channel_enable()` polls a "clock ready" hardware flag that Wokwi
     never sets, so `mic_init()`/`speaker_init()` block forever on boot (task
     watchdog trips ~5s in). Confirmed via task-watchdog backtrace +
     `addr2line`, not a guess.
  - **Workaround**: `CONFIG_AA_SKIP_AUDIO_INIT` (Kconfig, set in
    `sdkconfig.wokwi` only) makes `audio_init()` skip the real hardware bring-up;
    `audio_mic_read()`/`audio_spk_write()`/volume functions become no-ops so the
    rest of `app_main()` (WiFi, display, buttons, gateway WS) still runs.
    Real hardware is unaffected — the flag defaults off and isn't set in the
    shared `sdkconfig`.
  - **Practical effect**: mic capture, Opus encode/decode, and speaker
    playback do not work in Wokwi at all. Voice round-trip testing (the
    device's actual purpose) can only be done on real hardware.

- **No ST7789 element in Wokwi.** Checked the official
  [`wokwi/wokwi-elements`](https://github.com/wokwi/wokwi-elements) source —
  only `wokwi-ili9341` exists for SPI color TFTs, no ST7789. `diagram.json`
  uses `wokwi-ssd1306` (I2C OLED) instead. `sdkconfig.wokwi` sets no
  `CONFIG_AA_BOARD_*`, so the simulator builds the esp32s3 default board,
  `lugo-s3-nx`; that board declares `.display = NULL`, so `display_init()`
  probes its shared clock/data pins, finds the simulated SSD1306, and takes the
  OLED branch. The ST7789 branch of the same board has no element to probe
  against and can only be verified on real hardware.

- **The AP-provisioning flow can't be tested end-to-end.** When WiFi STA
  connect fails, the firmware brings up its own SoftAP (`Lugo-XXXX`) for
  captive-portal provisioning. That AP only exists inside Wokwi's virtual
  network sandbox — it is not bridged to a real WiFi radio, so a phone/laptop
  can never see or join it. Confirming the firmware *enters* that state is all
  Wokwi can do here.

- **STA WiFi connect does work**, using Wokwi's fixed virtual AP
  (`Wokwi-GUEST`, open, no password — see
  [Wokwi's ESP32 WiFi guide](https://docs.wokwi.com/guides/esp32-wifi)).
  `CONFIG_AA_WIFI_SSID`/`CONFIG_AA_WIFI_PASS` are set to `"Wokwi-GUEST"`/`""`
  in `sdkconfig.wokwi` only, so `wifi_cfg_load()`'s NVS-empty fallback resolves
  to Wokwi's network in simulation. **This simulation is the only reason those
  two options still exist** — they are labelled SIMULATOR ONLY in Kconfig, and
  on hardware an empty SSID is what sends first boot straight to the setup
  portal.

## What's actually worth using this for

Given the limitations above, treat this as a narrow tool, not a hardware
substitute:

- Regression-checking `board_def.c`/board-select changes without needing a
  physical board on your desk.
- Iterating on display/HUD/robot-eyes rendering logic.
- Checking the WiFi connect → WS client → button state-machine flow.

Not for: anything about the actual voice experience (mic sensitivity, Opus
quality, gateway round-trip latency/behavior, speaker output) — that's real
hardware only.
