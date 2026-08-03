#include "board.h"
#include "esp_log.h"
#include "sdkconfig.h"

// Boundary symbols of the "board_desc" section (see components/boards/linker.lf).
extern const board_t *const _board_desc_start[];
extern const board_t *const _board_desc_end[];

static const char *TAG = "board";

esp_err_t board_select_configured(void) {
    int n = (int)(_board_desc_end - _board_desc_start);
    const board_t *b = board_select(_board_desc_start, n, CONFIG_AA_BOARD_NAME);
    if (b == NULL) {
        ESP_LOGE(TAG, "no board named \"%s\" among %d registered",
                 CONFIG_AA_BOARD_NAME, n);
        return ESP_ERR_NOT_FOUND;
    }
    board_set(b);
    ESP_LOGI(TAG, "board: %s (registered=%d)", b->name, n);
    return ESP_OK;
}
