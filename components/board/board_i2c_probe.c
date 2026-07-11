#include "board_i2c_probe.h"
#include "driver/i2c_master.h"

bool board_i2c_probe(int scl, int sda, uint16_t addr, int timeout_ms) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    if (i2c_new_master_bus(&bus_config, &bus_handle) != ESP_OK) {
        return false;
    }

    bool found = i2c_master_probe(bus_handle, addr, timeout_ms) == ESP_OK;

    i2c_del_master_bus(bus_handle);
    return found;
}
