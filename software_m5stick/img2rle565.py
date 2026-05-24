#!/usr/bin/env python3
"""
Конвертер PNG -> RLE-сжатый RGB565 C-заголовок для ESP32/ST7789
Использование:
  python img2rle565.py image.png                    # stdout
  python img2rle565.py image.png -o out.h           # один файл
  python img2rle565.py *.png -o include/sprites/    # batch в папку
"""

import argparse
import os
import sys
from PIL import Image


def rgb_to_rgb565(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v >> 8) | ((v & 0xFF) << 8)) & 0xFFFF  # byte-swap for ST7789 SPI


def encode_rle(pixels):
    rle = []
    i = 0
    while i < len(pixels):
        color = pixels[i]
        count = 1
        while i + count < len(pixels) and pixels[i + count] == color and count < 255:
            count += 1
        rle.append((count, color))
        i += count
    return rle


def convert(input_path, var_name=None):
    img = Image.open(input_path).convert("RGB")
    w, h = img.size

    pixels = []
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))
            pixels.append(rgb_to_rgb565(r, g, b))

    rle = encode_rle(pixels)

    if not var_name:
        var_name = os.path.splitext(os.path.basename(input_path))[0]
        var_name = "".join(c if c.isalnum() or c == "_" else "_" for c in var_name)

    orig_bytes = w * h * 2
    rle_bytes = len(rle) * 3  # uint8 count + uint16 color
    ratio = rle_bytes / orig_bytes * 100

    lines = []
    lines.append(f"// Generated from: {os.path.basename(input_path)}")
    lines.append(f"// Original: {orig_bytes} bytes | RLE: {rle_bytes} bytes | Ratio: {ratio:.1f}%")
    lines.append(f"// Size: {w}x{h} px, {len(rle)} RLE entries")
    lines.append(f"")
    lines.append(f"#pragma once")
    lines.append(f"#include <stdint.h>")
    lines.append(f"")
    lines.append(f"#define {var_name.upper()}_WIDTH   {w}")
    lines.append(f"#define {var_name.upper()}_HEIGHT  {h}")
    lines.append(f"#define {var_name.upper()}_RLE_LEN {len(rle)}")
    lines.append(f"")

    # counts array
    lines.append(f"static const uint8_t {var_name}_rle_counts[{len(rle)}] = {{")
    row = []
    for i, (cnt, _) in enumerate(rle):
        row.append(str(cnt))
        if len(row) == 20 or i == len(rle) - 1:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    lines.append("};")
    lines.append(f"")

    # colors array
    lines.append(f"static const uint16_t {var_name}_rle_colors[{len(rle)}] = {{")
    row = []
    for i, (_, col) in enumerate(rle):
        row.append(f"0x{col:04X}")
        if len(row) == 12 or i == len(rle) - 1:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    lines.append("};")
    lines.append(f"")

    return "\n".join(lines), w, h, orig_bytes, rle_bytes, ratio


def main():
    parser = argparse.ArgumentParser(description="Convert PNG to RLE RGB565 C header")
    parser.add_argument("inputs", nargs="+", help="Input PNG file(s)")
    parser.add_argument("-o", "--output", help="Output .h file or directory (for batch)")
    parser.add_argument("-n", "--name", help="Variable name (single file only)")
    args = parser.parse_args()

    for input_path in args.inputs:
        result, w, h, orig, rle_b, ratio = convert(input_path, args.name if len(args.inputs) == 1 else None)

        if args.output:
            if os.path.isdir(args.output) or args.output.endswith("/") or args.output.endswith("\\"):
                os.makedirs(args.output, exist_ok=True)
                base = os.path.splitext(os.path.basename(input_path))[0]
                out_path = os.path.join(args.output, base + ".h")
            else:
                out_path = args.output
            with open(out_path, "w") as f:
                f.write(result)
            print(f"{os.path.basename(input_path)}: {w}x{h}, {orig} -> {rle_b} bytes ({ratio:.1f}%) -> {out_path}")
        else:
            print(result)
            print(f"// Stats: {w}x{h}, {orig} -> {rle_b} bytes ({ratio:.1f}%)", file=sys.stderr)


if __name__ == "__main__":
    main()
