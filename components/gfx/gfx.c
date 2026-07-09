#include "gfx.h"

void gfx_fill_rect(uint16_t *buf, int buf_w, int buf_h,
                    int x, int y, int w, int h, uint16_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > buf_w ? buf_w : x + w;
    int y1 = y + h > buf_h ? buf_h : y + h;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            buf[py * buf_w + px] = color;
        }
    }
}

void gfx_fill_circle(uint16_t *buf, int buf_w, int buf_h,
                      int cx, int cy, int r, uint16_t color) {
    int x0 = cx - r < 0 ? 0 : cx - r;
    int y0 = cy - r < 0 ? 0 : cy - r;
    int x1 = cx + r + 1 > buf_w ? buf_w : cx + r + 1;
    int y1 = cy + r + 1 > buf_h ? buf_h : cy + r + 1;
    int r2 = r * r;
    for (int py = y0; py < y1; py++) {
        int dy = py - cy;
        for (int px = x0; px < x1; px++) {
            int dx = px - cx;
            if (dx * dx + dy * dy <= r2) buf[py * buf_w + px] = color;
        }
    }
}
