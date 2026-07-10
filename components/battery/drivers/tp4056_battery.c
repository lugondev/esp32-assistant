#include "tp4056_battery.h"
#include "battery_logic.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const tp4056_battery_cfg_t *s_cfg;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_have_adc;

static esp_err_t init(const void *cfg) {
    s_cfg = (const tp4056_battery_cfg_t *)cfg;

    gpio_config_t pins = {
        .pin_bit_mask = (1ULL << s_cfg->gpio_chrg) | (1ULL << s_cfg->gpio_stdby),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pins);
    if (err != ESP_OK) return err;

    s_have_adc = s_cfg->adc_channel >= 0;
    if (!s_have_adc) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = s_cfg->adc_unit };
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = s_cfg->adc_atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, s_cfg->adc_channel, &chan_cfg);
    if (err != ESP_OK) return err;

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_cfg->adc_unit,
        .chan = s_cfg->adc_channel,
        .atten = s_cfg->adc_atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
}

static int read_pct(void) {
    if (!s_have_adc) return -1;
    int raw;
    if (adc_oneshot_read(s_adc, s_cfg->adc_channel, &raw) != ESP_OK) return -1;
    int mv_at_pin;
    if (adc_cali_raw_to_voltage(s_cali, raw, &mv_at_pin) != ESP_OK) return -1;
    // BAT+ --R1-- pin --R2-- GND: pin voltage is the divider's fraction of
    // BAT+, so scale back up by (R1+R2)/R2 to recover the real pack voltage.
    int mv_batt = mv_at_pin * (s_cfg->r1_ohms + s_cfg->r2_ohms) / s_cfg->r2_ohms;
    return battery_pct_from_millivolts(mv_batt);
}

static battery_charge_state_t charge_state(void) {
    bool chrg_active = gpio_get_level(s_cfg->gpio_chrg) == 0;
    bool stdby_active = gpio_get_level(s_cfg->gpio_stdby) == 0;
    return battery_charge_state_from_pins(chrg_active, stdby_active);
}

const battery_ops_t tp4056_battery_ops = {
    .init = init,
    .read_pct = read_pct,
    .charge_state = charge_state,
};
