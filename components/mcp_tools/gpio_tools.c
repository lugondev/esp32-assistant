// esp32-assistant/components/mcp_tools/gpio_tools.c
#include "mcp_tools.h"
#include "board.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "esp_bit_defs.h"   // BIT64, for esp_gpio_is_reserved's mask argument
#include "esp_private/esp_gpio_reserve.h"
#include <stdbool.h>

// Three independent gates, because no single one of them is sufficient:
//
//  1. GPIO_IS_VALID_OUTPUT_GPIO — the pin exists on THIS SoC and can drive an
//     output. The schema's numeric bounds can't express this: they are one
//     literal range shared by every target, and the S3's 0..48 is nonsense on
//     a C3 (0..21).
//  2. esp_gpio_is_reserved — the pin is claimed by a driver or by the module
//     itself. This is the one that matters most: it covers the SPI flash and
//     PSRAM buses (26-32 on an S3 quad module, and further up on the
//     octal-PSRAM R8 parts this project targets, 11-17 on the C3), where a
//     single gpio_config() as an output faults the chip instantly. Doing that
//     by hand would mean hardcoding a per-module pin table and getting the
//     quad-vs-octal distinction right; the flash/PSRAM drivers already
//     register the true mask at startup, so ask them instead. It also picks up
//     the I2S mic/speaker pins for free (i2s_common.c reserves them at channel
//     init), which is why the board lists below don't repeat them.
//  3. board_t.reserved_pins — everything the board wired that no driver
//     reserves: the display bus (neither SPI nor I2C reserves its pins),
//     DC/RST/backlight, and the buttons. This replaces a hardcoded array that
//     held lugo-s3-nx's literals only, so on the two C3 boards it protected
//     nothing real (their display is on 4/5/20/21 and wake on 0, none of which
//     appeared in it) while "reserving" pins 39-47 that a C3 doesn't have.
static bool pin_is_reserved(int pin, const char **why) {
    const board_t *b = board_active();
    if (b && b->reserved_pins) {
        for (int i = 0; i < b->n_reserved_pins; i++) {
            if (b->reserved_pins[i] == pin) {
                *why = "wired to this board's display or buttons";
                return true;
            }
        }
    }
    if (esp_gpio_is_reserved(BIT64(pin))) {
        *why = "reserved by the SoC (flash/PSRAM) or an active driver";
        return true;
    }
    return false;
}

static mcp_result_t gpio_set_fn(const char *args) {
    int pin = mcp_arg_int(args, "pin", -1);
    int value = mcp_arg_int(args, "value", -1);
    if (pin < 0 || value < 0) return mcp_err("missing pin or value");
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin))
        return mcp_err("pin %d is not a usable output on this chip", pin);
    const char *why = "";
    if (pin_is_reserved(pin, &why)) return mcp_err("pin %d is %s", pin, why);
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << (unsigned)pin, .mode = GPIO_MODE_OUTPUT };
    if (gpio_config(&cfg) != ESP_OK) return mcp_err("failed to configure pin %d", pin);
    gpio_set_level(pin, value ? 1 : 0);
    return mcp_ok_text("pin %d set to %d", pin, value ? 1 : 0);
}
// Upper bound tracks the build's target (SOC_GPIO_PIN_COUNT is 49 on the S3,
// 22 on the C3) so the schema the model sees isn't advertising pins the chip
// doesn't have. Gaps within that range are still the runtime checks' job.
static const mcp_prop_t gpio_set_props[] = {
    MCP_PROP_INT("pin", 0, SOC_GPIO_PIN_COUNT - 1), MCP_PROP_INT("value", 0, 1), MCP_PROP_END,
};
LUGO_MCP_TOOL(tool_gpio_set) {
    .name = "self.gpio.set", .description = "Set a GPIO pin high or low (rejects pins used by existing hardware)",
    .props = gpio_set_props, .requires_confirm = true, .fn = gpio_set_fn,
};
