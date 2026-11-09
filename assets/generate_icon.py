#!/usr/bin/env python3
"""Generate the TailZero application icon.

The artwork is a deterministic, dependency-light drawing: a dark-green square
canvas with a Tailscale-style mesh (one hub node, five peer nodes, direct and
relayed links) plus a comet-like tail emerging from the hub. It is drawn at 4x
and downsampled with LANCZOS, so the same script reproduces the exact PNGs:

    assets/share/images/tailzero.png      256x256  store icon + APPLaunch icon
    assets/share/images/tailzero_100.png  100x100  launcher/small sizes
    assets/share/images/tailzero_80.png    80x80   launcher/small sizes

Usage:
    python generate_icon.py [--out-dir assets/share/images]

Requires Pillow. Generated files are committed to the repository; rerun only
when the artwork itself changes.
"""
import argparse
import math
import os

from PIL import Image, ImageDraw, ImageFilter

# Palette (matches src/ui/theme.h: color:: names).
BG_TOP = (11, 17, 16)        # 0x0B1110
BG_BOTTOM = (16, 29, 24)     # 0x101D18
PANEL = (19, 28, 25)         # 0x131C19
LINE = (38, 58, 51)          # 0x263A33
ACCENT = (61, 220, 132)      # 0x3DDC84
ACCENT_DIM = (31, 116, 68)   # 0x1F7444
RELAY = (99, 179, 237)       # 0x63B3ED
TEXT = (220, 232, 226)       # 0xDCE8E2

SIZE = 256
SS = 4                        # supersampling factor
S = SIZE * SS


def sc(v):
    """Scale a coordinate expressed in final 256px space."""
    return int(round(v * SS))


def vertical_gradient(size, top, bottom):
    img = Image.new("RGB", (size, size), top)
    px = img.load()
    for y in range(size):
        t = y / (size - 1)
        r = int(top[0] + (bottom[0] - top[0]) * t)
        g = int(top[1] + (bottom[1] - top[1]) * t)
        b = int(top[2] + (bottom[2] - top[2]) * t)
        for x in range(size):
            px[x, y] = (r, g, b)
    return img


def glow_layer(size, color, alpha):
    return Image.new("RGBA", (size, size), color + (alpha,))


def bezier(p0, p1, p2, steps):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]
        pts.append((x, y))
    return pts


def draw_tapered_curve(draw, points, widths, colors):
    for (x, y), w, color in zip(points, widths, colors):
        r = max(1, w / 2.0)
        draw.ellipse((x - r, y - r, x + r, y + r), fill=color)


def draw_mesh(draw):
    center = (112, 106)
    hub_r = 36
    peers = [
        (50, 54, 15),      # top-left
        (188, 48, 16),     # top-right
        (238, 120, 14),    # right, relayed (DERP)
        (44, 168, 15),     # bottom-left
    ]
    relay_index = 2

    # Outer mesh ring: faint links between neighbouring peers.
    for a, b in ((0, 1), (0, 3), (1, 2)):
        pa = (sc(peers[a][0]), sc(peers[a][1]))
        pb = (sc(peers[b][0]), sc(peers[b][1]))
        draw.line([pa, pb], fill=LINE + (255,), width=sc(2.2))

    # Hub-to-peer links: direct links solid accent, relayed link dotted in blue.
    for i, (px, py, pr) in enumerate(peers):
        if i == relay_index:
            continue
        p = (sc(px), sc(py))
        c = (sc(center[0]), sc(center[1]))
        draw.line([c, p], fill=(31, 90, 62, 255), width=sc(7.5))
        draw.line([c, p], fill=ACCENT_DIM + (255,), width=sc(4.5))

    # Relayed (DERP) link: dotted blue line.
    rx, ry, _ = peers[relay_index]
    start = (center[0] + hub_r - 4, center[1] + 2)
    end = (rx - 12, ry)
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy)
    steps = int(length / 7)
    for i in range(steps):
        t0 = i / steps
        t1 = t0 + 0.45 / steps
        a = (sc(start[0] + dx * t0), sc(start[1] + dy * t0))
        b = (sc(start[0] + dx * t1), sc(start[1] + dy * t1))
        draw.line([a, b], fill=RELAY + (235,), width=sc(4.0))

    # Peer nodes.
    for i, (px, py, pr) in enumerate(peers):
        ring = RELAY if i == relay_index else ACCENT
        cx, cy, r = sc(px), sc(py), sc(pr)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=PANEL + (255,),
                     outline=ring + (255,), width=sc(4.5))
        dot = sc(3.2)
        draw.ellipse((cx - dot, cy - dot, cx + dot, cy + dot), fill=TEXT + (255,))

    # Hub: dark disc with an accent ring and a three-node mini mesh inside.
    cx, cy, r = sc(center[0]), sc(center[1]), sc(hub_r)
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=PANEL + (255,),
                 outline=ACCENT + (255,), width=sc(6.5))
    inner_r = r - sc(9)
    draw.ellipse((cx - inner_r, cy - inner_r, cx + inner_r, cy + inner_r),
                 outline=(31, 77, 55, 255), width=sc(1.6))
    glyph = [
        (center[0] - 11, center[1] - 8),
        (center[0] + 13, center[1] - 5),
        (center[0] - 1, center[1] + 13),
    ]
    g = [(sc(x), sc(y)) for x, y in glyph]
    for a, b in ((0, 1), (1, 2), (2, 0)):
        draw.line([g[a], g[b]], fill=TEXT + (255,), width=sc(2.6))
    for x, y in g:
        rr = sc(4.2)
        draw.ellipse((x - rr, y - rr, x + rr, y + rr), fill=ACCENT + (255,))

    # Comet tail sweeping out of the hub towards the free bottom-right corner.
    spine = bezier((center[0] + 16, center[1] + 22),
                   (196, 186), (248, 250), 52)
    widths = []
    colors = []
    for i in range(len(spine)):
        t = i / (len(spine) - 1)
        widths.append(sc(13 * (1 - t) ** 1.5 + 1.0))
        c = (int(ACCENT[0] + (ACCENT_DIM[0] - ACCENT[0]) * t),
             int(ACCENT[1] + (ACCENT_DIM[1] - ACCENT[1]) * t),
             int(ACCENT[2] + (ACCENT_DIM[2] - ACCENT[2]) * t))
        colors.append(c + (int(235 * (1 - t) ** 0.6),))
    draw_tapered_curve(draw, [(sc(x), sc(y)) for x, y in spine], widths, colors)


def build() -> Image.Image:
    base = vertical_gradient(S, BG_TOP, BG_BOTTOM).convert("RGBA")

    # Soft accent glow behind the hub.
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gx, gy, gr = sc(116), sc(108), sc(120)
    gd.ellipse((gx - gr, gy - gr, gx + gr, gy + gr), fill=ACCENT + (26,))
    glow = glow.filter(ImageFilter.GaussianBlur(sc(18)))
    base = Image.alpha_composite(base, glow)

    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    draw_mesh(draw)
    layer = layer.filter(ImageFilter.GaussianBlur(sc(0.35)))
    return Image.alpha_composite(base, layer)


def write(img: Image.Image, path: str, size: int) -> None:
    out = img.resize((size, size), Image.LANCZOS)
    out.save(path, format="PNG", optimize=True)
    print(f"{path} ({size}x{size})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", default=os.path.join(os.path.dirname(__file__),
                                                      "share", "images"))
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    art = build()
    write(art, os.path.join(args.out_dir, "tailzero.png"), 256)
    write(art, os.path.join(args.out_dir, "tailzero_100.png"), 100)
    write(art, os.path.join(args.out_dir, "tailzero_80.png"), 80)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
