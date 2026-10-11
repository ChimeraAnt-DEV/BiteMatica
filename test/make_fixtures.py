#!/usr/bin/env python3
"""Generates real schematic fixtures for the BiteMatica host tests.

The C++ tests read these files through the production readers, so the fixtures must be
genuine NBT, not hand-waved byte arrays. Each builder writes the exact wire format the
matching reader expects, which doubles as a second, independent check of the format
spec the readers document.

Usage: python3 test/make_fixtures.py test/fixtures
"""

import gzip
import os
import struct
import sys


# --- low-level NBT encoding ---------------------------------------------------------

def _write_tag(out, tag_type, name, payload):
    out += bytes([tag_type])
    name_bytes = name.encode("utf-8")
    out += struct.pack(">H", len(name_bytes))
    out += name_bytes
    out += payload


def tag_string(name, value):
    body = value.encode("utf-8")
    out = bytearray()
    _write_tag(out, 8, name, struct.pack(">H", len(body)) + body)
    return bytes(out)


def tag_int(name, value):
    out = bytearray()
    _write_tag(out, 3, name, struct.pack(">i", value))
    return bytes(out)


def tag_short(name, value):
    out = bytearray()
    _write_tag(out, 2, name, struct.pack(">h", value))
    return bytes(out)


def tag_byte_array(name, values):
    out = bytearray()
    _write_tag(out, 7, name, struct.pack(">i", len(values)) + bytes(v & 0xFF for v in values))
    return bytes(out)


def tag_int_array(name, values):
    out = bytearray()
    payload = struct.pack(">i", len(values)) + b"".join(struct.pack(">i", v) for v in values)
    _write_tag(out, 11, name, payload)
    return bytes(out)


def tag_long_array(name, values):
    out = bytearray()
    payload = struct.pack(">i", len(values)) + b"".join(struct.pack(">q", v) for v in values)
    _write_tag(out, 12, name, payload)
    return bytes(out)


def tag_int_list(name, values):
    """A LIST of INT tags."""
    out = bytearray()
    payload = bytes([3]) + struct.pack(">i", len(values))
    payload += b"".join(struct.pack(">i", v) for v in values)
    _write_tag(out, 9, name, payload)
    return bytes(out)


def compound_body(*children):
    """The body of a compound: its children followed by TAG_End.

    A compound nested inside another compound is written as type+name+body, but a
    compound used as a LIST element is written as body only (the list already carries
    the element type and names are not stored for list entries).
    """
    return b"".join(children) + b"\x00"


def tag_list_of_compounds(name, bodies):
    """A LIST of COMPOUND, each element given as a compound *body*."""
    out = bytearray()
    payload = bytes([10]) + struct.pack(">i", len(bodies))
    for body in bodies:
        payload += body
    _write_tag(out, 9, name, payload)
    return bytes(out)


def tag_list_of_int_lists(name, lists):
    """A LIST of LIST-of-INT, used by Bedrock's block_indices."""
    out = bytearray()
    payload = bytes([9]) + struct.pack(">i", len(lists))
    for values in lists:
        payload += bytes([3]) + struct.pack(">i", len(values))
        payload += b"".join(struct.pack(">i", v) for v in values)
    _write_tag(out, 9, name, payload)
    return bytes(out)


def compound(name, *children):
    out = bytearray()
    _write_tag(out, 10, name, b"".join(children) + b"\x00")
    return bytes(out)


def pack_long_array(values, bits):
    """Packs values into 64-bit longs, little-endian bit order (Litematica layout)."""
    longs = []
    current = 0
    offset = 0
    in_long = 0
    per_long = 64 // bits
    for value in values:
        current |= (value & ((1 << bits) - 1)) << offset
        offset += bits
        in_long += 1
        if in_long >= per_long:
            longs.append(current)
            current = 0
            offset = 0
            in_long = 0
    if offset > 0:
        longs.append(current)
    return [v if v < (1 << 63) else v - (1 << 64) for v in longs]


# --- format builders ----------------------------------------------------------------

def make_litematic(path):
    """A 3x3x3 stone cube with one oak_planks corner (minimal non-trivial palette)."""
    size = 3
    palette = [
        compound_body(tag_string("Name", "minecraft:air")),
        compound_body(tag_string("Name", "minecraft:stone")),
        compound_body(tag_string("Name", "minecraft:oak_planks")),
    ]
    values = []
    for y in range(size):
        for z in range(size):
            for x in range(size):
                values.append(2 if (x == 0 and y == 2 and z == 0) else 1)

    region = compound(
        "main",
        compound("Position", tag_int("x", 0) + tag_int("y", 0) + tag_int("z", 0)),
        compound("Size", tag_int("x", size) + tag_int("y", size) + tag_int("z", size)),
        tag_list_of_compounds("BlockStatePalette", palette),
        tag_long_array("BlockStates", pack_long_array(values, 2)),
    )
    metadata = compound(
        "Metadata",
        tag_string("Name", "Fixture Litematic"),
        tag_string("Author", "BiteMatica Tests"),
        tag_string("Description", "A 3x3x3 stone cube"),
    )
    root = compound(
        "Schematic",
        tag_int("MinecraftDataVersion", 3120),
        tag_int("Version", 6),
        metadata,
        compound("Regions", region),
    )
    with open(path, "wb") as fp:
        fp.write(gzip.compress(root))


def make_schem(path):
    """A 2x2x2 stone cube with one oak_planks block."""
    sx = sy = sz = 2
    palette = {"minecraft:air": 0, "minecraft:stone": 1, "minecraft:oak_planks": 2}
    palette_tag = compound("Palette", *[tag_int(n, i) for n, i in palette.items()])
    data = [2] + [1] * (sx * sy * sz - 1)
    root = compound(
        "Schematic",
        tag_short("Width", sx) + tag_short("Height", sy) + tag_short("Length", sz),
        tag_int_array("Offset", [0, 0, 0]),
        palette_tag,
        tag_byte_array("BlockData", data),
    )
    with open(path, "wb") as fp:
        fp.write(gzip.compress(root))


def make_legacy_schematic(path):
    """A 2x4x2 column: legacy ids, top layer oak planks (5), rest stone (1)."""
    sx, sy, sz = 2, 4, 2
    blocks = []
    for y in range(sy):
        for z in range(sz):
            for x in range(sx):
                blocks.append(5 if y == sy - 1 else 1)
    root = compound(
        "Schematic",
        tag_short("Width", sx) + tag_short("Height", sy) + tag_short("Length", sz),
        tag_byte_array("Blocks", blocks),
        tag_byte_array("Data", [0] * len(blocks)),
    )
    with open(path, "wb") as fp:
        fp.write(gzip.compress(root))


def make_structure(path):
    """A Bedrock structure: 2x2x2 with one red_wool block in the first corner."""
    palette = [
        compound_body(tag_string("name", "minecraft:stone"), compound("states")),
        compound_body(tag_string("name", "minecraft:red_wool"), compound("states")),
    ]
    indices = [1, 0, 0, 0, 0, 0, 0, 0]
    default = compound("default", tag_list_of_compounds("block_palette", palette))
    root = compound(
        "structure",
        tag_int("format_version", 1),
        tag_int_list("size", [2, 2, 2]),
        tag_list_of_int_lists("block_indices", [indices]),
        compound("palette", default),
    )
    with open(path, "wb") as fp:
        fp.write(root)


def make_multi_region_litematic(path):
    """A litematic with two regions at different positions, to exercise the merge path.

    Region A is a 2x2x2 stone block at (0,0,0); region B is a 1x1x1 gold block at (5,3,-2). A
    reader that only handled the first region, or that mishandled per-region palettes, would pass a
    single-region test and fail this one.
    """
    palette_a = [
        compound_body(tag_string("Name", "minecraft:air")),
        compound_body(tag_string("Name", "minecraft:stone")),
    ]
    values_a = [1] * 8  # 2x2x2 all stone
    region_a = compound(
        "a",
        compound("Position", tag_int("x", 0) + tag_int("y", 0) + tag_int("z", 0)),
        compound("Size", tag_int("x", 2) + tag_int("y", 2) + tag_int("z", 2)),
        tag_list_of_compounds("BlockStatePalette", palette_a),
        tag_long_array("BlockStates", pack_long_array(values_a, 2)),
    )

    # Region B has its own palette, so the same palette index means a different block here.
    palette_b = [
        compound_body(tag_string("Name", "minecraft:air")),
        compound_body(tag_string("Name", "minecraft:gold_block")),
    ]
    values_b = [1]
    region_b = compound(
        "b",
        compound("Position", tag_int("x", 5) + tag_int("y", 3) + tag_int("z", -2)),
        compound("Size", tag_int("x", 1) + tag_int("y", 1) + tag_int("z", 1)),
        tag_list_of_compounds("BlockStatePalette", palette_b),
        tag_long_array("BlockStates", pack_long_array(values_b, 2)),
    )

    metadata = compound(
        "Metadata",
        tag_string("Name", "Multi Region"),
        tag_string("Author", "BiteMatica Tests"),
        tag_string("Description", "Two regions at different offsets"),
    )
    root = compound(
        "Schematic",
        tag_int("MinecraftDataVersion", 3120),
        tag_int("Version", 6),
        metadata,
        compound("Regions", region_a, region_b),
    )
    with open(path, "wb") as fp:
        fp.write(gzip.compress(root))


def make_negative_size_litematic(path):
    """A litematic whose region has a negative size, the common Litematica encoding.

    Litematica stores a region's extent as a signed size and lets the position be the far corner;
    a reader that does not normalize this produces a mirrored or shifted build.
    """
    palette = [
        compound_body(tag_string("Name", "minecraft:air")),
        compound_body(tag_string("Name", "minecraft:stone")),
    ]
    values = [1] * 8
    region = compound(
        "neg",
        compound("Position", tag_int("x", 1) + tag_int("y", 1) + tag_int("z", 1)),
        compound("Size", tag_int("x", -2) + tag_int("y", -2) + tag_int("z", -2)),
        tag_list_of_compounds("BlockStatePalette", palette),
        tag_long_array("BlockStates", pack_long_array(values, 2)),
    )
    metadata = compound("Metadata", tag_string("Name", "Negative Size"))
    root = compound(
        "Schematic",
        tag_int("MinecraftDataVersion", 3120),
        tag_int("Version", 6),
        metadata,
        compound("Regions", region),
    )
    with open(path, "wb") as fp:
        fp.write(gzip.compress(root))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "test/fixtures"
    os.makedirs(out_dir, exist_ok=True)
    make_litematic(os.path.join(out_dir, "cube.litematic"))
    make_schem(os.path.join(out_dir, "cube.schem"))
    make_legacy_schematic(os.path.join(out_dir, "column.schematic"))
    make_structure(os.path.join(out_dir, "corner.nbt"))
    make_multi_region_litematic(os.path.join(out_dir, "multi_region.litematic"))
    make_negative_size_litematic(os.path.join(out_dir, "negative_size.litematic"))
    print("wrote fixtures to", out_dir)


if __name__ == "__main__":
    main()
