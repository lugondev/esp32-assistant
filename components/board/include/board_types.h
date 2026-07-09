#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Moved here from buttons.h so the board layer can reference it without a
// circular component dependency. Values must stay 0/1/2 (main.c relies on them).
typedef enum {
    BTN_WAKE,      // wake / conversation toggle
    BTN_VOL_UP,
    BTN_VOL_DOWN,
} button_id_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    int       (*read)(int16_t *pcm, int samples);   // returns frames read
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
    // of the 2-line text path. NULL on boards with no pixel-addressable
    // panel (must be checked before calling, same as the battery op pattern).
    void (*flush)(int x, int y, int w, int h, const uint16_t *rgb565);
    int width;   // panel dimensions in pixels; 0 if flush is NULL
    int height;
    // Turn the panel backlight on/off. GPIO on/off only, no PWM dimming.
    void (*set_backlight)(bool on);
} display_ops_t;

typedef struct {
    void (*start)(void (*on_press)(button_id_t id));
} buttons_ops_t;

typedef struct board {
    const char          *name;
    const mic_ops_t     *mic;
    const speaker_ops_t *speaker;
    const display_ops_t *display;
    const buttons_ops_t *buttons;
    // const void *net;         // RESERVED for a future 4G/other-transport board
    const void          *mic_cfg;
    const void          *speaker_cfg;
    const void          *display_cfg;
    const void          *buttons_cfg;
    bool               (*match)(void); // true if firmware is running on this board
} board_t;
