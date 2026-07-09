#include "board.h"
#include "audio.h"
#include "display.h"
#include "buttons.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// ---- mock mic driver ----
static int mic_init_calls, mic_read_calls;
static const int MOCK_MIC_SAMPLES = 7;
static esp_err_t mic_init(const void *cfg) { (void)cfg; mic_init_calls++; return ESP_OK; }
static int mic_read(int16_t *p, int n) { (void)p; (void)n; mic_read_calls++; return MOCK_MIC_SAMPLES; }
static const mic_ops_t MOCK_MIC = { .init = mic_init, .read = mic_read };

// ---- mock speaker driver ----
static int spk_init_calls, spk_last_vol;
static esp_err_t spk_init(const void *cfg) { (void)cfg; spk_init_calls++; return ESP_OK; }
static int  spk_write(const int16_t *p, int n) { (void)p; return n; }
static void spk_reset(void) {}
static void spk_setv(int v) { spk_last_vol = v; }
static int  spk_getv(void) { return spk_last_vol; }
static int  spk_adjv(int d) { spk_last_vol += d; return spk_last_vol; }
static const speaker_ops_t MOCK_SPEAKER = {
    .init = spk_init, .write = spk_write, .reset = spk_reset,
    .set_volume = spk_setv, .get_volume = spk_getv, .adjust_volume = spk_adjv,
};
// ---- mock display driver ----
static int d_init_calls, d_show_calls, d_flush_calls;
static const char *d_last1;
static int d_last_x, d_last_y, d_last_w, d_last_h;
static const uint16_t *d_last_buf;
static esp_err_t d_init(const void *cfg) { (void)cfg; d_init_calls++; return ESP_OK; }
static void d_show(const char *l1, const char *l2) { (void)l2; d_show_calls++; d_last1 = l1; }
static void d_flush(int x, int y, int w, int h, const uint16_t *buf) {
    d_flush_calls++; d_last_x=x; d_last_y=y; d_last_w=w; d_last_h=h; d_last_buf=buf;
}
static bool s_backlight_on = false;
static void mock_display_set_backlight(bool on) { s_backlight_on = on; }
static const display_ops_t MOCK_DISPLAY = {
    .init=d_init, .show=d_show, .flush=d_flush, .width=64, .height=32,
    .set_backlight=mock_display_set_backlight,
};

// ---- mock buttons driver ----
static int b_start_calls;
static void b_start(void (*cb)(button_id_t)) { (void)cb; b_start_calls++; }
static const buttons_ops_t MOCK_BUTTONS = { .start = b_start };

static const board_t MOCK_BOARD = { .name="mock", .mic=&MOCK_MIC, .speaker=&MOCK_SPEAKER,
                                    .display=&MOCK_DISPLAY, .buttons=&MOCK_BUTTONS };

static void test_audio_facade_dispatches(void) {
    board_set(&MOCK_BOARD);
    CHECK(audio_init() == ESP_OK);
    CHECK(mic_init_calls == 1);
    CHECK(spk_init_calls == 1);
    int16_t buf[16];
    CHECK(audio_mic_read(buf, 16) == MOCK_MIC_SAMPLES);
    CHECK(mic_read_calls == 1);
    audio_set_volume(42);
    CHECK(audio_get_volume() == 42);
    CHECK(audio_adjust_volume(-10) == 32);
}

static void test_display_facade_dispatches(void) {
    board_set(&MOCK_BOARD);
    CHECK(display_init() == ESP_OK);
    CHECK(d_init_calls == 1);
    display_show("hello", NULL);
    CHECK(d_show_calls == 1);
    CHECK(d_last1 != NULL && strcmp(d_last1, "hello") == 0);
}

static void test_display_facade_dispatches_flush_and_dims(void) {
    board_set(&MOCK_BOARD);
    uint16_t pixels[4] = {1, 2, 3, 4};
    display_flush(5, 6, 2, 2, pixels);
    CHECK(d_flush_calls == 1);
    CHECK(d_last_x == 5 && d_last_y == 6 && d_last_w == 2 && d_last_h == 2);
    CHECK(d_last_buf == pixels);
    CHECK(display_width() == 64);
    CHECK(display_height() == 32);
}

static void noop_cb(button_id_t id) { (void)id; }
static void test_buttons_facade_dispatches(void) {
    board_set(&MOCK_BOARD);
    buttons_start(noop_cb);
    CHECK(b_start_calls == 1);
}

static void test_display_set_backlight_calls_through(void) {
    board_set(&MOCK_BOARD);
    display_init();
    display_set_backlight(true);
    CHECK(s_backlight_on == true);
    display_set_backlight(false);
    CHECK(s_backlight_on == false);
}

int main(void) {
    test_audio_facade_dispatches();
    test_display_facade_dispatches();
    test_display_facade_dispatches_flush_and_dims();
    test_buttons_facade_dispatches();
    test_display_set_backlight_calls_through();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
