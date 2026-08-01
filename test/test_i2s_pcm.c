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

// --- software output volume ------------------------------------------------
// The MAX98357A has no hardware volume, so both speaker drivers scale samples
// on the way out. They used to carry identical copies of the clamp, the Q8
// gain conversion and the scaling loop.

static void test_volume_clamps_to_percent_range(void) {
    CHECK(i2s_pcm_clamp_volume(-5) == 0);
    CHECK(i2s_pcm_clamp_volume(0) == 0);
    CHECK(i2s_pcm_clamp_volume(50) == 50);
    CHECK(i2s_pcm_clamp_volume(100) == 100);
    CHECK(i2s_pcm_clamp_volume(150) == 100);
}

// Q8 fixed point instead of a divide by 100: this runs 960 times per 60 ms
// frame, and the C3 has no cycles to spare for an integer divide per sample.
static void test_gain_q8_maps_percent_to_fixed_point(void) {
    CHECK(i2s_pcm_gain_q8(0) == 0);
    CHECK(i2s_pcm_gain_q8(100) == 256);
    CHECK(i2s_pcm_gain_q8(50) == 128);
}

static void test_apply_gain_at_unity_is_identity(void) {
    const int16_t in[] = { -32768, -1, 0, 1, 32767 };
    int16_t out[5];
    i2s_pcm_apply_gain(in, out, 5, i2s_pcm_gain_q8(100));
    for (int i = 0; i < 5; i++) CHECK(out[i] == in[i]);
}

static void test_apply_gain_scales_samples(void) {
    const int16_t in[] = { 1000, -1000 };
    int16_t out[2];
    i2s_pcm_apply_gain(in, out, 2, i2s_pcm_gain_q8(50));
    CHECK(out[0] == 500);
    CHECK(out[1] == -500);
}

int main(void) {
    test_shift_scales_sample();
    test_one_shift_step_halves_the_sample();
    test_clamps_to_int16_range();
    test_negative_clamp_reaches_int16_min();
    test_zero_frames_writes_nothing();
    test_volume_clamps_to_percent_range();
    test_gain_q8_maps_percent_to_fixed_point();
    test_apply_gain_at_unity_is_identity();
    test_apply_gain_scales_samples();
    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
