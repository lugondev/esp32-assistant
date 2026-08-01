# ESP32-C3 SuperMini port (`lugo-c3-supermini`)

A reduced port of the assistant firmware onto an **ESP32-C3 SuperMini** dev board
with an SSD1306 0.96" OLED (I2C), MAX98357A speaker + INMP441 mic on the single
full-duplex I2S controller. Wiring diagram: `img/c3-supermini-oled-ssd1306.png`.

Build & flash (separate build dir so the S3 build is untouched):

```sh
idf.py -B build-c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=sdkconfig.c3 build flash monitor
```

The C3 board is force-selected via `CONFIG_AA_BOARD_LUGO_C3_SUPERMINI=y`
(`sdkconfig.defaults.esp32c3`).

## Pin map (matches the wiring diagram)

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | Wake button | RTC-wake capable (GPIO0–5) |
| 1 | mic SCK | I2S BCLK fanned out here (see below) |
| 2 | mic WS | I2S WS fanned out here — **strapping pin**, OK for prototype |
| 3 | speaker LRC (WS) | primary I2S WS |
| 4 | OLED SDA | |
| 5 | OLED SCL | |
| 6 | speaker DIN | I2S data-out |
| 7 | speaker BCLK | primary I2S BCLK |
| 8 | *(free)* | **onboard LED** (active-low) + strapping — leave free |
| 9 | *(free)* | **BOOT button** + strapping — leave free |
| 10 | mic SD | I2S data-in |
| 18, 19 | — | **USB D-/D+, not broken out** — never use for peripherals |

## Chip constraints vs the S3 build

- **Single core @ 160MHz** (no 240MHz option). Audio tasks fall back to
  `tskNO_AFFINITY` (`main.c`). Opus `COMPLEXITY(3)`/24kbps/16kHz is unchanged;
  watch the `opus_encode new max … us / 60000` log for the per-frame budget.
- **No PSRAM.** The audio packet queues fall back from `MALLOC_CAP_SPIRAM` to
  internal RAM (`main.c`) instead of aborting at boot.
- **One I2S controller** (S3 has two). `i2s_fd` runs mic+speaker full-duplex on
  one controller sharing BCLK+WS. The mic here is wired to its *own* SCK/WS pins
  (1/2), so the shared clock is **fanned out** onto them via the GPIO matrix
  (`i2s_fd_cfg_t.mic_bclk/mic_ws`). Set those to `-1` if the mic instead shares
  the speaker's clock pins physically.

## I2C / OLED

The SSD1306 runs at **100kHz** (not 400) — the bus is marginal with only the
ESP internal pull-ups (~45kΩ). **Add external 4.7kΩ pull-ups on SDA(4)/SCL(5)
to 3V3** for reliability. Display init is non-fatal: on I2C failure the device
logs an i2c bus scan and runs **headless** (show/flush become no-ops) instead of
boot-looping.

## ⚠️ WiFi: known SuperMini antenna defect

The C3 SuperMini's onboard SMD antenna is undersized and mis-placed (too close to
the ground plane, not at the vendor-recommended spot) — a documented flaw across
the whole board line (Hackaday, Elektor). On the test unit:

- **RX works**: it finds a 2.4GHz router at RSSI **-59 dBm** (good).
- **TX is crippled**: STA gets stuck at authentication (disconnect reason 2/3,
  before the WPA2 password is ever checked), and its SoftAP `Lugo-XXXX` beacon is
  not findable by any client — the router/phone can't hear the C3's transmit.

Confirmed **not** a firmware issue (reproduced with audio+display+I2C all disabled
and after erasing flash for a fresh PHY calibration). **Fix is hardware:** solder
a ~31mm λ/4 wire to the antenna feed point, or use a C3 board with an external
IPEX antenna. WiFi provisioning is otherwise standard: SoftAP `Lugo-XXXX` (open,
192.168.9.1) — power on, wait ~16s, join, enter a **2.4GHz** SSID.

## Bring-up toggles (Kconfig, default off)

- `CONFIG_AA_SKIP_DISPLAY_INIT` — skip OLED bring-up (no I2C), run headless.
- `CONFIG_AA_SKIP_AUDIO_INIT` — skip mic/speaker I2S init.
- `CONFIG_AA_WIFI_SSID` / `CONFIG_AA_WIFI_PASS` — **simulator only.** These
  bake a credential into the image; on hardware use the setup portal instead,
  which now lists nearby networks to pick from.

Handy for isolating a subsystem during hardware bring-up. Upstream reference for
the C3 assistant design: <https://github.com/78/xiaozhi-esp32>.
