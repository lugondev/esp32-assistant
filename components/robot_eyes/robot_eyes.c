#include "robot_eyes.h"
#include "gfx.h"

bool robot_eyes_is_closed(uint32_t now_ms) {
    return (now_ms % ROBOT_EYES_BLINK_INTERVAL_MS) < ROBOT_EYES_BLINK_DURATION_MS;
}

void robot_eyes_dirty_band(int panel_h, int *y, int *height) {
    int r  = panel_h / 4;
    int cy = panel_h / 2;
    *y = cy - r;
    *height = 2 * r;
}

void robot_eyes_render(uint16_t *buf, int buf_w, int buf_rows, int panel_h,
                        int y_offset, uint32_t now_ms,
                        uint16_t eye_color, uint16_t bg_color) {
    gfx_fill_rect(buf, buf_w, buf_rows, 0, 0, buf_w, buf_rows, bg_color);

    int r  = panel_h / 4;
    int cy = panel_h / 2 - y_offset;   // panel-space center, translated to buf-local rows
    int left_cx  = buf_w / 4;
    int right_cx = 3 * buf_w / 4;

    // Coordinates may fall partly or fully outside [0,buf_rows) when buf is
    // a crop (e.g. y_offset past the eyes entirely) — gfx's own clipping
    // silently skips out-of-range rows/cols, same as a full-panel buffer.
    if (!robot_eyes_is_closed(now_ms)) {
        gfx_fill_circle(buf, buf_w, buf_rows, left_cx,  cy, r, eye_color);
        gfx_fill_circle(buf, buf_w, buf_rows, right_cx, cy, r, eye_color);
    } else {
        int band_h = r / 3;
        if (band_h < 1) band_h = 1;
        gfx_fill_rect(buf, buf_w, buf_rows, left_cx  - r, cy - band_h / 2, 2 * r, band_h, eye_color);
        gfx_fill_rect(buf, buf_w, buf_rows, right_cx - r, cy - band_h / 2, 2 * r, band_h, eye_color);
    }
}
