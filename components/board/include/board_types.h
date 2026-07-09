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
    int  (*mic_read)(int16_t *pcm, int samples);
    int  (*spk_write)(const int16_t *pcm, int samples);
    void (*spk_reset)(void);
    void (*set_volume)(int pct);
    int  (*get_volume)(void);
    int  (*adjust_volume)(int delta);
} audio_ops_t;

typedef struct {
    esp_err_t (*init)(const void *cfg);
    void (*show)(const char *line1, const char *line2);
} display_ops_t;

typedef struct {
    void (*start)(void (*on_press)(button_id_t id));
} buttons_ops_t;

typedef struct board {
    const char          *name;
    const audio_ops_t   *audio;
    const display_ops_t *display;
    const buttons_ops_t *buttons;
    // const void *net;         // RESERVED for a future 4G/other-transport board
    const void          *audio_cfg;    // driver-specific pin/config blob
    const void          *display_cfg;
    const void          *buttons_cfg;
    bool               (*match)(void); // true if firmware is running on this board
} board_t;
