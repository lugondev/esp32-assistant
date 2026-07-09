#include "audio.h"
#include "board.h"

// Dispatch the combined app-facing audio API to the board's independent mic and
// speaker drivers. board_detect_and_select() must run (app_main) before audio_init().
static const mic_ops_t     *s_mic;
static const speaker_ops_t *s_spk;

esp_err_t audio_init(void) {
    s_mic = board_active()->mic;
    s_spk = board_active()->speaker;
    esp_err_t err = s_mic->init(board_active()->mic_cfg);
    if (err != ESP_OK) return err;
    return s_spk->init(board_active()->speaker_cfg);
}
int  audio_mic_read(int16_t *pcm, int samples)  { return s_mic->read(pcm, samples); }
int  audio_spk_write(const int16_t *pcm, int n) { return s_spk->write(pcm, n); }
void audio_spk_reset(void)                       { s_spk->reset(); }
void audio_set_volume(int pct)                   { s_spk->set_volume(pct); }
int  audio_get_volume(void)                      { return s_spk->get_volume(); }
int  audio_adjust_volume(int delta)              { return s_spk->adjust_volume(delta); }
