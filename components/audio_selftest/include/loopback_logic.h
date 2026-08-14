#pragma once
#include <stdbool.h>

// Pure, host-testable record/playback sequencing for the mic->speaker
// self-test — no GPIO, I2S or RTOS access. The caller owns the actual PCM
// buffer and the hardware calls; this decides only *when* to record, *where*
// in the buffer the next captured frame goes, and *how many* of its samples
// still fit.
//
// The capacity arithmetic is the reason this is a separate unit: the last
// frame of a recording is almost always partial (a 960-sample frame arriving
// with 300 samples of room left), and getting that clamp wrong overruns a
// 320KB PSRAM buffer on real hardware — a bug that is miserable to find on a
// device and trivial to pin down here.

typedef enum {
    LB_IDLE,        // waiting for the press that starts a recording
    LB_RECORDING,   // capturing into the buffer
    LB_PLAYING,     // buffer closed; caller is playing `filled` samples back
} lb_state_t;

typedef enum {
    LB_ACT_NONE,        // nothing for the caller to do
    LB_ACT_START_REC,   // begin capturing frames
    LB_ACT_PLAYBACK,    // stop capturing and play `filled` samples back
} lb_action_t;

typedef struct {
    lb_state_t state;
    int capacity;   // total samples the caller's buffer holds
    int filled;     // samples recorded so far this take
} lb_t;

// `capacity` is the caller's buffer size in samples (not bytes).
void lb_init(lb_t *lb, int capacity);

// One button press. IDLE -> starts a recording (discarding the previous take);
// RECORDING -> stops and asks for playback.
lb_action_t lb_on_press(lb_t *lb);

// Claim room for a freshly captured frame of `avail` samples.
// *offset = index in the caller's buffer to copy them to.
// *take   = how many of the `avail` samples fit (0..avail).
// Returns LB_ACT_PLAYBACK on the frame that fills the buffer — the caller
// copies *take samples first, then plays — otherwise LB_ACT_NONE. That
// transition fires at most once per take: a further reserve while the buffer
// is full yields take=0 and LB_ACT_NONE, so a caller that reads one more
// frame before servicing the playback can't restart it.
// Outside LB_RECORDING this always yields take=0 and LB_ACT_NONE.
lb_action_t lb_reserve(lb_t *lb, int avail, int *offset, int *take);

// Playback finished; back to LB_IDLE, ready for the next press.
void lb_playback_done(lb_t *lb);
