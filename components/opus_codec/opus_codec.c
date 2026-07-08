#include "opus_codec.h"
#include "opus.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "opus";
static OpusEncoder *s_enc;
static OpusDecoder *s_dec;

esp_err_t opus_codec_init(void) {
    int err = 0;
    s_enc = opus_encoder_create(OPUS_UP_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !s_enc) { ESP_LOGE(TAG, "enc create %d", err); return ESP_FAIL; }
    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(s_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    // Lower complexity: much less CPU (and some stack) on ESP32 for a barely
    // perceptible quality drop at voice bitrates — standard for embedded Opus.
    opus_encoder_ctl(s_enc, OPUS_SET_COMPLEXITY(3));
    s_dec = opus_decoder_create(OPUS_DOWN_RATE, 1, &err);
    if (err != OPUS_OK || !s_dec) { ESP_LOGE(TAG, "dec create %d", err); return ESP_FAIL; }
    return ESP_OK;
}

int opus_codec_encode(const int16_t *pcm960, uint8_t *out, int out_cap) {
    int64_t t0 = esp_timer_get_time();  // DIAGNOSTIC
    int n = opus_encode(s_enc, pcm960, OPUS_UP_SAMPLES, out, out_cap);
    static int dbg = 0;  // DIAGNOSTIC: encode must fit under the 60ms (60000us) frame
    if ((++dbg % 50) == 0) ESP_LOGW(TAG, "encode %d us -> %d bytes", (int)(esp_timer_get_time() - t0), n);
    return n < 0 ? -1 : n;
}

int opus_codec_decode(const uint8_t *pkt, int pkt_len, int16_t *pcm_out) {
    // Caller's pcm_out must be OPUS_DOWN_SAMPLES_MAX (120ms) — a single Opus
    // packet may decode to up to that many samples, and opus_decode writes
    // as many as the packet contains up to this cap.
    int n = opus_decode(s_dec, pkt, pkt_len, pcm_out, OPUS_DOWN_SAMPLES_MAX, 0);
    return n < 0 ? -1 : n;
}
