#include "opus_codec.h"
#include "opus.h"
#include "esp_log.h"

static const char *TAG = "opus";
static OpusEncoder *s_enc;
static OpusDecoder *s_dec;

esp_err_t opus_codec_init(void) {
    int err = 0;
    s_enc = opus_encoder_create(OPUS_UP_RATE, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !s_enc) { ESP_LOGE(TAG, "enc create %d", err); return ESP_FAIL; }
    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(s_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    s_dec = opus_decoder_create(OPUS_DOWN_RATE, 1, &err);
    if (err != OPUS_OK || !s_dec) { ESP_LOGE(TAG, "dec create %d", err); return ESP_FAIL; }
    return ESP_OK;
}

int opus_codec_encode(const int16_t *pcm960, uint8_t *out, int out_cap) {
    int n = opus_encode(s_enc, pcm960, OPUS_UP_SAMPLES, out, out_cap);
    return n < 0 ? -1 : n;
}

int opus_codec_decode(const uint8_t *pkt, int pkt_len, int16_t *pcm_out) {
    int n = opus_decode(s_dec, pkt, pkt_len, pcm_out, OPUS_DOWN_SAMPLES, 0);
    return n < 0 ? -1 : n;
}
