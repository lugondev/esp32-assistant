#include "audio.h"
#include "board.h"
#include "sdkconfig.h"

// Dispatch the combined app-facing audio API to the board's independent mic and
// speaker drivers. board_select_configured() must run (app_main) before audio_init().
static const mic_ops_t     *s_mic;
static const speaker_ops_t *s_spk;

esp_err_t audio_init(void) {
    s_mic = board_active()->mic;
    s_spk = board_active()->speaker;
#if CONFIG_AA_SKIP_AUDIO_INIT
    // See Kconfig help: Wokwi's simulated I2S peripheral never signals
    // clock-ready, so the real init() calls below hang forever in sim.
    return ESP_OK;
#else
    esp_err_t err = s_mic->init(board_active()->mic_cfg);
    if (err != ESP_OK) return err;
    return s_spk->init(board_active()->speaker_cfg);
#endif
}
int audio_mic_read(int16_t *pcm, int samples) {
#if CONFIG_AA_SKIP_AUDIO_INIT
    (void)pcm; (void)samples;
    return 0;
#else
    return s_mic->read(pcm, samples);
#endif
}
void audio_mic_flush(void) {
#if !CONFIG_AA_SKIP_AUDIO_INIT
    // Optional op: a board whose mic driver can't drop captured audio simply
    // doesn't get the self-talk protection, rather than crashing on a NULL.
    if (s_mic->flush) s_mic->flush();
#endif
}
int audio_spk_write(const int16_t *pcm, int n) {
#if CONFIG_AA_SKIP_AUDIO_INIT
    (void)pcm;
    return n;
#else
    return s_spk->write(pcm, n);
#endif
}
void audio_spk_reset(void) {
#if !CONFIG_AA_SKIP_AUDIO_INIT
    s_spk->reset();
#endif
}
void audio_set_volume(int pct) {
#if !CONFIG_AA_SKIP_AUDIO_INIT
    s_spk->set_volume(pct);
#else
    (void)pct;
#endif
}
int audio_get_volume(void) {
#if CONFIG_AA_SKIP_AUDIO_INIT
    return 0;
#else
    return s_spk->get_volume();
#endif
}
int audio_adjust_volume(int delta) {
#if CONFIG_AA_SKIP_AUDIO_INIT
    (void)delta;
    return 0;
#else
    return s_spk->adjust_volume(delta);
#endif
}
