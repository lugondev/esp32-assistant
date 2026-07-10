#include "battery.h"
#include "board.h"

static const battery_ops_t *s_ops;

esp_err_t battery_init(void) {
    s_ops = board_active()->battery;
    if (!s_ops) return ESP_OK;  // this board has no battery hardware wired
    return s_ops->init(board_active()->battery_cfg);
}
int battery_read_pct(void) {
    return s_ops ? s_ops->read_pct() : -1;
}
battery_charge_state_t battery_charge_state(void) {
    return s_ops ? s_ops->charge_state() : BATTERY_NOT_CHARGING;
}
