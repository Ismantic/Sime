#!/usr/bin/env python3
"""
生成 macOS 版本的 Sime 极简 Logo (1.3:1 比例)
"""

from PIL import Image, ImageDraw

def create_rounded_rect(draw, xy, radius, fill):
    """绘制圆角矩形"""
    x1, y1, x2, y2 = xy
    # 绘制主体矩形
    draw.rectangle([x1 + radius, y1, x2 - radius, y2], fill=fill)
    draw.rectangle([x1, y1 + radius, x2, y2 - radius], fill=fill)
    # 绘制四个圆角
    draw.ellipse([x1, y1, x1 + radius * 2, y1 + radius * 2], fill=fill)
    draw.ellipse([x2 - radius * 2, y1, x2, y1 + radius * 2], fill=fill)
    draw.ellipse([x1, y2 - radius * 2, x1 + radius * 2, y2], fill=fill)
    draw.ellipse([x2 - radius * 2, y2 - radius * 2, x2, y2], fill=fill)

def design_minimal_macos(width, height, color):
    """
    极简设计 - macOS 版本 (1.3:1 比例)
    外框比例：横边:高边 = 1.3:1
    """
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # 圆角半径基于高度计算，保持视觉协调
    radius = height // 5
    padding_x = width // 10  # 水平内边距
    padding_y = height // 8  # 垂直内边距
    
    # 绘制横向圆角矩形背景
    create_rounded_rect(draw, 
                        [padding_x, padding_y, width - padding_x, height - padding_y], 
                        radius, color)
    
    # 内部绘制三条横线代表输入
    line_color = (255, 255, 255, 255)
    
    # 计算线条区域（在背景内）
    content_x1 = padding_x + width // 8
    content_x2 = width - padding_x - width // 8
    content_y1 = padding_y + height // 6
    content_y2 = height - padding_y - height // 6
    
    content_height = content_y2 - content_y1
    line_spacing = content_height // 3
    line_height = max(1, height // 12)  # 线条粗细
    
    # 绘制三条等距横线
    for i in range(3):
        y = content_y1 + i * line_spacing + line_spacing // 2 - line_height // 2
        draw.rectangle([content_x1, y, content_x2, y + line_height], fill=line_color)
    
    return img

def main():
    # macOS 状态栏图标常用尺寸
    # 目标比例 1.3:1
    
    sizes = [
        (22, 17),   # ~1.29:1, 接近 1.3:1，适合标准状态栏
        (26, 20),   # 1.3:1，适合 Retina
        (32, 25),   # ~1.28:1，较大尺寸
        (44, 34),   # ~1.29:1，@2x  Retina
        (52, 40),   # 1.3:1，@2x 大尺寸
    ]
    
    # 青绿色配色
    color = (17, 153, 142, 255)  # #11998E
    
    print("正在生成 macOS 版本 Sime 极简 Logo (1.3:1 比例)...")
    
    for width, height in sizes:
        img = design_minimal_macos(width, height, color)
        filename = f'sime_minimal_macos_{width}x{height}.png'
        img.save(filename)
        ratio = width / height
        print(f"  生成: {filename} (比例: {ratio:.2f}:1)")
    
    # 也生成一个标准的 1.3:1 比例的 22px 高度版本
    # 22 * 1.3 = 28.6 ≈ 29
    width, height = 29, 22
    img = design_minimal_macos(width, height, color)
    img.save('sime_minimal_macos_29x22.png')
    print(f"  生成: sime_minimal_macos_29x22.png (精确 1.3:1 比例)")
    
    print("\n生成完成!")
    print("\n推荐用于 macOS 状态栏:")
    print("  - 标准分辨率: sime_minimal_macos_22x17.png 或 sime_minimal_macos_29x22.png")
    print("  - Retina (@2x): sime_minimal_macos_44x34.png")

if __name__ == '__main__':
    main()
