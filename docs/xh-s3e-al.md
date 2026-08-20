# XH-S3E-AL V1.0 (`lugo-s3-xhs3e`)

A compact ESP32-S3-WROOM-1 board with the **microphone and class-D amplifier
fitted on the board**. It is the first board here that is not a devkit you wire
up: the audio I2S lines never reach a header, so there is nothing to solder on
the audio side and nothing to get wrong there either — but also no way to read
the pinout off the silkscreen.

## What the header exposes

```
TXD  RXD  GND  VIN  IO41/SDA  IO42/SCL  3V3  GND      + a speaker pad pair
```

That is the whole pin budget. Consequences that shape `board_def.c`:

| | |
|---|---|
| **Display** | SSD1306 on IO41 (SDA) / IO42 (SCL). ST7789 is impossible — it needs DC/RST/BL and there are no pins left, so `display_cfg.st7789 = NULL` and `display_init()` probes I2C only. |
| **Speaker** | A bare speaker on the pad pair. The amp is onboard; do **not** also fit a MAX98357A module. |
| **Buttons** | BOOT (GPIO0) only. It is RTC-capable, so deep-sleep wake and the 10s wake-hold setup portal both work. |
| **Volume** | No hardware control at all — MCP / web UI only. `vol_up`/`vol_down`/`emotion` are all `-1`. |

## Audio pins: confirmed by the vendor config

The vendor ships this board as the xiaozhi-esp32 board **`bread-compact-wifi-lcd`**,
and that config.h pins the audio down exactly:

```
mic  WS=4   SCK=5   DIN=6     (I2S_NUM_0, simplex)
amp  DOUT=7 BCLK=15 LRCK=16   (I2S_NUM_1, simplex)
BOOT button = GPIO0,  no volume buttons (GPIO_NUM_NC)
```

Same map as every 41/42-display board in the [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
tree (`bread-compact-wifi`, `bread-compact-ml307`, `bread-compact-nt26`,
`hu-087`, `nologo/xingzhi-cube-0.96oled`), so there is nothing left to guess here.

The vendor board `.cc` also settles a question by omission: it builds the codec
from those six pins and nothing else, so **there is no PA-enable GPIO** gating
the amplifier. (Some integrated boards do have one — `hu-087` gates its amp on
GPIO17 — which would make a correctly-wired speaker silent forever. Not this
board.)

All of it is now confirmed on hardware. The loopback self-test
(`CONFIG_AA_AUDIO_LOOPBACK=y`, BOOT on GPIO0) records real varying audio and
plays it back audibly through the onboard amp:

```
selftest: take buffer: 10s in PSRAM (312 KB)
selftest: rec frame: got=960 stored=960 peak=12308 ... peak=25476 ... peak=32768
selftest: stopped: buffer full (10s cap)
selftest: playback: 160000 samples (10.00s), peak=32768
```

The mic sits in the **left** I2S slot (`mic_cfg.right_slot = false`) — a wiring
choice no vendor config records, so it had to be measured. If a revised board
ever reads silent, flip that before suspecting pins: the wrong slot yields a
channel of pure zeros that looks exactly like a dead mic.

## The display: silkscreen wins over the vendor config

The vendor config drives an **SPI panel** — MOSI=12, CLK=10, DC=8, CS=11 — and
none of those pins reach this board's header. What the header exposes is an
**I2C pair silkscreened IO41/SDA and IO42/SCL**, the wiring of the *non*-LCD
`bread-compact-wifi` config, which is what an SSD1306 wants.

Confirmed on hardware at first boot:

```
display: i2c device @ 0x3C (scl=42 sda=41)
display: display ready (ssd1306 i2c 128x64)
```

Driving the vendor's SPI panel instead would need more than new pin numbers:
`display_st7789_cfg_t` has no CS field and that config uses one, so the ST7789
path would have to grow chip-select support first.

## First boot, confirmed

```
board: lugo-s3-xhs3e (registered=1)
esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator
spi_flash: detected chip: boya      flash io: qio
display: display ready (ssd1306 i2c 128x64)
i2s_mic: mic ready (left slot)
i2s_speaker: speaker ready
```

So the module is genuinely N16R8 (8MB **octal** PSRAM — the overlay inheriting
the S3 default is right), the port enumerates as `/dev/cu.usbmodem*` so the
native USB-Serial-JTAG console is right, and the OLED pins are right.

`mic ready` / `speaker ready` only mean the I2S channels came up on those pins;
the loopback self-test above is what proves audio actually flows. The full
pipeline — WiFi, gateway, speak-and-get-a-reply — has since been exercised on
this board too.

## Building

The module is N16R8 (16MB flash, 8MB **octal** PSRAM), so it needs its own
overlay for the flash size — the octal PSRAM setting is inherited from
`sdkconfig.defaults.esp32s3` and is already correct.

```sh
idf.py -B build-xhs3e -DSDKCONFIG=sdkconfig.xhs3e \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3xhs3e" \
       build flash monitor
```

Both of that overlay's assumptions held up on hardware — the PSRAM really is
octal (`Adding pool of 8192K`) and the console really is the native
USB-Serial-JTAG. If a later unit ever ships with an R2/quad module instead, the
symptom is loud in its silence: an octal init against a quad part **hangs the
bootloader before `app_main`**, with no log at all. The fix is
`CONFIG_SPIRAM_MODE_QUAD=y` in the overlay, not a conclusion that the board is
dead.
