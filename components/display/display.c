#include "display.h"
#include "display_font.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_io_spi.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "display";

#define DISP_SPI_HOST   SPI2_HOST
#define DISP_SCLK_GPIO  42
#define DISP_MOSI_GPIO  41
#define DISP_DC_GPIO    1
#define DISP_RST_GPIO   2
#define DISP_BL_GPIO    17
#define DISP_WIDTH      240
#define DISP_HEIGHT     240

static esp_lcd_panel_handle_t s_panel;

static void clear_screen(void) {
    uint16_t black_row[DISP_WIDTH];
    memset(black_row, 0, sizeof black_row);
    for (int y = 0; y < DISP_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + 1, black_row);
    }
}

static void draw_char(int x, int y, char c) {
    const uint8_t *glyph = display_font_glyph(c);
    if (!glyph) return;
    uint16_t px[DISPLAY_FONT_GLYPH_WIDTH * DISPLAY_FONT_GLYPH_HEIGHT];
    for (int row = 0; row < DISPLAY_FONT_GLYPH_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < DISPLAY_FONT_GLYPH_WIDTH; col++) {
            bool on = (bits >> col) & 1;
            px[row * DISPLAY_FONT_GLYPH_WIDTH + col] = on ? 0xFFFF : 0x0000;
        }
    }
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + DISPLAY_FONT_GLYPH_WIDTH,
                               y + DISPLAY_FONT_GLYPH_HEIGHT, px);
}

static void draw_line(const char *text, int y) {
    int x = display_layout_line(text, DISP_WIDTH);
    if (x < 0) return;  // too wide for the screen — skip rather than wrap
    for (const char *p = text; *p; p++) {
        draw_char(x, y, *p);
        x += DISPLAY_FONT_GLYPH_WIDTH;
    }
}

esp_err_t display_init(void) {
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << DISP_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(DISP_BL_GPIO, 1);

    spi_bus_config_t buscfg = {
        .mosi_io_num = DISP_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = DISP_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISP_WIDTH * DISPLAY_FONT_GLYPH_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = -1,
        .dc_gpio_num = DISP_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST,
                                              &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = DISP_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    clear_screen();
    ESP_LOGI(TAG, "display ready");
    return ESP_OK;
}

void display_show(const char *line1, const char *line2) {
    clear_screen();
    if (line2 == NULL) {
        draw_line(line1, (DISP_HEIGHT - DISPLAY_FONT_GLYPH_HEIGHT) / 2);
    } else {
        int gap = 4;
        int total_h = 2 * DISPLAY_FONT_GLYPH_HEIGHT + gap;
        int y1 = (DISP_HEIGHT - total_h) / 2;
        int y2 = y1 + DISPLAY_FONT_GLYPH_HEIGHT + gap;
        draw_line(line1, y1);
        draw_line(line2, y2);
    }
}
