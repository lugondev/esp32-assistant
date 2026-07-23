// esp32-assistant/components/mcp_tools/gpio_tools.c
#include "mcp_tools.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include <stdbool.h>

// Pins already owned by mic/speaker/display/buttons on this board (see
// board_def.c for lugo-s3-devkit). A tool-driven GPIO write must never touch
// these — reconfiguring, say, the I2S BCLK pin as a generic output would
// desync audio. Kconfig-configurable mic/speaker pins are read as their
// #define'd values; display/button pins are the literal ints in board_def.c.
static const int RESERVED_PINS[] = {
    42, 41, 1, 2, 17,        // display: sclk, mosi, dc, rst, bl
    47, 40, 39,              // buttons: wake, vol_up, vol_down
    CONFIG_AA_MIC_WS, CONFIG_AA_MIC_SCK, CONFIG_AA_MIC_SD,
    CONFIG_AA_SPK_BCLK, CONFIG_AA_SPK_LRC, CONFIG_AA_SPK_DIN,
};
#define N_RESERVED (int)(sizeof(RESERVED_PINS) / sizeof(RESERVED_PINS[0]))

static bool pin_is_reserved(int pin) {
    for (int i = 0; i < N_RESERVED; i++) if (RESERVED_PINS[i] == pin) return true;
    return false;
}

static mcp_result_t gpio_set_fn(const char *args) {
    int pin = mcp_arg_int(args, "pin", -1);
    int value = mcp_arg_int(args, "value", -1);
    if (pin < 0 || value < 0) return mcp_err("missing pin or value");
    if (pin_is_reserved(pin)) return mcp_err("pin %d is reserved by existing hardware", pin);
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT };
    if (gpio_config(&cfg) != ESP_OK) return mcp_err("failed to configure pin %d", pin);
    gpio_set_level(pin, value ? 1 : 0);
    return mcp_ok_text("pin %d set to %d", pin, value ? 1 : 0);
}
static const mcp_prop_t gpio_set_props[] = {
    MCP_PROP_INT("pin", 0, 48), MCP_PROP_INT("value", 0, 1), MCP_PROP_END,
};
LUGO_MCP_TOOL(tool_gpio_set) {
    .name = "self.gpio.set", .description = "Set a GPIO pin high or low (rejects pins used by existing hardware)",
    .props = gpio_set_props, .requires_confirm = true, .fn = gpio_set_fn,
};
