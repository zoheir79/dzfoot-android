#!/usr/bin/env python3
"""Generate a simple ARCore Augmented Image marker."""

from PIL import Image, ImageDraw

def create_marker(size=512):
    img = Image.new('RGBA', (size, size), (255, 255, 255, 255))
    draw = ImageDraw.Draw(img)
    
    # Draw border
    border_w = size // 16
    draw.rectangle([border_w, border_w, size-border_w, size-border_w], 
                   outline=(0, 0, 0, 255), width=border_w)
    
    # Draw cross
    cross_w = size // 8
    cx, cy = size // 2, size // 2
    # Horizontal bar
    draw.rectangle([border_w*2, cy-cross_w//2, size-border_w*2, cy+cross_w//2], 
                   fill=(0, 0, 0, 255))
    # Vertical bar
    draw.rectangle([cx-cross_w//2, border_w*2, cx+cross_w//2, size-border_w*2], 
                   fill=(0, 0, 0, 255))
    
    # Draw corner markers (helps ARCore detect)
    corner_size = size // 6
    for corner in [(border_w*3, border_w*3), 
                   (size-border_w*3-corner_size, border_w*3),
                   (border_w*3, size-border_w*3-corner_size),
                   (size-border_w*3-corner_size, size-border_w*3-corner_size)]:
        draw.rectangle([corner[0], corner[1], 
                       corner[0]+corner_size, corner[1]+corner_size],
                      fill=(0, 0, 0, 255))
    
    # Add DZFoot text in center
    try:
        from PIL import ImageFont
        font_size = size // 10
        font = ImageFont.truetype("arial.ttf", font_size)
        text = "DZFOOT"
        bbox = draw.textbbox((0, 0), text, font=font)
        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]
        draw.text((cx - text_w//2, cy + cross_w), text, fill=(0, 0, 0, 255), font=font)
    except:
        pass  # No font available, skip text
    
    return img

if __name__ == "__main__":
    marker = create_marker(512)
    marker.save("marker_dzfoot.png")
    print("Created marker_dzfoot.png")
    print("")
    print("IMPORTANT:")
    print("1. Print this image on paper (about 10-15cm)")
    print("2. On a REAL phone with ARCore, point camera at it")
    print("3. The app will detect it and show the stadium AR")
    print("")
    print("NOTE: Emulator CANNOT do real AR. Use fallback mode on emulator.")
