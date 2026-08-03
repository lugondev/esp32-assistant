# Bringing up a board that has no board_def.c

`lugo-custom` exists so new hardware can make a sound before anyone writes a
file for it. Every pin comes from Kconfig.

## Use it

```
idf.py set-target esp32s3      # or esp32c3
idf.py menuconfig
```

Under **Target board**, pick *Custom board*. A **Custom board pinout** menu
appears below; enter the GPIOs you actually wired. Then build and flash as
usual.

If the mic is silent and the pins are confirmed correct, try toggling
*INMP441 L/R is tied HIGH (mic drives the RIGHT I2S slot)*. Reading the wrong
I2S slot returns a channel of pure zeros, which looks exactly like a dead
microphone.

`CONFIG_AA_MIC_METER` (under the project's own menu) renders the mic's peak
level on the panel, which separates "mic produces nothing" from "the rest of
the pipeline is wrong".

## Then promote it

The custom board is a workbench, not a destination. Once a pinout works,
copy it into `components/boards/<your_board>/board_def.c`, add the matching
entries to `choice AA_BOARD` and `AA_BOARD_NAME` in `main/Kconfig.projbuild`,
and — the part that matters — write down what you learned.

`components/boards/lugo_s3_supermini/board_def.c` is the example to follow. It
records that GPIO4 on that board is shorted to ground and fails silently as a
logic 0, that GPIO44 is safe for BCLK only because the console runs on the
native USB-Serial-JTAG rather than UART0, and that its INMP441 drives the right
slot as measured rather than assumed. None of that fits in a Kconfig help
block, and all of it cost hardware debugging to find.

## What cannot be auto-detected

The **MAX98357A** cannot be detected at all. Every digital pin it has is an
input and its only output is analogue, so there is no signal path back to the
ESP32 — no software can tell whether one is attached.

The **INMP441** drives its data line, so a stereo capture can reveal which slot
it uses and whether any mic is responding. But it cannot identify the part (an
ICS-43434 looks identical) and it cannot find the pins, because probing means
driving clocks on pins you must already have chosen. That is why this menu asks
you for them.
