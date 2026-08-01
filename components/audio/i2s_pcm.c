#include "i2s_pcm.h"

void i2s_pcm_from_i2s32(const int32_t *raw, int frames, int gain_shift,
                        int16_t *pcm) {
    for (int i = 0; i < frames; i++) {
        int32_t v = raw[i] >> gain_shift;
        // Saturate rather than let the int16 cast wrap: a positive peak that
        // wraps comes out as a negative one, which is an audible crackle and
        // feeds the uplink encoder a discontinuity it has to spend bits on.
        if (v > INT16_MAX) v = INT16_MAX;
        else if (v < INT16_MIN) v = INT16_MIN;
        pcm[i] = (int16_t)v;
    }
}

int i2s_pcm_clamp_volume(int pct) {
    if (pct < 0) return 0;
    if (pct > 100) return 100;
    return pct;
}

int i2s_pcm_gain_q8(int volume_pct) {
    return (i2s_pcm_clamp_volume(volume_pct) * 256 + 50) / 100;
}

void i2s_pcm_apply_gain(const int16_t *in, int16_t *out, int n, int gain_q8) {
    for (int i = 0; i < n; i++)
        out[i] = (int16_t)(((int32_t)in[i] * gain_q8) >> 8);
}
