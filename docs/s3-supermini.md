# ESP32-S3 SuperMini port (`lugo-s3-supermini`)

The assistant firmware on an **ESP32-S3 SuperMini** (ESP32-S3FH4R2 — 4MB
in-package flash, 2MB **quad** PSRAM) with an SSD1306 0.96" OLED (I2C),
MAX98357A speaker and INMP441 mic on the S3's two I2S controllers.

Feature-wise this is the full S3 build — dual-core 240MHz, PSRAM, dual I2S, so
none of the [C3 SuperMini](c3-supermini.md) compromises apply. What differs from
`lugo-s3-nx` is the pinout and the module: **4MB/quad**, not 8MB/octal.

Build & flash (separate build dir + sdkconfig so the NX build is untouched):

```sh
idf.py -B build-s3sm -DSDKCONFIG=sdkconfig.s3sm \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.s3supermini" \
       build flash monitor
```

`sdkconfig.defaults.s3supermini` layers on top of `sdkconfig.defaults.esp32s3`
and overrides only the module-specific settings (flash size, PSRAM mode, board
select, console); the S3-wide tuning — 240MHz, 32KB I-cache / 64KB D-cache with
64B lines, QIO flash — is inherited. Pass the same two flags on every subsequent
`idf.py` invocation for this board, including `menuconfig`.

> Adding a board directory also means adding its `choice AA_BOARD` entry in
> `main/Kconfig.projbuild`; the first build after that needs `idf.py reconfigure`
> (or `fullclean`) so the new `CONFIG_AA_BOARD_NAME` value is picked up.

## Pin map

| GPIO | Header | Function | Notes |
|------|--------|----------|-------|
| 1 | `GP1` | speaker LRC (I2S WS) | MAX98357A |
| 2 | `GP2` | OLED SCL | SSD1306, I2C @ 0x3C |
| **4** | `GP4` | **UNUSABLE** | **shorted to ground on this unit — see below** |
| 5 | `GP5` | mic SCK (I2S BCLK) | INMP441 |
| 6 | `GP6` | mic SD | INMP441 I2S data-in |
| 7 | `GP7` | wake button | RTC-capable (GPIO0–21) → deep-sleep wake works |
| 8 | `GP8` | mic WS | INMP441 |
| 12 | `GP12` | speaker DIN | MAX98357A I2S data-out |
| 13 | `GP13` | OLED SDA | SSD1306 |
| 44 | `RX` | speaker BCLK | UART0 RX — see below |
| 43, 3, 9, 10, 11 | `TX`, `GP3`… | *(free)* | GPIO3 is a strapping pin (JTAG select) |
| 19, 20 | — | — | **native USB D-/D+, not broken out** — never use |

This board's INMP441 has **`L/R` tied high**, so it drives the **right** I2S
slot and `mic_cfg` sets `.right_slot = true`. That is the opposite of
`lugo-s3-nx`. Reading the wrong slot is not an error — it returns a channel of
zeros that is indistinguishable from a dead microphone, which is why the driver
now logs `mic ready (right slot)` at boot. If you rewire `L/R` to GND, drop the
`.right_slot` line.

Also wire INMP441 `L/R` (see above) and both modules' `VDD`/`GND` to
`3V3(OUT)`/`GND`. MAX98357A `SD` (shutdown) is left floating/pulled high for
the default 9dB gain, as on the other boards.

### Why BCLK on GPIO44 is fine

GPIO44 is the pin silkscreened `RX`, i.e. UART0 RX. Two reasons it is safe here:

1. The console is moved to the **native USB-Serial-JTAG** (GPIO19/20) by
   `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, which is how this board flashes and
   monitors anyway. Nothing on the firmware side wants UART0.
2. The ROM bootloader configures GPIO44 as a UART *input* before the app runs,
   so there is no output contention with the MAX98357A before I2S claims the
   pin. BCLK is an ESP→amp output; the amp never drives it.

The cost is that you cannot use an external USB-UART bridge on `TX`/`RX` for
console input on this build.

### GPIO4 is dead on this unit

The board this port was brought up on has **GPIO4 shorted to ground**. Driving it
high from the ESP32's own push-pull output still reads back 0, and an internal
pull-up cannot lift it — with nothing at all connected to the header pin. Every
other broken-out pin (2/3/5/8/9/10/11/13/43) drives both ways cleanly.

This cost most of a day of debugging, because a pin stuck at logic 0 fails
*silently*. With the mic's SD there, the I2S read returned full 960-sample
frames of `0x00000000` — identical to a dead mic. With the mic's WS there, the
microphone never received a word-select and emitted nothing — also identical to
a dead mic. The one stable clue was `pullup=0 pulldown=0` on GPIO4 from the very
first measurement.

To check another unit, drive the pin high and read it back:

```c
gpio_config_t io = { .pin_bit_mask = 1ULL << 4, .mode = GPIO_MODE_INPUT_OUTPUT };
gpio_config(&io);
gpio_set_level(4, 1);          // a healthy pin reads 1 here
ESP_LOGI("t", "%d", gpio_get_level(4));
```

If GPIO4 is healthy on your unit, it is free to use — nothing else claims it.

### Strapping pins

The SuperMini header does not break out GPIO0, GPIO45 or GPIO46, so unlike the
NX (whose emotion button sits on strapping pin GPIO46) nothing in this pin map
can disturb boot mode. GPIO3 is a strapping pin but is left free.

## Display

SSD1306 only. `board_def.c` sets `display_auto_cfg_t.st7789 = NULL` instead of
using the `LUGO_DISPLAY_AUTO` macro: the ST7789 alternative needs two more pins
for DC/RST, and the SuperMini header has too few left to commit them to a panel
that is not fitted. `display_init()` probes I2C and stops there — a board with
no OLED attached still boots and runs headless.

## Module differences vs `lugo-s3-nx`

- **4MB flash** (NX: 8MB/16MB). `partitions.csv` needs `0x10000 + 0x300000` =
  3.2MB, so the existing table fits with ~800KB spare — but there is no room to
  grow the app partition or add an OTA slot without a second table.
- **2MB quad PSRAM** (NX: 8MB octal). Setting this wrong is not a soft failure:
  an octal-mode init against a quad part hangs the bootloader before
  `app_main()`. If a build boot-loops immediately after flashing, check
  `CONFIG_SPIRAM_MODE_QUAD` first.
- Quad PSRAM is roughly **half the bandwidth** of octal at the same clock, so
  the inherited 64KB/64B-line D-cache matters more here, not less — every audio
  or display buffer read that misses pays a slower bus.
- If the in-package flash refuses to boot at QIO, fall back to
  `CONFIG_ESPTOOLPY_FLASHMODE_DIO=y` and reflash the **bootloader** too
  (`idf.py flash`, not `app-flash`) — see the note in `sdkconfig.defaults`.

## Buttons

Only the wake button (GPIO7) is wired. `vol_up`/`vol_down`/`emotion` are `-1`;
volume and emotion are driven over MCP and the web UI instead.
