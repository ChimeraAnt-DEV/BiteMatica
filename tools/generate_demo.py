#!/usr/bin/env python3
"""Generates the demo schematic shipped inside the .AntEgg package.

A fresh install should show something the moment it is enabled, so the package carries
one small build. It reuses the NBT encoders from the test fixture generator so there is
a single definition of the wire format.

Usage: python3 tools/generate_demo.py assets/schematics/demo_house.litematic
"""

import gzip
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "test"))

from make_fixtures import (  # noqa: E402
    compound,
    compound_body,
    pack_long_array,
    tag_int,
    tag_list_of_compounds,
    tag_long_array,
    tag_string,
)


def build_schematic():
    # A 7x5x7 hollow hut: cobble floor, log corners, plank roof, a window and a door.
    sx, sy, sz = 7, 5, 7

    palette = [
        compound_body(tag_string("Name", "minecraft:air")),
        compound_body(tag_string("Name", "minecraft:oak_planks")),
        compound_body(tag_string("Name", "minecraft:oak_log")),
        compound_body(tag_string("Name", "minecraft:glass")),
        compound_body(tag_string("Name", "minecraft:cobblestone")),
    ]
    AIR, PLANKS, LOG, GLASS, COBBLE = 0, 1, 2, 3, 4

    grid = [[[AIR for _ in range(sz)] for _ in range(sy)] for _ in range(sx)]
    for x in range(sx):
        for z in range(sz):
            grid[x][0][z] = COBBLE  # floor
            grid[x][sy - 1][z] = PLANKS  # ceiling
    for y in range(1, sy - 1):
        for x in range(sx):
            grid[x][y][0] = LOG
            grid[x][y][sz - 1] = LOG
        for z in range(sz):
            grid[0][y][z] = LOG
            grid[sx - 1][y][z] = LOG
    grid[3][3][0] = GLASS  # window
    grid[3][1][0] = AIR    # door gap
    grid[3][2][0] = AIR

    # Litematica reads BlockStates X-major: index = (y * sizeZ + z) * sizeX + x.
    values = []
    for y in range(sy):
        for z in range(sz):
            for x in range(sx):
                values.append(grid[x][y][z])

    pos = compound("Position", tag_int("x", 0) + tag_int("y", 0) + tag_int("z", 0))
    dims = compound("Size", tag_int("x", sx) + tag_int("y", sy) + tag_int("z", sz))
    region = compound(
        "main",
        pos,
        dims,
        tag_list_of_compounds("BlockStatePalette", palette),
        tag_long_array("BlockStates", pack_long_array(values, 3)),
    )
    metadata = compound(
        "Metadata",
        tag_string("Name", "BiteMatica Demo Hut"),
        tag_string("Author", "BiteMatica"),
        tag_string(
            "Description",
            "A 7x5x7 plank hut with a cobble floor, log corners, a window and a door.",
        ),
    )
    root = compound(
        "Schematic",
        tag_int("MinecraftDataVersion", 3120),
        tag_int("Version", 6),
        metadata,
        compound("Regions", region),
    )
    return gzip.compress(root)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/schematics/demo_house.litematic"
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as fp:
        fp.write(build_schematic())
    print("wrote", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
