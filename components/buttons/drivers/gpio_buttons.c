#include "buttons.h"
#include "buttons_gpio.h"
#include "board.h"
#include "button_hold_logic.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

// 5 slots, not 4: the last one is a second BTN_WAKE (buttons_gpio_cfg_t.wake2).
// Every slot carries its own debounce/hold state, so two wake buttons behave as
// one button each rather than fighting over shared state.
#define NBTN 5
static int s_gpios[NBTN];  // filled from board cfg in gpio_buttons_start; <0 = absent
static const button_id_t s_ids[NBTN]   = { BTN_WAKE, BTN_VOL_UP, BTN_VOL_DOWN, BTN_EMOTION,
                                           BTN_WAKE };

static void (*s_cb)(button_id_t);

// Per-button debounce state machine, one 20ms scan tick for all buttons.
// Nothing here ever blocks the scan loop — the previous version busy-waited
// inside the loop for the pressed button's release, so HOLDING one button
// (e.g. Vol+) froze every other button, including Wake/barge-in.
typedef enum {
    BTN_ST_RELEASED,   // idle high (pull-up)
    BTN_ST_DEBOUNCE,   // saw a press edge; confirm it next tick (20ms settle)
    BTN_ST_HELD,       // fired; ignore until released (one event per press)
} btn_state_t;

// Hold-duration tracking, Wake-only (see btn_hold_step in button_hold_logic.h).
// Indexed in parallel with s_gpios/s_ids; unused (and untouched) for every
// button other than s_ids[i] == BTN_WAKE.
static int  s_held_ticks[NBTN];
static bool s_hold_fired[NBTN];

static void buttons_task(void *arg) {
    (void)arg;
    btn_state_t st[NBTN];
    for (int i = 0; i < NBTN; i++) st[i] = BTN_ST_RELEASED;
    for (;;) {
        for (int i = 0; i < NBTN; i++) {
            if (s_gpios[i] < 0) continue;   // button not present on this board
            int lvl = gpio_get_level(s_gpios[i]);
            switch (st[i]) {
            case BTN_ST_RELEASED:
                if (lvl == 0) st[i] = BTN_ST_DEBOUNCE;   // press edge
                break;
            case BTN_ST_DEBOUNCE:
                if (lvl == 0) {                          // still low = real press
                    ESP_LOGI(TAG, "press gpio%d", s_gpios[i]);
                    if (s_cb) s_cb(s_ids[i]);
                    st[i] = BTN_ST_HELD;
                    s_held_ticks[i] = 0;
                    s_hold_fired[i] = false;
                } else {
                    st[i] = BTN_ST_RELEASED;             // bounce — ignore
                }
                break;
            case BTN_ST_HELD:
                if (s_ids[i] == BTN_WAKE) {
                    btn_hold_event_t ev = btn_hold_step(lvl == 1, &s_held_ticks[i], &s_hold_fired[i]);
                    if (ev == BTN_HOLD_EVENT_RELEASE) {
                        if (s_cb) s_cb(BTN_WAKE_RELEASE);
                    } else if (ev == BTN_HOLD_EVENT_HOLD) {
                        ESP_LOGI(TAG, "wake held to threshold on gpio%d", s_gpios[i]);
                        if (s_cb) s_cb(BTN_WAKE_HOLD);
                    }
                }
                if (lvl == 1) st[i] = BTN_ST_RELEASED;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void gpio_buttons_start(void (*on_press)(button_id_t)) {
    const buttons_gpio_cfg_t *c = (const buttons_gpio_cfg_t *)board_active()->buttons_cfg;
    s_gpios[0] = c->wake; s_gpios[1] = c->vol_up; s_gpios[2] = c->vol_down;
    s_gpios[3] = c->emotion; s_gpios[4] = c->wake2;
    s_cb = on_press;
    for (int i = 0; i < NBTN; i++) {
        if (s_gpios[i] < 0) continue;   // button not present on this board
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << s_gpios[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&c);
    }
    xTaskCreatePinnedToCore(buttons_task, "buttons", 3072, NULL, 3, NULL, 0);
}

const buttons_ops_t buttons_gpio_ops = {
    .start = gpio_buttons_start,
};
