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
