#pragma once
#include "esp_log.h"

// Panel bring-up guard, shared by both drivers (they each had a verbatim copy).
//
// Every step of a panel init is non-fatal: log it and return the error so
// display_init()'s caller can run headless. main.c's call site promises this
// ("a missing/miswired panel must not boot-loop the device"), and using
// ESP_ERROR_CHECK here — which aborts — is exactly how that promise got broken
// once already.
//
// Expects a `TAG` in scope, like the ESP_LOG macros it wraps.
#define DISP_TRY(expr) do {                                                   \
        esp_err_t _e = (expr);                                                \
        if (_e != ESP_OK) {                                                   \
            ESP_LOGE(TAG, "display init: %s failed (%s) — running headless",  \
                     #expr, esp_err_to_name(_e));                             \
            return _e;                                                        \
        }                                                                     \
    } while (0)
