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
static int d_init_calls, d_show_calls;
static const char *d_last1;
static esp_err_t d_init(const void *cfg) { (void)cfg; d_init_calls++; return ESP_OK; }
static void d_show(const char *l1, const char *l2) { (void)l2; d_show_calls++; d_last1 = l1; }
static const display_ops_t MOCK_DISPLAY = { .init=d_init, .show=d_show };

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

static void noop_cb(button_id_t id) { (void)id; }
static void test_buttons_facade_dispatches(void) {
    board_set(&MOCK_BOARD);
    buttons_start(noop_cb);
    CHECK(b_start_calls == 1);
}

int main(void) {
    test_audio_facade_dispatches();
    test_display_facade_dispatches();
    test_buttons_facade_dispatches();
    printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
