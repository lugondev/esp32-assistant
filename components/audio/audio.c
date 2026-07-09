#include "audio.h"
#include "board.h"

// Dispatch to the active board's audio driver. board_detect_and_select() must
// run (in app_main) before audio_init().
static const audio_ops_t *s_ops;

esp_err_t audio_init(void) {
    s_ops = board_active()->audio;
    return s_ops->init(board_active()->audio_cfg);
}
int  audio_mic_read(int16_t *pcm, int samples)     { return s_ops->mic_read(pcm, samples); }
int  audio_spk_write(const int16_t *pcm, int n)    { return s_ops->spk_write(pcm, n); }
void audio_spk_reset(void)                          { s_ops->spk_reset(); }
void audio_set_volume(int pct)                      { s_ops->set_volume(pct); }
int  audio_get_volume(void)                         { return s_ops->get_volume(); }
int  audio_adjust_volume(int delta)                 { return s_ops->adjust_volume(delta); }
