#include "voice.h"
#include "audio.h"
#include <stdint.h>
#include <stddef.h>

extern const uint8_t voice_setup_pcm_start[]      asm("_binary_voice_setup_pcm_start");
extern const uint8_t voice_setup_pcm_end[]        asm("_binary_voice_setup_pcm_end");
extern const uint8_t voice_connecting_pcm_start[] asm("_binary_voice_connecting_pcm_start");
extern const uint8_t voice_connecting_pcm_end[]   asm("_binary_voice_connecting_pcm_end");
extern const uint8_t voice_connected_pcm_start[]  asm("_binary_voice_connected_pcm_start");
extern const uint8_t voice_connected_pcm_end[]    asm("_binary_voice_connected_pcm_end");

#define VOICE_CHUNK_SAMPLES 1600  // 100ms @ 16kHz mono — matches this project's
                                  // existing convention of bounded, chunked writes
                                  // rather than one large write.

static void play_pcm(const uint8_t *start, const uint8_t *end) {
    const int16_t *pcm = (const int16_t *)start;
    size_t total_samples = ((size_t)(end - start)) / sizeof(int16_t);
    size_t offset = 0;
    while (offset < total_samples) {
        size_t chunk = total_samples - offset;
        if (chunk > VOICE_CHUNK_SAMPLES) chunk = VOICE_CHUNK_SAMPLES;
        audio_spk_write(pcm + offset, (int)chunk);
        offset += chunk;
    }
}

void voice_play(voice_clip_t clip) {
    switch (clip) {
    case VOICE_SETUP:      play_pcm(voice_setup_pcm_start, voice_setup_pcm_end); break;
    case VOICE_CONNECTING: play_pcm(voice_connecting_pcm_start, voice_connecting_pcm_end); break;
    case VOICE_CONNECTED:  play_pcm(voice_connected_pcm_start, voice_connected_pcm_end); break;
    default: break;
    }
}
