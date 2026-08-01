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
