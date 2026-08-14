#include "audio_selftest.h"
#include "sdkconfig.h"   // explicit: IDF does not force-include it into components,
                         // and without it the #if below silently compiles the whole
                         // file away even with the option enabled

// The whole implementation is compiled out of normal builds: the button GPIO
// below comes from a Kconfig int that only exists while AA_AUDIO_LOOPBACK is
// set, and nothing calls audio_selftest_run() otherwise. The header's
// declaration keeps this a non-empty translation unit.
#if CONFIG_AA_AUDIO_LOOPBACK

#include "loopback_logic.h"
#include "audio.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "selftest";

#define SAMPLE_RATE   16000
#define FRAME_SAMPLES   960   // 60ms — same cadence as the real mic_task, so the
                              // I2S read pattern under test matches production
#define PLAY_CHUNK     1600   // 100ms per audio_spk_write, as voice.c uses
#define MAX_SECONDS      10

// Reserve for the driver/RTOS allocations that must still succeed after we
// take our slice. Only matters on the internal-RAM fallback path.
#define INTERNAL_RAM_HEADROOM 16384

#define BTN_GPIO CONFIG_AA_AUDIO_LOOPBACK_BTN_GPIO

// One press = one edge. Called from the same loop that does the (blocking) I2S
// reads, so the poll interval swings between 20ms when idle and ~60ms while
// recording; a real press lasts far longer than either, and the settle-recheck
// below is what rejects contact bounce, not the poll rate.
static bool boot_press_edge(void) {
    static bool down = false;
    if (gpio_get_level(BTN_GPIO) != 0) { down = false; return false; }
    if (down) return false;                     // still held from a press we reported
    vTaskDelay(pdMS_TO_TICKS(30));              // settle
    if (gpio_get_level(BTN_GPIO) != 0) return false;   // bounce, not a press
    down = true;
    return true;
}

// Full take in PSRAM when the board has it; otherwise as many whole seconds of
// internal RAM as can be spared. Returns NULL if even one second won't fit —
// true on the C3, whose largest free internal block is ~9KB by the time the
// audio drivers are up.
static int16_t *alloc_take(int *capacity_samples) {
    int n = MAX_SECONDS * SAMPLE_RATE;
    int16_t *buf = heap_caps_malloc((size_t)n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (buf) {
        *capacity_samples = n;
        ESP_LOGI(TAG, "take buffer: %ds in PSRAM (%d KB)", MAX_SECONDS,
                 n * (int)sizeof(int16_t) / 1024);
        return buf;
    }
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t budget = largest > INTERNAL_RAM_HEADROOM ? largest - INTERNAL_RAM_HEADROOM : 0;
    int secs = (int)(budget / (SAMPLE_RATE * sizeof(int16_t)));
    if (secs > MAX_SECONDS) secs = MAX_SECONDS;
    if (secs < 1) {
        ESP_LOGE(TAG, "no PSRAM and only %u B of internal RAM free — need >=%u B for 1s",
                 (unsigned)largest,
                 (unsigned)(SAMPLE_RATE * sizeof(int16_t) + INTERNAL_RAM_HEADROOM));
        return NULL;
    }
    n = secs * SAMPLE_RATE;
    buf = heap_caps_malloc((size_t)n * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "internal-RAM take buffer alloc failed (%d B)",
                 n * (int)sizeof(int16_t));
        return NULL;
    }
    *capacity_samples = n;
    ESP_LOGI(TAG, "take buffer: %ds in internal RAM (%d KB) — no PSRAM on this board",
             secs, n * (int)sizeof(int16_t) / 1024);
    return buf;
}

static int frame_peak(const int16_t *pcm, int n) {
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (a > peak) peak = a;
    }
    return peak;
}

// The one number this whole mode exists to produce: peak stays 0 => the mic is
// not delivering audio (wiring/slot/clock), peak jumps when you speak => the
// mic is fine and any silence is downstream.
static void play_take(const int16_t *buf, int samples) {
    ESP_LOGI(TAG, "playback: %d samples (%d.%02ds), peak=%d",
             samples, samples / SAMPLE_RATE, (samples % SAMPLE_RATE) * 100 / SAMPLE_RATE,
             frame_peak(buf, samples));
    for (int off = 0; off < samples; ) {
        int chunk = samples - off;
        if (chunk > PLAY_CHUNK) chunk = PLAY_CHUNK;
        int w = audio_spk_write(buf + off, chunk);
        if (w < 0) {
            ESP_LOGW(TAG, "audio_spk_write failed at offset %d", off);
            break;
        }
        off += chunk;
    }
}

void audio_selftest_run(void) {
    ESP_LOGI(TAG, "audio loopback self-test — BOOT (gpio%d): press to record, press again to play back",
             BTN_GPIO);

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    int capacity = 0;
    int16_t *take = alloc_take(&capacity);
    if (!take) {
        display_show("Loopback test", "no RAM for take");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    static int16_t frame[FRAME_SAMPLES];   // 1.9KB — off this task's stack
    lb_t lb;
    lb_init(&lb, capacity);

    char l1[24], l2[24];
    snprintf(l1, sizeof l1, "Loopback %ds", capacity / SAMPLE_RATE);
    display_show(l1, "BOOT to record");

    // Refresh the recording readout ~4x/sec rather than on every 60ms frame:
    // an I2C SSD1306 write costs more than the frame interval, and starving
    // the I2S reads is exactly the failure this test must not introduce.
    int frames_since_redraw = 0;

    for (;;) {
        if (boot_press_edge()) {
            switch (lb_on_press(&lb)) {
            case LB_ACT_START_REC:
                audio_mic_flush();   // drop DMA contents so the take starts at the press
                frames_since_redraw = 0;
                ESP_LOGI(TAG, "recording...");
                display_show("REC 0.0s", "BOOT to stop");
                break;
            case LB_ACT_PLAYBACK:
                ESP_LOGI(TAG, "stopped by button");
                break;
            default:
                break;
            }
        }

        if (lb.state == LB_RECORDING) {
            int got = audio_mic_read(frame, FRAME_SAMPLES);
            if (got < 0) got = 0;
            if (got > FRAME_SAMPLES) got = FRAME_SAMPLES;

            int offset = 0, room = 0;
            lb_action_t act = lb_reserve(&lb, got, &offset, &room);
            if (room > 0) memcpy(take + offset, frame, (size_t)room * sizeof(int16_t));

            ESP_LOGI(TAG, "rec frame: got=%d stored=%d peak=%d total=%d/%d",
                     got, room, frame_peak(frame, got), lb.filled, capacity);

            if (act == LB_ACT_PLAYBACK) {
                ESP_LOGI(TAG, "stopped: buffer full (%ds cap)", capacity / SAMPLE_RATE);
            } else if (++frames_since_redraw >= 4) {
                frames_since_redraw = 0;
                snprintf(l1, sizeof l1, "REC %d.%ds", lb.filled / SAMPLE_RATE,
                         (lb.filled % SAMPLE_RATE) * 10 / SAMPLE_RATE);
                display_show(l1, "BOOT to stop");
            }
        }

        // Not an `else`: the press handled above, and the cap reached just
        // above it, both land here in the SAME iteration — the take plays
        // immediately instead of waiting for another trip round the loop.
        if (lb.state == LB_PLAYING) {
            snprintf(l2, sizeof l2, "%d.%ds", lb.filled / SAMPLE_RATE,
                     (lb.filled % SAMPLE_RATE) * 10 / SAMPLE_RATE);
            display_show("PLAY", l2);
            play_take(take, lb.filled);
            lb_playback_done(&lb);
            snprintf(l1, sizeof l1, "Loopback %ds", capacity / SAMPLE_RATE);
            display_show(l1, "BOOT to record");
        }

        if (lb.state == LB_IDLE) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#endif  // CONFIG_AA_AUDIO_LOOPBACK
