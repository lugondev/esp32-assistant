#include "buttons.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define BTN_WAKE_GPIO      47
#define BTN_VOL_UP_GPIO    40
#define BTN_VOL_DOWN_GPIO  39

#define NBTN 3
static const int         s_gpios[NBTN] = { BTN_WAKE_GPIO, BTN_VOL_UP_GPIO, BTN_VOL_DOWN_GPIO };
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

void buttons_start(void (*on_press)(button_id_t)) {
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
