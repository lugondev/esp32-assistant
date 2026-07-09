#include "board.h"
#include "audio.h"
#include "display.h"
#include "buttons.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// ---- mock audio driver ----
static int m_init_calls, m_mic_calls, m_last_vol;
static const int MOCK_MIC_SAMPLES = 7;
static esp_err_t m_init(const void *cfg) { (void)cfg; m_init_calls++; return ESP_OK; }
static int  m_mic(int16_t *p, int n) { (void)p; (void)n; m_mic_calls++; return MOCK_MIC_SAMPLES; }
static int  m_spk(const int16_t *p, int n) { (void)p; return n; }
static void m_reset(void) {}
static void m_setv(int v) { m_last_vol = v; }
static int  m_getv(void) { return m_last_vol; }
static int  m_adjv(int d) { m_last_vol += d; return m_last_vol; }
static const audio_ops_t MOCK_AUDIO = {
    .init=m_init, .mic_read=m_mic, .spk_write=m_spk, .spk_reset=m_reset,
    .set_volume=m_setv, .get_volume=m_getv, .adjust_volume=m_adjv,
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

static const board_t MOCK_BOARD = { .name="mock", .audio=&MOCK_AUDIO, .display=&MOCK_DISPLAY, .buttons=&MOCK_BUTTONS };

static void test_audio_facade_dispatches(void) {
    board_set(&MOCK_BOARD);
    CHECK(audio_init() == ESP_OK);
    CHECK(m_init_calls == 1);
    int16_t buf[16];
    CHECK(audio_mic_read(buf, 16) == MOCK_MIC_SAMPLES);
    CHECK(m_mic_calls == 1);
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
