#!/usr/bin/env python3
"""Draws the BiteMatica mod icon.

The icon is generated rather than hand-drawn so it stays in one source of truth and can
be regenerated at any size. The motif is a schematic blueprint: an isometric wireframe
cube (a "build" you place) in the GlowberryClient green, on a dark rounded tile, with a
small berry accent for the "Bite" in the name.

Usage: python3 tools/generate_icon.py assets/icon
"""

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

# GlowberryClient's accent green and its darker mint companion.
ACCENT = (124, 240, 192)
ACCENT_DEEP = (58, 200, 150)
BERRY = (255, 92, 120)
BG_TOP = (18, 22, 30)
BG_BOTTOM = (9, 12, 17)


def rounded_mask(size, radius):
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return mask


def vertical_gradient(size, top, bottom):
    image = Image.new("RGB", (1, size))
    for y in range(size):
        t = y / max(1, size - 1)
        image.putpixel((0, y), tuple(
            int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return image.resize((size, size))


def project(x, y, z, size, scale):
    """Isometric projection shared with the mod's own view math."""
    yaw = math.radians(45)
    pitch = math.radians(30)
    rx = x * math.cos(yaw) - z * math.sin(yaw)
    rz = x * math.sin(yaw) + z * math.cos(yaw)
    ry = y * math.cos(pitch) - rz * math.sin(pitch)
    return (size / 2 + rx * scale, size / 2 - ry * scale)


def draw_icon(size):
    base = vertical_gradient(size, BG_TOP, BG_BOTTOM).convert("RGBA")
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(base, (0, 0), rounded_mask(size, int(size * 0.22)))

    draw = ImageDraw.Draw(canvas)
    # A soft accent glow behind the cube.
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glow)
    gdraw.ellipse([size * 0.16, size * 0.16, size * 0.84, size * 0.84],
                  fill=(*ACCENT_DEEP, 90))
    glow = glow.filter(ImageFilter.GaussianBlur(size * 0.09))
    canvas.alpha_composite(glow)

    scale = size * 0.19
    # The cube is centered a little below middle so the berry can sit above it.
    offset_y = size * 0.05

    def point(x, y, z):
        px, py = project(x, y, z, size, scale)
        return (px, py + offset_y)

    corners = {
        "000": point(0, 0, 0), "100": point(1, 0, 0),
        "110": point(1, 1, 0), "010": point(0, 1, 0),
        "001": point(0, 0, 1), "101": point(1, 0, 1),
        "111": point(1, 1, 1), "011": point(0, 1, 1),
    }

    # Faint translucent faces so the cube reads as solid, not just wires.
    faces = [
        (["000", "100", "110", "010"], (42, 55, 66, 150)),   # bottom
        (["001", "101", "111", "011"], (70, 92, 108, 200)),  # top
        (["000", "001", "011", "010"], (34, 46, 58, 210)),   # left
        (["100", "110", "111", "101"], (26, 36, 46, 210)),   # right
    ]
    for names, color in faces:
        draw.polygon([corners[n] for n in names], fill=color)

    line_width = max(2, int(size * 0.012))
    edges = [
        ("000", "100"), ("100", "110"), ("110", "010"), ("010", "000"),
        ("001", "101"), ("101", "111"), ("111", "011"), ("011", "001"),
        ("000", "001"), ("100", "101"), ("010", "011"), ("110", "111"),
    ]
    for a, b in edges:
        draw.line([corners[a], corners[b]], fill=(*ACCENT, 235), width=line_width)

    # Vertex studs, a blueprint touch.
    stud = max(2, int(size * 0.016))
    for px, py in corners.values():
        draw.ellipse([px - stud, py - stud, px + stud, py + stud],
                     fill=(*ACCENT, 255))

    # The berry, top-right, with a leaf.
    berry_center = (size * 0.72, size * 0.27)
    berry_radius = size * 0.085
    draw.ellipse(
        [berry_center[0] - berry_radius, berry_center[1] - berry_radius,
         berry_center[0] + berry_radius, berry_center[1] + berry_radius],
        fill=(*BERRY, 255))
    highlight = berry_radius * 0.35
    draw.ellipse(
        [berry_center[0] - berry_radius * 0.5 - highlight,
         berry_center[1] - berry_radius * 0.5 - highlight,
         berry_center[0] - berry_radius * 0.5 + highlight,
         berry_center[1] - berry_radius * 0.5 + highlight],
        fill=(255, 255, 255, 200))
    leaf = [
        (berry_center[0], berry_center[1] - berry_radius * 0.9),
        (berry_center[0] + berry_radius * 0.9, berry_center[1] - berry_radius * 1.5),
        (berry_center[0] + berry_radius * 0.5, berry_center[1] - berry_radius * 0.6),
    ]
    draw.polygon(leaf, fill=(*ACCENT, 255))

    return canvas


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "assets/icon"
    os.makedirs(out_dir, exist_ok=True)
    master = draw_icon(1024)
    for size in (512, 256, 128):
        resized = master.resize((size, size), Image.LANCZOS)
        path = os.path.join(out_dir, "bitematica-%d.png" % size)
        resized.save(path)
        print("wrote", path)
    master.resize((512, 512), Image.LANCZOS).save(
        os.path.join(out_dir, "bitematica.png"))
    print("wrote", os.path.join(out_dir, "bitematica.png"))


if __name__ == "__main__":
    main()
