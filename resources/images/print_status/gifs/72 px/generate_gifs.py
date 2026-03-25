#!/usr/bin/env python3
"""
generate_status_gifs.py
Generates 12 animated GIF status icons (72x72px) matching the
axolotl / geometric / sci-fi theme of the original SVGs.
Each GIF loops indefinitely.

Run:  python3 generate_status_gifs.py
Output: ./gifs/status_<name>.gif
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

OUT_DIR = os.path.join(os.path.dirname(__file__), "gifs")
os.makedirs(OUT_DIR, exist_ok=True)

SIZE     = 72        # canvas size px
NFRAMES  = 12        # frames per animation
DURATION = 80        # ms per frame  (~12.5 fps)

# ── Palette ────────────────────────────────────────────────────────────────
BG        = (0, 0, 0, 0)          # fully transparent background

C_PURPLE  = (127, 119, 221)
C_TEAL    = (29,  158, 117)
C_AMBER   = (239, 159,  39)
C_BLUE    = (55,  138, 221)
C_GREEN   = (99,  153,  34)
C_RED     = (226,  75,  74)
C_GREY    = (136, 135, 128)
C_LGREY   = (180, 178, 169)
C_CORAL   = (216,  90,  48)

def new_frame():
    return Image.new("RGBA", (SIZE, SIZE), BG)

def save_gif(frames, name, duration=DURATION):
    path = os.path.join(OUT_DIR, f"status_{name}.gif")
    # Convert RGBA → P (palette) for GIF transparency
    pal_frames = []
    for f in frames:
        bg = Image.new("RGBA", f.size, (30, 30, 30, 255))
        bg.paste(f, mask=f)
        pal = bg.convert("RGB").convert("P", palette=Image.ADAPTIVE, colors=255)
        pal_frames.append(pal)
    pal_frames[0].save(
        path,
        save_all=True,
        append_images=pal_frames[1:],
        loop=0,
        duration=duration,
        disposal=2,
    )
    print(f"  wrote {path}")

def hex_points(cx, cy, r, n=6, angle_offset=0):
    pts = []
    for i in range(n):
        a = math.radians(angle_offset + i * 360 / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def draw_hex(draw, cx, cy, r, outline, width=2, fill=None, angle_offset=90):
    pts = hex_points(cx, cy, r, angle_offset=angle_offset)
    draw.polygon(pts, outline=outline, fill=fill)

def draw_axolotl_face(draw, cx, cy, r, color, eye_color, smile=True, t=0):
    """Draw a minimal geometric axolotl face."""
    # Body circle
    draw.ellipse([cx-r, cy-r, cx+r, cy+r], fill=color)
    # Eyes
    ex = int(r * 0.35)
    ey = int(r * 0.15)
    er = max(2, int(r * 0.18))
    draw.ellipse([cx-ex-er, cy-ey-er, cx-ex+er, cy-ey+er], fill=eye_color)
    draw.ellipse([cx+ex-er, cy-ey-er, cx+ex+er, cy-ey+er], fill=eye_color)
    # Smile or flat
    if smile:
        draw.arc([cx-int(r*0.4), cy, cx+int(r*0.4), cy+int(r*0.5)],
                 0, 180, fill=eye_color, width=2)
    else:
        draw.line([cx-int(r*0.3), cy+int(r*0.25),
                   cx+int(r*0.3), cy+int(r*0.25)],
                  fill=eye_color, width=2)

def draw_gills(draw, cx, cy, body_r, color):
    """Draw 2 gill frills on each side."""
    for side in [-1, 1]:
        bx = cx + side * body_r
        for dy, length in [(-8, 8), (-3, 10)]:
            x1 = bx
            y1 = cy + dy
            x2 = bx + side * length
            y2 = y1 - length // 2
            draw.line([(x1, y1), (x2, y2)], fill=color, width=2)


# ── 1. IDLE ─────────────────────────────────────────────────────────────────
def make_idle():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Slow-pulsing hexagon
        alpha = int(128 + 127 * math.sin(i * math.pi * 2 / NFRAMES))
        col = (*C_PURPLE[:3], alpha)
        draw_hex(d, SIZE//2, SIZE//2, 28, outline=col, width=2)
        draw_hex(d, SIZE//2, SIZE//2, 20, outline=col, width=1)
        # Axolotl face
        draw_gills(d, SIZE//2, SIZE//2, 12, C_PURPLE)
        draw_axolotl_face(d, SIZE//2, SIZE//2, 10, C_PURPLE, (60, 50, 140), smile=True)
        # Idle dot pulse
        dot_alpha = int(80 + 175 * abs(math.sin(i * math.pi / NFRAMES)))
        d.ellipse([32, 58, 40, 66], fill=(*C_PURPLE[:3], dot_alpha))
        frames.append(f)
    save_gif(frames, "idle", duration=160)

# ── 2. SLICING ───────────────────────────────────────────────────────────────
def make_slicing():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Rotating dashed hex
        angle = i * 360 / NFRAMES
        pts = hex_points(SIZE//2, SIZE//2, 30, angle_offset=90+angle)
        for j in range(6):
            if j % 2 == 0:
                d.line([pts[j], pts[(j+1)%6]], fill=C_TEAL, width=2)
        # Layer stack
        for ly, opacity in [(46, 230), (40, 180), (34, 130), (28, 80)]:
            d.rectangle([24, ly, 48, ly+4], fill=(*C_TEAL[:3], opacity))
        # Scan laser line - moves down
        scan_y = 24 + int(22 * (i / NFRAMES))
        d.line([(18, scan_y), (54, scan_y)], fill=(*C_TEAL[:3], 200), width=2)
        # Axolotl face on top
        draw_gills(d, SIZE//2, 20, 6, C_TEAL)
        draw_axolotl_face(d, SIZE//2, 20, 5, C_TEAL, (8, 80, 60), smile=True)
        frames.append(f)
    save_gif(frames, "slicing", duration=80)

# ── 3. SLICED ────────────────────────────────────────────────────────────────
def make_sliced():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Static filled green hex
        pts = hex_points(SIZE//2, SIZE//2, 30, angle_offset=90)
        d.polygon(pts, fill=(*C_GREEN[:3], 60), outline=C_GREEN)
        # Checkmark
        d.line([(24, 36), (32, 44), (48, 28)], fill=C_GREEN, width=3)
        # Axolotl face
        draw_gills(d, SIZE//2, SIZE//2, 12, C_GREEN)
        draw_axolotl_face(d, SIZE//2, SIZE//2, 10,
                          (140, 200, 90), (50, 100, 20), smile=True)
        frames.append(f)
    save_gif(frames, "sliced", duration=200)

# ── 4. SENDING ───────────────────────────────────────────────────────────────
def make_sending():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Diamond frame
        pts = [(SIZE//2, 12), (56, SIZE//2), (SIZE//2, 60), (16, SIZE//2)]
        d.polygon(pts, outline=C_BLUE, fill=None)
        # Bouncing upload arrow
        offset = int(4 * math.sin(i * math.pi * 2 / NFRAMES))
        ax, ay = SIZE//2, 46 + offset
        d.line([(ax, ay), (ax, ay-18)], fill=C_BLUE, width=2)
        d.line([(ax-6, ay-12), (ax, ay-18), (ax+6, ay-12)], fill=C_BLUE, width=2)
        # Three progress dots
        for dot_i, dot_x in enumerate([28, SIZE//2, 44]):
            phase = (i - dot_i * 2) % NFRAMES
            alpha = int(80 + 175 * abs(math.sin(phase * math.pi / NFRAMES)))
            d.ellipse([dot_x-3, 53, dot_x+3, 59],
                      fill=(*C_BLUE[:3], alpha))
        # Eyes on diamond
        d.ellipse([29, 32, 35, 38], fill=C_BLUE)
        d.ellipse([37, 32, 43, 38], fill=C_BLUE)
        frames.append(f)
    save_gif(frames, "sending", duration=80)

# ── 5. PREPARE ───────────────────────────────────────────────────────────────
def make_prepare():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Dashed outer ring (rotates)
        angle0 = i * 30
        for seg in range(12):
            a1 = math.radians(angle0 + seg * 30)
            a2 = math.radians(angle0 + seg * 30 + 15)
            x1 = SIZE//2 + 28 * math.cos(a1)
            y1 = SIZE//2 + 28 * math.sin(a1)
            x2 = SIZE//2 + 28 * math.cos(a2)
            y2 = SIZE//2 + 28 * math.sin(a2)
            d.line([(x1, y1), (x2, y2)], fill=C_AMBER, width=2)
        # Triangle
        pts = hex_points(SIZE//2, SIZE//2, 22, n=3, angle_offset=-90)
        d.polygon(pts, outline=C_AMBER, fill=(*C_AMBER[:3], 30))
        # Thermometer mercury pulse
        h = int(8 + 6 * abs(math.sin(i * math.pi * 2 / NFRAMES)))
        d.rectangle([33, 42-h, 39, 42], fill=C_AMBER)
        d.ellipse([31, 42, 41, 52], fill=C_AMBER)
        # Gill frills
        draw_gills(d, SIZE//2, 42, 12, C_AMBER)
        # Eyes
        d.ellipse([29, 44, 33, 48], fill=(130, 80, 10))
        d.ellipse([39, 44, 43, 48], fill=(130, 80, 10))
        frames.append(f)
    save_gif(frames, "prepare", duration=80)

# ── 6. RUNNING ───────────────────────────────────────────────────────────────
def make_running():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        draw_hex(d, SIZE//2, SIZE//2, 30, outline=C_TEAL, width=2)
        # Layer stack — one new layer rises per cycle
        layers = 3
        for li in range(layers):
            opacity = int(255 * (li + 1) / layers)
            y = 46 - li * 6
            d.rectangle([23, y, 49, y+4], fill=(*C_TEAL[:3], opacity))
        # Nozzle bobs up and down
        bob = int(3 * math.sin(i * math.pi * 2 / NFRAMES))
        nz_y = 22 + bob
        pts = [(31, nz_y), (41, nz_y), (39, nz_y+7), (33, nz_y+7)]
        d.polygon(pts, fill=C_TEAL)
        # Extrusion dot
        dot_alpha = int(128 + 127 * abs(math.sin(i * math.pi * 2 / NFRAMES)))
        d.ellipse([34, 29+bob, 38, 33+bob], fill=(*C_TEAL[:3], dot_alpha))
        # Nozzle eyes
        d.ellipse([33, nz_y+1, 36, nz_y+4], fill=(10, 80, 60))
        d.ellipse([37, nz_y+1, 40, nz_y+4], fill=(10, 80, 60))
        # Gills
        draw_gills(d, SIZE//2, SIZE//2, 22, C_TEAL)
        frames.append(f)
    save_gif(frames, "running", duration=80)

# ── 7. PAUSE ─────────────────────────────────────────────────────────────────
def make_pause():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        pts = hex_points(SIZE//2, SIZE//2, 30, angle_offset=90)
        alpha_fill = int(30 + 20 * math.sin(i * math.pi * 2 / NFRAMES))
        d.polygon(pts, fill=(*C_AMBER[:3], alpha_fill), outline=C_AMBER)
        # Pause bars
        d.rectangle([25, 25, 32, 47], fill=C_AMBER)
        d.rectangle([40, 25, 47, 47], fill=C_AMBER)
        # Pulsing ring
        ring_alpha = int(80 + 120 * abs(math.sin(i * math.pi / NFRAMES)))
        draw_hex(d, SIZE//2, SIZE//2, 18,
                 outline=(*C_AMBER[:3], ring_alpha), width=1)
        # Worried axolotl face
        draw_gills(d, SIZE//2, 22, 8, C_AMBER)
        draw_axolotl_face(d, SIZE//2, 22, 6,
                          C_AMBER, (130, 80, 10), smile=False)
        frames.append(f)
    save_gif(frames, "pause", duration=100)

# ── 8. FILAMENT_CHANGE ───────────────────────────────────────────────────────
def make_filament_change():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Spinning spool
        angle = i * 360 / NFRAMES
        d.ellipse([12, 12, 60, 60], outline=C_PURPLE, width=2)
        d.ellipse([26, 26, 46, 46], outline=C_PURPLE, width=1)
        # Dashed filament strand (rotates)
        for seg in range(6):
            a1 = math.radians(angle + seg * 60)
            a2 = math.radians(angle + seg * 60 + 30)
            x1 = SIZE//2 + 22 * math.cos(a1)
            y1 = SIZE//2 + 22 * math.sin(a1)
            x2 = SIZE//2 + 22 * math.cos(a2)
            y2 = SIZE//2 + 22 * math.sin(a2)
            d.line([(x1, y1), (x2, y2)], fill=C_PURPLE, width=2)
        # Central axolotl face
        draw_axolotl_face(d, SIZE//2, SIZE//2, 7,
                          (200, 190, 255), (60, 50, 140), smile=True)
        frames.append(f)
    save_gif(frames, "filament_change", duration=80)

# ── 9. CALIBRATING ───────────────────────────────────────────────────────────
def make_calibrating():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Outer dashed ring
        for seg in range(16):
            a = math.radians(seg * 22.5)
            if seg % 2 == 0:
                x1 = SIZE//2 + 28 * math.cos(a)
                y1 = SIZE//2 + 28 * math.sin(a)
                a2 = math.radians((seg+1) * 22.5)
                x2 = SIZE//2 + 28 * math.cos(a2)
                y2 = SIZE//2 + 28 * math.sin(a2)
                d.line([(x1,y1),(x2,y2)], fill=C_GREY, width=1)
        # Rotating cross
        angle = math.radians(i * 360 / NFRAMES)
        for arm in range(4):
            a = angle + arm * math.pi / 2
            x2 = SIZE//2 + 22 * math.cos(a)
            y2 = SIZE//2 + 22 * math.sin(a)
            d.line([(SIZE//2, SIZE//2), (x2, y2)], fill=C_GREY, width=2)
        # CCW orbiting dots
        angle_ccw = math.radians(-i * 360 / NFRAMES)
        for dot in range(4):
            a = angle_ccw + dot * math.pi / 2
            x = SIZE//2 + 28 * math.cos(a)
            y = SIZE//2 + 28 * math.sin(a)
            d.ellipse([x-3, y-3, x+3, y+3], fill=C_LGREY)
        # Target centre with axolotl eyes
        d.ellipse([SIZE//2-8, SIZE//2-8, SIZE//2+8, SIZE//2+8],
                  fill=(220, 218, 208), outline=C_GREY)
        d.ellipse([SIZE//2-4, SIZE//2-4, SIZE//2+4, SIZE//2+4],
                  fill=C_GREY)
        d.ellipse([32, 34, 36, 38], fill=(40, 40, 40))
        d.ellipse([37, 34, 41, 38], fill=(40, 40, 40))
        frames.append(f)
    save_gif(frames, "calibrating", duration=80)

# ── 10. FINISH ───────────────────────────────────────────────────────────────
def make_finish():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Expanding ring pulse
        ring_r = int(10 + 20 * (i / NFRAMES))
        ring_alpha = int(200 * (1 - i / NFRAMES))
        d.ellipse([SIZE//2-ring_r, SIZE//2-ring_r,
                   SIZE//2+ring_r, SIZE//2+ring_r],
                  outline=(*C_TEAL[:3], ring_alpha), width=2)
        # Starburst (pulsing scale)
        scale = 0.85 + 0.15 * abs(math.sin(i * math.pi * 2 / NFRAMES))
        star_pts = []
        for j in range(8):
            r_out = int(26 * scale) if j % 2 == 0 else int(14 * scale)
            a = math.radians(j * 45 - 90)
            star_pts.append((SIZE//2 + r_out * math.cos(a),
                             SIZE//2 + r_out * math.sin(a)))
        d.polygon(star_pts, fill=(*C_TEAL[:3], 120), outline=C_TEAL)
        # Layered part on bed
        for li, (w, y) in enumerate([(28, 44), (20, 38), (14, 32)]):
            x0 = SIZE//2 - w//2
            d.rectangle([x0, y, x0+w, y+5], fill=C_TEAL)
        # Happy axolotl arms up
        draw_axolotl_face(d, SIZE//2, 24, 7, (150, 230, 200), (10, 80, 60), smile=True)
        d.line([(SIZE//2-7, 24), (SIZE//2-14, 16)], fill=C_TEAL, width=2)
        d.line([(SIZE//2+7, 24), (SIZE//2+14, 16)], fill=C_TEAL, width=2)
        frames.append(f)
    save_gif(frames, "finish", duration=100)

# ── 11. FAILED ───────────────────────────────────────────────────────────────
def make_failed():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Warning triangle
        pts = [(SIZE//2, 10), (58, 48), (14, 48)]
        d.polygon(pts, fill=(*C_RED[:3], 40), outline=C_RED)
        # X mark
        d.line([(26, 26), (46, 46)], fill=C_RED, width=3)
        d.line([(46, 26), (26, 46)], fill=C_RED, width=3)
        # Sad axolotl with pulsing tears
        draw_axolotl_face(d, SIZE//2, 54, 6,
                          (252, 220, 220), (120, 30, 30), smile=False)
        tear_alpha = int(100 + 155 * abs(math.sin(i * math.pi / NFRAMES)))
        d.ellipse([30, 60, 33, 65], fill=(*C_RED[:3], tear_alpha))
        d.ellipse([39, 60, 42, 65], fill=(*C_RED[:3], tear_alpha))
        frames.append(f)
    save_gif(frames, "failed", duration=120)

# ── 12. OFFLINE ──────────────────────────────────────────────────────────────
def make_offline():
    frames = []
    for i in range(NFRAMES):
        f = new_frame(); d = ImageDraw.Draw(f)
        # Dashed hexagon
        pts = hex_points(SIZE//2, SIZE//2, 30, angle_offset=90)
        for j in range(6):
            if j % 2 == 0:
                d.line([pts[j], pts[(j+1)%6]], fill=C_LGREY, width=2)
        # Faded signal arcs
        d.arc([22, 36, 50, 52], 210, 330, fill=C_LGREY, width=2)
        d.arc([26, 38, 46, 50], 210, 330, fill=C_GREY, width=2)
        # X over signal
        d.line([(26, 24), (46, 44)], fill=C_GREY, width=2)
        d.line([(46, 24), (26, 44)], fill=C_GREY, width=2)
        # Sleeping axolotl + z z z
        draw_axolotl_face(d, SIZE//2, 56, 5,
                          (230, 228, 218), (80, 78, 70), smile=False)
        # Closed eyes (lines instead of circles)
        d.line([(SIZE//2-4, 55), (SIZE//2-1, 54)], fill=(80,78,70), width=1)
        d.line([(SIZE//2+1, 54), (SIZE//2+4, 55)], fill=(80,78,70), width=1)
        # z z z fade in/out
        phase = i / NFRAMES
        for zi, (zx, zy, zs) in enumerate([(44,48,8),(48,42,10),(53,35,12)]):
            alpha = int(80 + 120 * abs(math.sin(
                (phase + zi * 0.25) * math.pi)))
            # Draw 'z' as lines
            d.line([(zx, zy), (zx+zs, zy)], fill=(*C_LGREY[:3], alpha), width=1)
            d.line([(zx+zs, zy), (zx, zy+zs)], fill=(*C_LGREY[:3], alpha), width=1)
            d.line([(zx, zy+zs), (zx+zs, zy+zs)], fill=(*C_LGREY[:3], alpha), width=1)
        frames.append(f)
    save_gif(frames, "offline", duration=160)


if __name__ == "__main__":
    print("Generating animated GIFs...")
    make_idle()
    make_slicing()
    make_sliced()
    make_sending()
    make_prepare()
    make_running()
    make_pause()
    make_filament_change()
    make_calibrating()
    make_finish()
    make_failed()
    make_offline()
    print(f"Done. All GIFs written to {OUT_DIR}/")
