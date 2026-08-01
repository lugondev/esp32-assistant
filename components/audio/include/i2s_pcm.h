#pragma once
#include <stdint.h>

// Shared I2S sample conversion for the mic drivers.
//
// Both mic drivers (i2s_mic.c on dual-I2S SoCs, i2s_fd.c on single-I2S ones)
// read the SAME microphone — an INMP441, which always clocks 32 SCK per WS
// half-period and delivers ~24-bit data left-justified in that slot. Turning
// one of those slots into a 16-bit PCM sample is therefore identical work on
// every target, and it lived as two hand-copied loops that had quietly drifted
// apart on both the gain shift (11 vs 12, i.e. 6 dB) and the negative clamp
// (-32768 vs -32767).
//
// Pure and target-independent, so it is host-tested (test/test_i2s_pcm.c) —
// the drivers around it are not.
//
// `gain_shift` stays a parameter rather than a constant here because it is a
// real per-board tuning knob (mic distance, enclosure, amp gain), not an
// accident. Each driver names its own value so the choice is visible at the
// call site instead of being buried in this loop.
void i2s_pcm_from_i2s32(const int32_t *raw, int frames, int gain_shift,
                        int16_t *pcm);

// --- software output volume ------------------------------------------------
// The MAX98357A has no hardware volume control, so the speaker drivers scale
// samples themselves on the way to the DMA. Both drivers used to carry their
// own copy of all three of these.

// Clamp a requested level to 0..100.
int i2s_pcm_clamp_volume(int pct);

// Convert a 0..100 level to a Q8 multiplier (100% -> 256). Q8 rather than a
// per-sample divide by 100: this scales 960 samples per 60 ms frame, and a
// shift is free where the divide is not. The rounding differs from integer
// division by at most 1 LSB, which is inaudible at any level.
int i2s_pcm_gain_q8(int volume_pct);

// Scale `n` samples by a Q8 gain. in and out may be the same buffer.
// No clamping: gain_q8 comes from i2s_pcm_gain_q8, which never exceeds unity,
// so the product cannot leave int16 range.
void i2s_pcm_apply_gain(const int16_t *in, int16_t *out, int n, int gain_q8);
