#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Moved here from buttons.h so the board layer can reference it without a
// circular component dependency. The first three values must stay 0/1/2
// (main.c relies on them); new buttons are only ever appended.
typedef enum {
    BTN_WAKE,      // wake / conversation toggle
    BTN_VOL_UP,
    BTN_VOL_DOWN,
    BTN_EMOTION,   // tact switch: show a random emotion on the eyes
    // Wake-only, fired by the button driver's hold-tracking (see
    // gpio_buttons.c + button_hold_logic.h). The ladder is decided by how long
    // the press lasted and reported when the button is LET GO, so that a 10s
    // press can still become a 15s one:
    //
    //   released  <10s        -> RELEASE     (normal tap: wake/idle toggle)
    //   released  10s..15s    -> HOLD        (enter the setup portal)
    //   released  >=15s       -> ERASE_ARM   (ask to confirm a full wipe)
    //
    // The two WARN events fire at the threshold crossings while the button is
    // still down, and exist only so the display can tell the user what letting
    // go right now would do. They carry no action of their own.
    BTN_WAKE_RELEASE,
    BTN_WAKE_HOLD,
    BTN_WAKE_WARN_SETUP,
    BTN_WAKE_WARN_ERASE,
    BTN_WAKE_ERASE_ARM,
    // Confirm phase, entered after ERASE_ARM: a second deliberate 3s hold
    // confirms, anything shorter cancels.
    BTN_WAKE_ERASE_CONFIRM,
    BTN_WAKE_CONFIRM_ABORT,
} button_id_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    // Read up to `samples` mono 16-bit frames at 16 kHz into pcm.
    //
    // Returns the number of frames actually written (0 .. samples), or -1 on a
    // driver/hardware error. A short read is normal and NOT an error — the
    // caller (mic_task) treats anything other than a full frame as "try again",
    // so 0 and -1 lead to the same place; the distinction exists so a genuine
    // failure is still distinguishable in a log or a future caller. Returning 0
    // for an error, as one driver used to, throws that away.
    //
    // Samples are converted from the mic's 32-bit left-justified I2S slots by
    // the shared i2s_pcm_from_i2s32() (components/audio/i2s_pcm.c), so every
    // driver saturates identically at the int16 bounds. The gain shift handed
    // to it is per-driver on purpose (see i2s_pcm.h) — it is board tuning, not
    // an implementation detail.
    //
    // Blocking behaviour is deliberately NOT part of this contract: a driver
    // may block indefinitely until a full frame arrives (the dual-I2S path) or
    // time out and return a short read (the single-I2S path). Callers must
    // handle a short read either way and must not assume the call returns
    // promptly.
    int       (*read)(int16_t *pcm, int samples);
    // Discard whatever the RX DMA has already captured. Optional (may be NULL —
    // audio_mic_flush() checks). MUST only be called from the task that owns
    // the channel, i.e. the one that calls read(): it restarts the channel, and
    // doing that underneath a task blocked inside read() is not a supported
    // ordering. See mic_task in main.c for the request-flag pattern.
    void      (*flush)(void);
} mic_ops_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    int       (*write)(const int16_t *pcm, int samples);  // returns samples written
    void      (*reset)(void);
    void      (*set_volume)(int pct);
    int       (*get_volume)(void);
    int       (*adjust_volume)(int delta);
} speaker_ops_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    void (*show)(const char *line1, const char *line2);
    // Blits an RGB565 buffer at (x,y), size w*h, straight to the panel — the
    // pixel-level counterpart to show(), used for images/animation instead
    // of the 2-line text path. flush/width/height are set as a group (every
    // board_def.c today sets all three or none) — width==0 is the actual
    // "no pixel panel" signal callers check (see main.c's status_task); a
    // board that set one without the other would violate that invariant,
    // since no caller in this codebase NULL-checks flush directly.
    void (*flush)(int x, int y, int w, int h, const uint16_t *rgb565);
    int width;   // panel dimensions in pixels; 0 if flush is NULL
    int height;
    // True for 1-bit panels, where flush() thresholds each RGB565 pixel to
    // on/off by luminance (see ssd1306's pixel_on) instead of showing it —
    // so any gradient or dimmed tone lands as either fully lit or fully
    // dark, never in between, and such a panel is also small enough that
    // screen real estate is worth spending differently (see main.c's
    // idle_status_bar_height, the only caller today).
    //
    // Deliberately NOT inferred from a small `height`, which is a separate
    // question: a color 128x64 panel would still render gradients fine, and
    // a 128x128 OLED still couldn't.
    bool mono;
    // Turn the panel backlight on/off. GPIO on/off only, no PWM dimming.
    void (*set_backlight)(bool on);
} display_ops_t;

typedef struct {
    void (*start)(void (*on_press)(button_id_t id));
    // Switch the Wake hold-ladder into (or out of) erase-confirmation mode.
    // Optional — may be NULL, which buttons_set_confirm_mode() treats as "this
    // driver has no hold ladder", not as an error.
    void (*set_confirm_mode)(bool on);
} buttons_ops_t;

// TP4056's CHRG/STDBY are mutually exclusive per its datasheet, so this is a
// tri-state rather than two independent booleans — NOT_CHARGING also covers
// "no battery hardware on this board" (the default when board_t.battery is
// NULL; see components/battery's facade).
typedef enum {
    BATTERY_NOT_CHARGING,
    BATTERY_CHARGING,
    BATTERY_FULL,
} battery_charge_state_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    int (*read_pct)(void);   // 0-100, or -1 if a reading isn't available yet
    battery_charge_state_t (*charge_state)(void);
} battery_ops_t;

typedef struct board {
    const char          *name;
    const mic_ops_t     *mic;
    const speaker_ops_t *speaker;
    const display_ops_t *display;
    const buttons_ops_t *buttons;
    const battery_ops_t *battery;   // NULL if this board has no battery sensor
    // const void *net;         // RESERVED for a future 4G/other-transport board
    const void          *mic_cfg;
    const void          *speaker_cfg;
    const void          *display_cfg;
    const void          *buttons_cfg;
    const void          *battery_cfg;
    // GPIOs this board has already wired to something, so a generic pin-poking
    // caller (today: mcp_tools' self.gpio.set, driven by the LLM) can refuse
    // them instead of, say, reconfiguring the display's SCLK as an output and
    // killing the panel mid-conversation.
    //
    // Only pins the *board* knows about need listing — display, buttons and
    // any other bare-GPIO peripheral. Pins claimed by an IDF driver that
    // reserves them (I2S mic/speaker, SPI flash, PSRAM) are already covered at
    // runtime by esp_gpio_is_reserved(), so listing them here would just be a
    // second copy to keep in sync. Declare pins via the same named constants
    // the cfg structs use, so the two can't drift.
    //
    // NULL/0 means "this board declares nothing", which is honest but leaves
    // its peripherals unprotected — prefer an explicit list.
    const int           *reserved_pins;
    int                  n_reserved_pins;
} board_t;
