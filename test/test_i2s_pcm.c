#include "i2s_pcm.h"
#include <stdio.h>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
  printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// The INMP441 delivers 24-bit samples left-justified in a 32-bit I2S slot, so
// the conversion is an arithmetic right shift. The shift is the driver's gain
// knob, which is exactly why it is a parameter here instead of a magic number
// buried in each driver (they used to disagree: >>11 on the S3, >>12 on the C3).
static void test_shift_scales_sample(void) {
    const int32_t raw[] = { 0x00100000, -0x00100000, 0 };
    int16_t pcm[3] = { 1, 1, 1 };
    i2s_pcm_from_i2s32(raw, 3, 11, pcm);
    CHECK(pcm[0] == 0x0200);
    CHECK(pcm[1] == -0x0200);
    CHECK(pcm[2] == 0);
}

// One shift step is 6 dB. A driver picking 12 where another picks 11 halves
// every sample — the divergence this function exists to make visible.
static void test_one_shift_step_halves_the_sample(void) {
    const int32_t raw[] = { 0x00100000 };
    int16_t loud[1], quiet[1];
    i2s_pcm_from_i2s32(raw, 1, 11, loud);
    i2s_pcm_from_i2s32(raw, 1, 12, quiet);
    CHECK(loud[0] == quiet[0] * 2);
}

// Loud speech near full scale must saturate, not wrap: an unclamped shift
// turns a positive peak into a negative one, which is audible as a crackle
// and poison for the uplink encoder.
static void test_clamps_to_int16_range(void) {
    const int32_t raw[] = { 0x7FFFFF00, -0x7FFFFF00 };
    int16_t pcm[2];
    i2s_pcm_from_i2s32(raw, 2, 11, pcm);
    CHECK(pcm[0] == 32767);
    CHECK(pcm[1] == -32768);
}

// The negative clamp is the full int16 floor, not -32767: the two drivers
// disagreed on this too, and silently losing the most negative code is a
// (tiny, but free to avoid) asymmetry in the waveform.
static void test_negative_clamp_reaches_int16_min(void) {
    const int32_t raw[] = { -0x40000000 };
    int16_t pcm[1];
    i2s_pcm_from_i2s32(raw, 1, 11, pcm);
    CHECK(pcm[0] == -32768);
}

static void test_zero_frames_writes_nothing(void) {
    const int32_t raw[] = { 0x00100000 };
    int16_t pcm[1] = { 4242 };
    i2s_pcm_from_i2s32(raw, 0, 11, pcm);
    CHECK(pcm[0] == 4242);
}

int main(void) {
    test_shift_scales_sample();
    test_one_shift_step_halves_the_sample();
    test_clamps_to_int16_range();
    test_negative_clamp_reaches_int16_min();
    test_zero_frames_writes_nothing();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
