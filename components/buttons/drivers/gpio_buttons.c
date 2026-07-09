#include "buttons.h"
#include "buttons_gpio.h"
#include "board.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define NBTN 3
static int s_gpios[NBTN];  // filled from board cfg in gpio_buttons_start
static const button_id_t s_ids[NBTN]   = { BTN_WAKE, BTN_VOL_UP, BTN_VOL_DOWN };

static void (*s_cb)(button_id_t);

static void buttons_task(void *arg) {
    (void)arg;
    int prev[NBTN];
    for (int i = 0; i < NBTN; i++) prev[i] = 1;  // released = high (pull-up)
    for (;;) {
        for (int i = 0; i < NBTN; i++) {
            int lvl = gpio_get_level(s_gpios[i]);
            if (prev[i] == 1 && lvl == 0) {           // high->low = press edge
                vTaskDelay(pdMS_TO_TICKS(20));          // debounce settle
                if (gpio_get_level(s_gpios[i]) == 0) {
                    ESP_LOGI(TAG, "press gpio%d", s_gpios[i]);
                    if (s_cb) s_cb(s_ids[i]);
                    // wait for release so a hold doesn't retrigger
                    while (gpio_get_level(s_gpios[i]) == 0) vTaskDelay(pdMS_TO_TICKS(20));
                    lvl = 1;
                }
            }
            prev[i] = lvl;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void gpio_buttons_start(void (*on_press)(button_id_t)) {
    const buttons_gpio_cfg_t *c = (const buttons_gpio_cfg_t *)board_active()->buttons_cfg;
    s_gpios[0] = c->wake; s_gpios[1] = c->vol_up; s_gpios[2] = c->vol_down;
    s_cb = on_press;
    for (int i = 0; i < NBTN; i++) {
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
