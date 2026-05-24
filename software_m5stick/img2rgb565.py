#!/usr/bin/env python3
"""
Конвертер изображений в C-массив RGB565 для ESP32/ST7789
Использование: python img2rgb565.py image.png -o output.h -w 32 -h 32
"""

import argparse
from PIL import Image
import os

def rgb_to_rgb565(r, g, b, swap_bytes=True):
    """Конвертирует RGB888 в RGB565"""
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    if swap_bytes:
        rgb565 = ((rgb565 >> 8) | ((rgb565 & 0xFF) << 8)) & 0xFFFF
    return rgb565

def convert_image(input_path, width=None, height=None, swap_bytes=True, var_name=None):
    """Конвертирует изображение в C-массив"""
    
    img = Image.open(input_path)
    
    # Конвертируем в RGB если нужно
    if img.mode != 'RGB':
        img = img.convert('RGB')
    
    # Изменяем размер если указано
    if width and height:
        img = img.resize((width, height), Image.Resampling.LANCZOS)
    elif width:
        ratio = width / img.width
        img = img.resize((width, int(img.height * ratio)), Image.Resampling.LANCZOS)
    elif height:
        ratio = height / img.height
        img = img.resize((int(img.width * ratio), height), Image.Resampling.LANCZOS)
    
    w, h = img.size
    pixels = list(img.getdata())
    
    # Имя переменной из имени файла
    if not var_name:
        var_name = os.path.splitext(os.path.basename(input_path))[0]
        var_name = ''.join(c if c.isalnum() else '_' for c in var_name)
    
    # Генерируем C-код
    output = []
    output.append(f"// Generated from: {os.path.basename(input_path)}")
    output.append(f"// Size: {w}x{h} pixels")
    output.append(f"// Format: RGB565 {'(byte-swapped)' if swap_bytes else ''}")
    output.append(f"")
    output.append(f"#define {var_name.upper()}_WIDTH  {w}")
    output.append(f"#define {var_name.upper()}_HEIGHT {h}")
    output.append(f"")
    output.append(f"const uint16_t {var_name}[{w * h}] = {{")
    
    # Конвертируем пиксели
    line = "    "
    for i, (r, g, b) in enumerate(pixels):
        rgb565 = rgb_to_rgb565(r, g, b, swap_bytes)
        line += f"0x{rgb565:04X},"
        
        if (i + 1) % 12 == 0:  # 12 значений на строку
            output.append(line)
            line = "    "
    
    if line.strip():
        output.append(line)
    
    output.append("};")
    
    return '\n'.join(output), w, h

def main():
    parser = argparse.ArgumentParser(description='Convert image to RGB565 C array')
    parser.add_argument('input', help='Input image file (PNG, JPG, etc.)')
    parser.add_argument('-o', '--output', help='Output .h file (default: stdout)')
    parser.add_argument('-W', '--width', type=int, help='Resize width')
    parser.add_argument('-H', '--height', type=int, help='Resize height')
    parser.add_argument('-n', '--name', help='Variable name (default: from filename)')
    parser.add_argument('--no-swap', action='store_true', help='Disable byte swapping')
    
    args = parser.parse_args()
    
    result, w, h = convert_image(
        args.input,
        width=args.width,
        height=args.height,
        swap_bytes=not args.no_swap,
        var_name=args.name
    )
    
    if args.output:
        with open(args.output, 'w') as f:
            f.write(result)
        print(f"Saved: {args.output} ({w}x{h}, {w*h*2} bytes)")
    else:
        print(result)

if __name__ == '__main__':
    main()