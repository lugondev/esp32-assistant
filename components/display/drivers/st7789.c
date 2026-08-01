#include "display.h"
#include "display_st7789.h"
#include "display_font.h"
#include "display_try.h"
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

// SPI pixel clock. The progression here was 4MHz (~8fps ceiling) -> 20MHz
// (~25ms per robot-eyes dirty band, ~57KB) -> 40MHz (~12ms). ST7789 silicon is
// generally happy well past 40MHz; the practical limit is the WIRING, not the
// panel — each corrupted transaction misdirects one chunk's row-address-set and
// shows up as a stray shifted band. Halving the flush time matters beyond
// frame rate: status_task blocks for the whole flush, and on the S3 that block
// is competing with opus for the PSRAM bus (the render buffers are in PSRAM, so
// spi_master bounce-copies every chunk out of it).
// If banding artifacts appear on jumper-wired panels, step back to 20MHz — this
// is the first thing to revert when the display misbehaves.
#define DISP_PCLK_HZ    (40 * 1000 * 1000)

static esp_lcd_panel_handle_t s_panel;

// Set only after a fully successful init, exactly like the ssd1306 driver's
// flag of the same name. When false (bad wiring, an SPI bus already taken, a
// panel that never answered) show()/flush()/set_backlight() become no-ops so
// the device boots and runs headless — main.c's display_init() call site
// promises this ("a missing/miswired panel must not boot-loop the device"),
// and before this flag existed the ESP_ERROR_CHECKs below broke that promise
// by aborting instead.
static bool s_ready;

// Backlight GPIO, cached at init time so st7789_set_backlight (which is
// cfg-less by the display_ops_t signature) can re-drive it later. -1 means
// "this board has no backlight pin" (both C3 board_defs wire the panel's LED
// straight to 3V3 and set .bl = -1) — every use below must check, since
// `1ULL << -1` is undefined behaviour and gpio_set_level(-1) is invalid.
static int s_bl_pin = -1;

// Fewer, larger SPI transactions are more reliable than many tiny ones on
// jumper-wire wiring (each transaction re-sends the row-address-set command;
// a single corrupted one misdirects that chunk's data, showing up as a
// stray colored band) — clear in 24-row bands (10 transactions) instead of
// one row at a time (240 transactions).
#define CLEAR_CHUNK_ROWS 24

static void clear_screen(void) {
    if (!s_ready) return;   // no panel (init failed / none wired): run headless
    // Lazily-allocated PSRAM scratch, NOT a static array: the static version
    // pinned 11.5KB of internal SRAM for the device's whole life (static
    // storage exists whether or not this ever runs) — internal RAM is the
    // scarce resource here, and this same display_flush path already runs
    // off PSRAM buffers everywhere else (robot_eyes, statusbar).
    static uint16_t *black_chunk;
    if (!black_chunk) {
        const size_t n = (size_t)DISP_WIDTH * CLEAR_CHUNK_ROWS;
        black_chunk = heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        // Fall back to internal RAM where there is no PSRAM (the C3 has none,
        // so the SPIRAM request there always fails) — otherwise this returned
        // early every time and an ST7789 on a C3 was simply never cleared,
        // leaving whatever was in panel RAM behind the UI.
        if (!black_chunk)
            black_chunk = heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!black_chunk) return;   // genuinely out of memory: skip, don't crash
    }
    for (int y = 0; y < DISP_HEIGHT; y += CLEAR_CHUNK_ROWS) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISP_WIDTH, y + CLEAR_CHUNK_ROWS, black_chunk);
    }
}

// display_font_draw_centered callback: one esp_lcd bitmap transaction per
// glyph, expanded to RGB565 on the stack.
static void put_glyph(int x, int y, const uint8_t *glyph, void *ctx) {
    (void)ctx;
    if (!s_ready) return;
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

static esp_err_t st7789_init(const void *cfg_v) {
    const display_st7789_cfg_t *c = (const display_st7789_cfg_t *)cfg_v;

    // Every failure below is non-fatal (DISP_TRY): display_init()'s caller
    // runs headless (see s_ready). This used to be ESP_ERROR_CHECK throughout,
    // which aborts — so a panel that couldn't come up boot-looped the device
    // instead of being skipped.

    // .bl < 0 means the panel's LED is hard-wired to 3V3 (both C3 boards).
    // Skip the GPIO entirely: `1ULL << -1` is undefined behaviour, and the
    // bit-63 mask it produced in practice made gpio_config return
    // ESP_ERR_INVALID_ARG — which the old ESP_ERROR_CHECK turned into an
    // abort, so the ST7789 fallback path could never boot on those boards.
    s_bl_pin = c->bl;
    if (s_bl_pin >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = 1ULL << (unsigned)s_bl_pin,
            .mode = GPIO_MODE_OUTPUT,
        };
        DISP_TRY(gpio_config(&bl_cfg));
        gpio_set_level(s_bl_pin, 1);
    }

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
    DISP_TRY(spi_bus_initialize(DISP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
    DISP_TRY(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISP_SPI_HOST,
                                       &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = c->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    DISP_TRY(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));

    DISP_TRY(esp_lcd_panel_reset(s_panel));
    DISP_TRY(esp_lcd_panel_init(s_panel));
    DISP_TRY(esp_lcd_panel_set_gap(s_panel, 0, 0));
    DISP_TRY(esp_lcd_panel_invert_color(s_panel, true));
    DISP_TRY(esp_lcd_panel_disp_on_off(s_panel, true));

    s_ready = true;   // must precede clear_screen(): it draws through the guard
    clear_screen();
    ESP_LOGI(TAG, "display ready (st7789 spi %dx%d, bl=%d)", DISP_WIDTH, DISP_HEIGHT, s_bl_pin);
    return ESP_OK;
}

static void st7789_show(const char *line1, const char *line2) {
    clear_screen();
    int y1, y2;
    display_layout_lines(DISP_HEIGHT, line2 != NULL, &y1, &y2);
    display_font_draw_centered(line1, y1, DISP_WIDTH, put_glyph, NULL);
    if (line2) display_font_draw_centered(line2, y2, DISP_WIDTH, put_glyph, NULL);
}

// Raw pixel blit for images/animation. Not host-tested — same as
// st7789_init/put_glyph/clear_screen above, this only exercises real SPI
// hardware. esp_lcd_panel_draw_bitmap auto-chunks large transfers against
// the bus's max_transfer_sz (see st7789_init), so arbitrary w*h is safe to
// pass through directly, same as clear_screen's larger-than-max_transfer_sz
// chunked writes already rely on.
static void st7789_flush(int x, int y, int w, int h, const uint16_t *rgb565) {
    if (!s_ready) return;   // no panel (init failed / none wired): run headless
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, rgb565);
}

// GPIO on/off only (no PWM dimming). Re-drives the pin captured at init time
// since display_ops_t functions are cfg-less by signature. A board with no
// backlight pin (s_bl_pin < 0, e.g. LED tied to 3V3) accepts the call and does
// nothing, so self.screen.set_backlight stays safe to invoke everywhere —
// same contract as the ssd1306's no-op backlight.
static void st7789_set_backlight(bool on) {
    if (!s_ready || s_bl_pin < 0) return;
    gpio_set_level(s_bl_pin, on ? 1 : 0);
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
