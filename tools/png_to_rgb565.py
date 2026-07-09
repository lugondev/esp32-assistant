#!/usr/bin/env python3
# Dev-time tool (not firmware): converts a PNG to a C array of RGB565 pixels
# for display_flush(). Requires Pillow: pip install pillow
#
# Usage: python3 png_to_rgb565.py background.png background_img > components/boards/<board>/background_img.c
#
# The generated array is row-major, w*h uint16_t, matching what
# display_flush(x, y, w, h, pixels) expects.
import sys
from PIL import Image

def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <input.png> <c_identifier>", file=sys.stderr)
        sys.exit(1)
    path, ident = sys.argv[1], sys.argv[2]

    img = Image.open(path).convert("RGB")
    w, h = img.size
    pixels = img.load()

    print('#include <stdint.h>')
    print(f'const int {ident}_width = {w};')
    print(f'const int {ident}_height = {h};')
    print(f'const uint16_t {ident}[{w * h}] = {{')
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b = pixels[x, y]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            row.append(str(rgb565))
        print('    ' + ','.join(row) + ',')
    print('};')

if __name__ == '__main__':
    main()
