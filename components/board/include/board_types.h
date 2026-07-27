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
} button_id_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    int       (*read)(int16_t *pcm, int samples);   // returns frames read
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
    bool               (*match)(void); // true if firmware is running on this board
} board_t;
