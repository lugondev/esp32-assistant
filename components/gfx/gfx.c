#include "gfx.h"
#include <stdbool.h>

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

void gfx_fill_rounded_rect(uint16_t *buf, int buf_w, int buf_h,
                            int x, int y, int w, int h, int r, uint16_t color) {
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    if (r < 0) r = 0;
    int r2 = r * r;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > buf_w ? buf_w : x + w;
    int y1 = y + h > buf_h ? buf_h : y + h;
    for (int py = y0; py < y1; py++) {
        int dy = py - y;   // local row within the rect, 0..h-1
        for (int px = x0; px < x1; px++) {
            int dx = px - x;   // local col within the rect, 0..w-1
            int cx = -1, cy = -1;   // nearest corner arc center, local coords
            if (dx < r && dy < r)               { cx = r;     cy = r; }
            else if (dx >= w - r && dy < r)      { cx = w-r-1; cy = r; }
            else if (dx < r && dy >= h - r)      { cx = r;     cy = h-r-1; }
            else if (dx >= w - r && dy >= h - r) { cx = w-r-1; cy = h-r-1; }
            bool inside;
            if (cx >= 0) {
                int ddx = dx - cx, ddy = dy - cy;
                inside = (ddx * ddx + ddy * ddy) <= r2;
            } else {
                inside = true;   // flat edge / interior — not near any corner
            }
            if (inside) buf[py * buf_w + px] = color;
        }
    }
}

void gfx_fill_triangle(uint16_t *buf, int buf_w, int buf_h,
                        int x0, int y0, int x1, int y1, int x2, int y2,
                        uint16_t color) {
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int cx0 = minx < 0 ? 0 : minx;
    int cy0 = miny < 0 ? 0 : miny;
    int cx1 = maxx + 1 > buf_w ? buf_w : maxx + 1;
    int cy1 = maxy + 1 > buf_h ? buf_h : maxy + 1;
    for (int py = cy0; py < cy1; py++) {
        for (int px = cx0; px < cx1; px++) {
            // Edge-function (signed area) test against each side; a point is
            // inside iff all three signs agree (all >=0 or all <=0) — this
            // works for either winding order since reversing it just flips
            // all three signs together.
            int e0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
            int e1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
            int e2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
            bool all_nonneg = e0 >= 0 && e1 >= 0 && e2 >= 0;
            bool all_nonpos = e0 <= 0 && e1 <= 0 && e2 <= 0;
            if (all_nonneg || all_nonpos) buf[py * buf_w + px] = color;
        }
    }
}
