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


def autocrop(img):
    """Обрезает симметричный однотонный фон по краям (определяется по углу)."""
    rgba = img.convert("RGBA")
    w, h = rgba.size
    pixels = rgba.load()
    bg = pixels[0, 0]
    left, right, top, bottom = w, 0, h, 0
    for y in range(h):
        for x in range(w):
            if pixels[x, y] != bg:
                if x < left:  left  = x
                if x > right: right = x
                if y < top:   top   = y
                if y > bottom: bottom = y
    if left > right or top > bottom:
        return img  # всё — фон, не обрезаем
    return img.crop((left, top, right + 1, bottom + 1))


def shift_down(img, n):
    """Сдвигает изображение вниз на n пикселей, нижний край переходит наверх."""
    w, h = img.size
    n = n % h
    if n == 0:
        return img
    top    = img.crop((0, 0,   w, h - n))
    bottom = img.crop((0, h - n, w, h))
    out = Image.new(img.mode, (w, h))
    out.paste(bottom, (0, 0))
    out.paste(top,    (0, n))
    return out


def convert(input_path, var_name=None, crop=False, width=None, height=None, shift=0, cover=False,
            crop_top=0, replace_bg=None):
    img = Image.open(input_path)
    if crop:
        img = autocrop(img)
    if shift:
        img = shift_down(img, shift)
    if width or height:
        if width and height:
            if cover:
                # Scale to fill WxH keeping aspect ratio, then center-crop
                scale = max(width / img.width, height / img.height)
                iw = round(img.width  * scale)
                ih = round(img.height * scale)
                img = img.resize((iw, ih), Image.NEAREST)
                left = (iw - width)  // 2
                top  = (ih - height) // 2
                img  = img.crop((left, top, left + width, top + height))
            else:
                img = img.resize((width, height), Image.NEAREST)
        elif width:
            th = round(img.height * width / img.width)
            img = img.resize((width, th), Image.NEAREST)
        else:
            tw = round(img.width * height / img.height)
            img = img.resize((tw, height), Image.NEAREST)
    if crop_top > 0:
        img = img.crop((0, crop_top, img.width, img.height))
    img = img.convert("RGB")
    if replace_bg is not None:
        r_new, g_new, b_new = replace_bg
        bg_rgb = img.getpixel((0, 0))
        new_data = [(r_new, g_new, b_new) if p == bg_rgb else p for p in img.getdata()]
        img.putdata(new_data)
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

    return "\n".join(lines), w, h, orig_bytes, rle_bytes, ratio, crop


def main():
    parser = argparse.ArgumentParser(description="Convert PNG to RLE RGB565 C header")
    parser.add_argument("inputs", nargs="+", help="Input PNG file(s)")
    parser.add_argument("-o", "--output", help="Output .h file or directory (for batch)")
    parser.add_argument("-n", "--name", help="Variable name (single file only)")
    parser.add_argument("-c", "--crop", action="store_true",
                        help="Auto-crop solid background borders")
    parser.add_argument("-W", "--width",  type=int, help="Resize width after crop")
    parser.add_argument("-H", "--height", type=int, help="Resize height after crop")
    parser.add_argument("-S", "--shift",  type=int, default=0,
                        help="Shift image down by N pixels (wraps around)")
    parser.add_argument("--cover", action="store_true",
                        help="Scale to fill WxH keeping aspect ratio, center-crop excess")
    parser.add_argument("--crop-top", type=int, default=0, metavar="N",
                        help="Crop N pixels from the top after resize/shift")
    parser.add_argument("--replace-bg", metavar="R,G,B",
                        help="Replace background color (corner pixel) with R,G,B, e.g. 0,0,0")
    parser.add_argument("--preview", action="store_true",
                        help="Save processed PNG(s) for preview instead of .h files")
    args = parser.parse_args()

    replace_bg = None
    if args.replace_bg:
        try:
            replace_bg = tuple(int(x) for x in args.replace_bg.split(","))
            if len(replace_bg) != 3:
                raise ValueError
        except ValueError:
            print("Error: --replace-bg must be R,G,B e.g. 0,0,0", file=sys.stderr)
            sys.exit(1)

    for input_path in args.inputs:
        result, w, h, orig, rle_b, ratio, _ = convert(
            input_path,
            args.name if len(args.inputs) == 1 else None,
            crop=args.crop,
            width=args.width,
            height=args.height,
            shift=args.shift,
            cover=args.cover,
            crop_top=args.crop_top,
            replace_bg=replace_bg,
        )

        if args.preview:
            img = Image.open(input_path)
            if args.crop:  img = autocrop(img)
            if args.shift: img = shift_down(img, args.shift)
            tw = args.width  or img.width
            th = args.height or img.height
            if args.cover and args.width and args.height:
                scale = max(tw / img.width, th / img.height)
                iw, ih = round(img.width*scale), round(img.height*scale)
                img = img.resize((iw, ih), Image.NEAREST)
                img = img.crop(((iw-tw)//2, (ih-th)//2,
                                (iw-tw)//2+tw, (ih-th)//2+th))
            elif tw != img.width or th != img.height:
                img = img.resize((tw, th), Image.NEAREST)
            if args.crop_top > 0:
                img = img.crop((0, args.crop_top, img.width, img.height))
            if replace_bg is not None:
                img_rgb = img.convert("RGB")
                bg_rgb = img_rgb.getpixel((0, 0))
                new_data = [replace_bg if p == bg_rgb else p for p in img_rgb.getdata()]
                img_rgb.putdata(new_data)
                img = img_rgb
            base = os.path.splitext(os.path.basename(input_path))[0]
            out_path = base + "_preview.png"
            img.save(out_path)
            print(f"Preview: {out_path}  ({img.width}x{img.height})")
            continue

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
