#include "display.h"
#include "display_st7789.h"
#include "display_font.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_io_spi.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "display";

#define DISP_SPI_HOST   SPI2_HOST
#define DISP_WIDTH      240
#define DISP_HEIGHT     240

// SPI pixel clock. 20MHz (~2.5MB/s) flushes the robot-eyes dirty band
// (~57KB) in ~25ms — the difference between the old 4MHz's ~8fps ceiling
// and a comfortably animated HUD. ST7789 silicon is generally happy well
// past 40MHz; the practical limit here is the wiring (each corrupted
// transaction misdirects one chunk's row-address-set, showing up as a
// stray shifted band). If those artifacts appear on a jumper-wired panel,
// step back down toward 10MHz.
#define DISP_PCLK_HZ    (20 * 1000 * 1000)

static esp_lcd_panel_handle_t s_panel;

// Backlight GPIO, cached at init time so st7789_set_backlight (which is
// cfg-less by the display_ops_t signature) can re-drive it later.
int g_st7789_bl_pin;

// Fewer, larger SPI transactions are more reliable than many tiny ones on
// jumper-wire wiring (each transaction re-sends the row-address-set command;
// a single corrupted one misdirects that chunk's data, showing up as a
// stray colored band) — clear in 24-row bands (10 transactions) instead of
// one row at a time (240 transactions).
#define CLEAR_CHUNK_ROWS 24

static void clear_screen(void) {
    // Lazily-allocated PSRAM scratch, NOT a static array: the static version
    // pinned 11.5KB of internal SRAM for the device's whole life (static
    // storage exists whether or not this ever runs) — internal RAM is the
    // scarce resource here, and this same display_flush path already runs
    // off PSRAM buffers everywhere else (robot_eyes, statusbar).
    static uint16_t *black_chunk;
    if (!black_chunk) {
        black_chunk = heap_caps_calloc((size_t)DISP_WIDTH * CLEAR_CHUNK_ROWS,
                                        sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!black_chunk) return;   // no PSRAM: skip the clear rather than crash
    }
    for (int y = 0; y < DISP_HEIGHT; y += CLEAR_CHUNK_ROWS) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + CLEAR_CHUNK_ROWS, black_chunk);
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

static esp_err_t st7789_init(const void *cfg_v) {
    const display_st7789_cfg_t *c = (const display_st7789_cfg_t *)cfg_v;
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << c->bl,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    gpio_set_level(c->bl, 1);
    g_st7789_bl_pin = c->bl;

    spi_bus_config_t buscfg = {
        .mosi_io_num = c->mosi,
        .miso_io_num = -1,
        .sclk_io_num = c->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // Deliberately small — do NOT raise this to batch bigger flushes.
        // Our render buffers live in PSRAM, which spi_master can't DMA from
        // directly: it allocates an INTERNAL bounce buffer per queued
        // transaction, sized to the chunk (spi_master.c setup_priv_desc).
        // esp_lcd queues all of a flush's chunks back-to-back, so worst-case
        // transient internal-DMA memory is chunk_size * min(chunks_per_flush,
        // trans_queue_depth): at this 3.8KB it's a proven-safe ~38KB; at 32KB
        // chunks it needed ~96KB and every flush failed on hardware with
        // "spi transmit (queue) color failed" (ESP_ERR_NO_MEM). The real
        // flush-speed lever is DISP_PCLK_HZ above, not fewer transactions.
        .max_transfer_sz = DISP_WIDTH * DISPLAY_FONT_GLYPH_HEIGHT * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = -1,
        .dc_gpio_num = c->dc,
        .spi_mode = 3,  // confirmed against xiaozhi-esp32's genjutech-s3-1.54tft
                        // board (same panel/wiring family) — mode 0 has wrong
                        // clock polarity/phase, so the panel silently
                        // misreads every SPI command during init even though
                        // there's no read-back to detect it in software.
        .pclk_hz = DISP_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST,
                                              &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = c->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    clear_screen();
    ESP_LOGI(TAG, "display ready");
    return ESP_OK;
}

static void st7789_show(const char *line1, const char *line2) {
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

// Raw pixel blit for images/animation. Not host-tested — same as
// st7789_init/draw_char/clear_screen above, this only exercises real SPI
// hardware. esp_lcd_panel_draw_bitmap auto-chunks large transfers against
// the bus's max_transfer_sz (see st7789_init), so arbitrary w*h is safe to
// pass through directly, same as clear_screen's larger-than-max_transfer_sz
// chunked writes already rely on.
static void st7789_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, rgb565);
}

// GPIO on/off only (no PWM dimming). Re-drives the pin captured at init time
// since display_ops_t functions are cfg-less by signature.
static void st7789_set_backlight(bool on) {
    gpio_set_level(g_st7789_bl_pin, on ? 1 : 0);
}

const display_ops_t display_st7789_ops = {
    .init = st7789_init,
    .show = st7789_show,
    .flush = st7789_flush,
    .mono = false,  // 16-bit color: pixels reach the panel as handed over
    .width = DISP_WIDTH,
    .height = DISP_HEIGHT,
    .set_backlight = st7789_set_backlight,
};
