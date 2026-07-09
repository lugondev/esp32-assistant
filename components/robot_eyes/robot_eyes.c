#include "robot_eyes.h"
#include "gfx.h"

bool robot_eyes_is_closed(uint32_t now_ms) {
    return (now_ms % ROBOT_EYES_BLINK_INTERVAL_MS) < ROBOT_EYES_BLINK_DURATION_MS;
}

void robot_eyes_render(uint16_t *buf, int buf_w, int buf_h, uint32_t now_ms,
                        uint16_t eye_color, uint16_t bg_color) {
    gfx_fill_rect(buf, buf_w, buf_h, 0, 0, buf_w, buf_h, bg_color);

    int r  = buf_h / 4;
    int cy = buf_h / 2;
    int left_cx  = buf_w / 4;
    int right_cx = 3 * buf_w / 4;

    if (!robot_eyes_is_closed(now_ms)) {
        gfx_fill_circle(buf, buf_w, buf_h, left_cx,  cy, r, eye_color);
        gfx_fill_circle(buf, buf_w, buf_h, right_cx, cy, r, eye_color);
    } else {
        int band_h = r / 3;
        if (band_h < 1) band_h = 1;
        gfx_fill_rect(buf, buf_w, buf_h, left_cx  - r, cy - band_h / 2, 2 * r, band_h, eye_color);
        gfx_fill_rect(buf, buf_w, buf_h, right_cx - r, cy - band_h / 2, 2 * r, band_h, eye_color);
    }
}
