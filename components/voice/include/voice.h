#pragma once

typedef enum {
    VOICE_SETUP,
    VOICE_CONNECTING,
    VOICE_CONNECTED,
} voice_clip_t;

// Blocking playback of a pre-recorded status announcement (16kHz mono
// PCM16), via the existing audio_spk_write(). Requires audio_init() to
// have already run. Takes as long as the clip's real-time duration
// (roughly 2-4 seconds) — call only from non-latency-sensitive contexts.
void voice_play(voice_clip_t clip);
