#!/usr/bin/env python3
"""
生成 Sime (是语输入法) 的状态栏 Logo
"""

from PIL import Image, ImageDraw, ImageFont
import os

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

def design_character_shi(size, bg_color, fg_color):
    """设计1: 使用'是'字的简约设计"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    padding = size // 8
    create_rounded_rect(draw, [padding, padding, size - padding, size - padding], 
                        size // 6, bg_color)
    
    # 尝试加载中文字体，如果没有则使用默认字体
    try:
        # 尝试常见的中文字体路径
        font_paths = [
            "/System/Library/Fonts/PingFang.ttc",  # macOS
            "/System/Library/Fonts/STHeiti Light.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",  # Linux
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        ]
        font = None
        for fp in font_paths:
            if os.path.exists(fp):
                font = ImageFont.truetype(fp, size // 2)
                break
        if font is None:
            font = ImageFont.load_default()
    except:
        font = ImageFont.load_default()
    
    text = "是"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    x = (size - text_w) // 2
    y = (size - text_h) // 2 - size // 12
    
    draw.text((x, y), text, font=font, fill=fg_color)
    return img

def design_letter_s(size, bg_color, fg_color):
    """设计2: 使用字母'S'的现代设计"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    padding = size // 8
    
    # 绘制圆角方形背景
    create_rounded_rect(draw, [padding, padding, size - padding, size - padding], 
                        size // 5, bg_color)
    
    # 绘制字母 S
    try:
        font_paths = [
            "/System/Library/Fonts/Helvetica.ttc",
            "/System/Library/Fonts/SF-Pro.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        ]
        font = None
        for fp in font_paths:
            if os.path.exists(fp):
                font = ImageFont.truetype(fp, int(size * 0.55))
                break
        if font is None:
            font = ImageFont.load_default()
    except:
        font = ImageFont.load_default()
    
    text = "S"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    x = (size - text_w) // 2
    y = (size - text_h) // 2 - size // 20
    
    draw.text((x, y), text, font=font, fill=fg_color)
    return img

def design_pinyin_yu(size, primary_color, secondary_color):
    """设计3: 使用'语'拼音'yu'的抽象设计"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    padding = size // 6
    center_x = size // 2
    center_y = size // 2
    
    # 绘制外圆
    r = size // 2 - padding
    draw.ellipse([center_x - r, center_y - r, center_x + r, center_y + r], 
                 fill=primary_color)
    
    # 绘制内部的"语"抽象 - 使用简单的几何形状
    inner_padding = size // 4
    
    # 绘制两个小圆点代表"语"的抽象
    dot_r = size // 12
    # 上面两点
    draw.ellipse([center_x - size//6 - dot_r, center_y - size//6 - dot_r,
                  center_x - size//6 + dot_r, center_y - size//6 + dot_r], fill=secondary_color)
    draw.ellipse([center_x + size//6 - dot_r, center_y - size//6 - dot_r,
                  center_x + size//6 + dot_r, center_y - size//6 + dot_r], fill=secondary_color)
    
    # 下面一条横线
    line_y = center_y + size // 8
    draw.rectangle([center_x - size//4, line_y - size//16, 
                    center_x + size//4, line_y + size//16], fill=secondary_color)
    
    return img

def design_minimal_geometric(size, color):
    """设计4: 极简几何设计 - 代表输入和语言"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    padding = size // 8
    
    # 绘制圆角方形
    create_rounded_rect(draw, [padding, padding, size - padding, size - padding], 
                        size // 6, color)
    
    # 内部绘制简单的线条代表输入
    line_color = (255, 255, 255, 255)
    margin = size // 3
    line_spacing = size // 6
    
    for i in range(3):
        y = margin + i * line_spacing
        draw.rectangle([margin, y - size//32, size - margin, y + size//32], fill=line_color)
    
    return img

def design_yu_character(size, bg_color, fg_color):
    """设计5: 使用'语'字的设计"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    padding = size // 10
    
    # 绘制圆形背景
    center = size // 2
    r = size // 2 - padding
    draw.ellipse([center - r, center - r, center + r, center + r], fill=bg_color)
    
    # 绘制"语"字
    try:
        font_paths = [
            "/System/Library/Fonts/PingFang.ttc",
            "/System/Library/Fonts/STHeiti Light.ttc",
            "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        ]
        font = None
        for fp in font_paths:
            if os.path.exists(fp):
                font = ImageFont.truetype(fp, size // 2)
                break
        if font is None:
            font = ImageFont.load_default()
    except:
        font = ImageFont.load_default()
    
    text = "语"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    x = (size - text_w) // 2
    y = (size - text_h) // 2 - size // 12
    
    draw.text((x, y), text, font=font, fill=fg_color)
    return img

def generate_svg_logo(filename, design_type="shi"):
    """生成 SVG 矢量 logo"""
    
    if design_type == "shi":
        svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="grad1" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#4A90D9;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#2E5C8A;stop-opacity:1" />
    </linearGradient>
  </defs>
  <rect x="4" y="4" width="56" height="56" rx="12" ry="12" fill="url(#grad1)"/>
  <text x="32" y="44" font-family="PingFang SC, Microsoft YaHei, sans-serif" font-size="32" 
        font-weight="bold" text-anchor="middle" fill="white">是</text>
</svg>'''
    elif design_type == "s":
        svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="grad2" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#5B8DEF;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#3D6BC4;stop-opacity:1" />
    </linearGradient>
  </defs>
  <rect x="4" y="4" width="56" height="56" rx="14" ry="14" fill="url(#grad2)"/>
  <text x="32" y="46" font-family="Helvetica Neue, Arial, sans-serif" font-size="38" 
        font-weight="bold" text-anchor="middle" fill="white">S</text>
</svg>'''
    elif design_type == "yu":
        svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="grad3" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#667EEA;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#764BA2;stop-opacity:1" />
    </linearGradient>
  </defs>
  <circle cx="32" cy="32" r="28" fill="url(#grad3)"/>
  <circle cx="22" cy="26" r="5" fill="white"/>
  <circle cx="42" cy="26" r="5" fill="white"/>
  <rect x="18" y="40" width="28" height="5" rx="2" fill="white"/>
</svg>'''
    elif design_type == "minimal":
        svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg width="64" height="64" viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="grad4" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#11998E;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#38EF7D;stop-opacity:1" />
    </linearGradient>
  </defs>
  <rect x="4" y="4" width="56" height="56" rx="12" ry="12" fill="url(#grad4)"/>
  <rect x="16" y="20" width="32" height="4" rx="2" fill="white"/>
  <rect x="16" y="30" width="32" height="4" rx="2" fill="white"/>
  <rect x="16" y="40" width="32" height="4" rx="2" fill="white"/>
</svg>'''
    
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(svg_content)

def main():
    # 配色方案
    colors = {
        'blue': ((74, 144, 217, 255), (255, 255, 255, 255)),      # 蓝底白字
        'indigo': ((91, 141, 239, 255), (255, 255, 255, 255)),    # 靛蓝
        'purple': ((102, 126, 234, 255), (255, 255, 255, 255)),   # 紫蓝渐变起点
        'teal': ((17, 153, 142, 255), (255, 255, 255, 255)),      # 青绿
        'dark': ((45, 55, 72, 255), (255, 255, 255, 255)),        # 深色
    }
    
    sizes = [16, 22, 24, 32, 48, 64, 128, 256]
    
    print("正在生成 Sime 输入法 Logo...")
    
    # 创建各个尺寸的 PNG
    for size in sizes:
        # 设计1: "是"字
        img = design_character_shi(size, colors['blue'][0], colors['blue'][1])
        img.save(f'sime_shi_{size}x{size}.png')
        
        # 设计2: 字母 S
        img = design_letter_s(size, colors['indigo'][0], colors['indigo'][1])
        img.save(f'sime_s_{size}x{size}.png')
        
        # 设计3: 抽象 "语"
        img = design_pinyin_yu(size, colors['purple'][0], colors['purple'][1])
        img.save(f'sime_yu_{size}x{size}.png')
        
        # 设计4: 极简几何
        img = design_minimal_geometric(size, colors['teal'][0])
        img.save(f'sime_minimal_{size}x{size}.png')
        
        # 设计5: "语"字
        img = design_yu_character(size, colors['dark'][0], colors['dark'][1])
        img.save(f'sime_yu_char_{size}x{size}.png')
    
    # 生成 SVG 版本
    generate_svg_logo('sime_shi.svg', 'shi')
    generate_svg_logo('sime_s.svg', 's')
    generate_svg_logo('sime_yu.svg', 'yu')
    generate_svg_logo('sime_minimal.svg', 'minimal')
    
    print("Logo 生成完成!")
    print("\n生成的文件:")
    print("  - 设计1 (是字): sime_shi_*.png, sime_shi.svg")
    print("  - 设计2 (字母S): sime_s_*.png, sime_s.svg")
    print("  - 设计3 (抽象语): sime_yu_*.png, sime_yu.svg")
    print("  - 设计4 (极简): sime_minimal_*.png, sime_minimal.svg")
    print("  - 设计5 (语字): sime_yu_char_*.png")
    print("\n推荐用于状态栏的尺寸: 16x16, 22x22, 24x24")
    print("推荐用于应用图标: 32x32, 48x48, 64x64")

if __name__ == '__main__':
    main()
