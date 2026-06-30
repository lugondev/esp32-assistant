#pragma once
#include "esp_err.h"
#include "ws_protocol.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ws_event_cb_t)(const wsp_event_t *ev);
typedef void (*ws_audio_cb_t)(const uint8_t *data, int len);

esp_err_t ws_client_start(const wsp_config_t *cfg,
                          ws_event_cb_t on_event, ws_audio_cb_t on_audio);
int ws_client_send_audio(const uint8_t *opus, int len);
int ws_client_send_control(const char *type);
bool ws_client_connected(void);
