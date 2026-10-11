#!/usr/bin/env python3
"""Generate `data/block_colors.json`: Bedrock block id -> average face colour.

Two modes:

1. **From a vanilla resource pack** (the accurate route):

       python3 tools/gen_block_colors.py --textures /path/to/resource_pack/textures/blocks \\
           --out data/block_colors.json

   Reads every `*.png` in the directory (and one level of subdirectories), averages the
   non-transparent pixels, and maps the texture stem to `minecraft:<stem>`. The mapping
   from a Bedrock block id to its texture stem is not 1:1 (`minecraft:grass_block` uses
   `grass_top`, `minecraft:oak_log` uses `log_oak`), so a small alias table below bridges
   the common cases and everything else is emitted under its texture stem.

   Only stdlib is used: PNG decoding is done with `zlib` + the PNG chunk format, so no
   Pillow dependency is added.

2. **Without textures** (the default): emit the hand-maintained table below. This is the
   shipped `data/block_colors.json`. It covers the ~150 blocks that dominate real builds.

The output is a flat JSON object `{ "minecraft:stone": [r, g, b], ... }`, which is exactly
what `BlockColors::loadJson` reads. Unknown blocks are *not* invented here: the renderer
falls back to a deterministic hash colour and reports the count, so a missing entry is
visible rather than silently wrong.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import zlib

# Hand-maintained colours for the ~150 most common blocks. These are close to the average
# colour of the block's main face texture. When a vanilla resource pack is supplied, mode 1
# replaces these with measured values.
BUILTIN: dict[str, tuple[int, int, int]] = {
    "air": (0, 0, 0),
    "stone": (125, 125, 125),
    "granite": (153, 106, 89),
    "diorite": (200, 200, 200),
    "andesite": (136, 136, 136),
    "deepslate": (77, 77, 80),
    "cobbled_deepslate": (77, 77, 80),
    "tuff": (108, 109, 102),
    "calcite": (223, 224, 220),
    "dripstone_block": (134, 107, 92),
    "cobblestone": (110, 110, 110),
    "mossy_cobblestone": (95, 120, 90),
    "stone_bricks": (122, 122, 122),
    "mossy_stone_bricks": (105, 120, 95),
    "cracked_stone_bricks": (118, 118, 118),
    "chiseled_stone_bricks": (122, 122, 122),
    "bricks": (150, 97, 83),
    "dirt": (134, 96, 67),
    "coarse_dirt": (134, 96, 67),
    "rooted_dirt": (144, 104, 76),
    "podzol": (91, 65, 30),
    "mud": (60, 57, 61),
    "clay": (160, 166, 179),
    "grass_block": (100, 160, 70),
    "mycelium": (111, 99, 105),
    "farmland": (111, 76, 48),
    "dirt_path": (148, 122, 65),
    "sand": (219, 207, 163),
    "red_sand": (190, 100, 40),
    "sandstone": (216, 203, 155),
    "red_sandstone": (181, 97, 31),
    "smooth_sandstone": (224, 214, 170),
    "gravel": (136, 126, 126),
    "bedrock": (85, 85, 85),
    "obsidian": (20, 18, 29),
    "crying_obsidian": (32, 20, 58),
    "netherrack": (120, 50, 50),
    "soul_sand": (82, 62, 50),
    "soul_soil": (75, 57, 46),
    "magma": (142, 63, 31),
    "nether_bricks": (44, 22, 26),
    "red_nether_bricks": (70, 9, 9),
    "basalt": (72, 71, 75),
    "blackstone": (42, 35, 40),
    "end_stone": (219, 222, 158),
    "end_stone_bricks": (219, 222, 158),
    "purpur_block": (169, 125, 169),
    "quartz_block": (235, 230, 224),
    "smooth_quartz": (235, 230, 224),
    "prismarine": (99, 171, 158),
    "dark_prismarine": (51, 91, 75),
    "prismarine_bricks": (99, 171, 158),
    "sea_lantern": (190, 220, 210),
    "glowstone": (220, 190, 110),
    "shroomlight": (240, 146, 70),
    "ochre_froglight": (223, 213, 158),
    "verdant_froglight": (200, 224, 190),
    "pearlescent_froglight": (224, 190, 214),
    "amethyst_block": (133, 97, 191),
    "budding_amethyst": (133, 97, 191),
    "copper_block": (192, 107, 79),
    "exposed_copper": (161, 125, 103),
    "weathered_copper": (108, 153, 110),
    "oxidized_copper": (82, 162, 132),
    "iron_block": (220, 220, 220),
    "gold_block": (246, 208, 61),
    "diamond_block": (97, 219, 213),
    "emerald_block": (42, 203, 86),
    "redstone_block": (175, 24, 5),
    "lapis_block": (30, 67, 140),
    "coal_block": (16, 16, 16),
    "netherite_block": (67, 61, 63),
    "oak_log": (150, 120, 70),
    "spruce_log": (86, 65, 38),
    "birch_log": (216, 215, 210),
    "jungle_log": (149, 108, 76),
    "acacia_log": (103, 96, 86),
    "dark_oak_log": (60, 46, 26),
    "mangrove_log": (103, 70, 54),
    "cherry_log": (53, 34, 43),
    "bamboo_block": (139, 149, 62),
    "crimson_stem": (93, 25, 29),
    "warped_stem": (58, 98, 96),
    "oak_planks": (162, 130, 78),
    "spruce_planks": (114, 84, 48),
    "birch_planks": (200, 180, 130),
    "jungle_planks": (160, 115, 80),
    "acacia_planks": (168, 90, 50),
    "dark_oak_planks": (66, 43, 20),
    "mangrove_planks": (117, 54, 48),
    "cherry_planks": (226, 179, 170),
    "bamboo_planks": (194, 171, 90),
    "crimson_planks": (101, 48, 70),
    "warped_planks": (43, 105, 99),
    "oak_leaves": (60, 130, 50),
    "spruce_leaves": (40, 80, 55),
    "birch_leaves": (80, 130, 60),
    "jungle_leaves": (45, 110, 40),
    "acacia_leaves": (70, 120, 40),
    "dark_oak_leaves": (40, 90, 40),
    "mangrove_leaves": (60, 120, 45),
    "cherry_leaves": (226, 168, 200),
    "azalea_leaves": (70, 110, 45),
    "glass": (180, 220, 235),
    "glass_pane": (180, 220, 235),
    "tinted_glass": (60, 60, 65),
    "water": (50, 90, 200),
    "lava": (220, 100, 20),
    "ice": (150, 190, 240),
    "packed_ice": (140, 180, 235),
    "blue_ice": (116, 167, 253),
    "snow_block": (240, 240, 245),
    "snow": (240, 240, 245),
    "powder_snow": (248, 250, 252),
    "hay_block": (170, 140, 30),
    "dried_kelp_block": (50, 60, 40),
    "moss_block": (90, 130, 60),
    "sculk": (16, 24, 30),
    "sculk_catalyst": (20, 30, 40),
    "honey_block": (240, 170, 40),
    "slime_block": (110, 190, 90),
    "sponge": (195, 195, 80),
    "wet_sponge": (170, 190, 90),
    "tnt": (200, 60, 50),
    "crafting_table": (140, 100, 60),
    "furnace": (110, 110, 110),
    "bookshelf": (160, 130, 90),
    "chest": (150, 110, 60),
    "trapped_chest": (150, 110, 60),
    "ender_chest": (40, 60, 60),
    "barrel": (140, 105, 60),
    "beacon": (120, 220, 220),
    "command_block": (200, 150, 120),
    "torch": (255, 210, 120),
    "lantern": (245, 200, 110),
    "soul_lantern": (110, 180, 190),
    "campfire": (150, 100, 50),
    "redstone_lamp": (140, 100, 60),
    "note_block": (120, 90, 60),
    "jukebox": (110, 80, 50),
    "lodestone": (150, 150, 150),
    "anvil": (70, 70, 72),
    "cauldron": (60, 60, 60),
    "white_wool": (233, 236, 236),
    "orange_wool": (240, 118, 19),
    "magenta_wool": (189, 68, 179),
    "light_blue_wool": (58, 175, 217),
    "yellow_wool": (248, 197, 39),
    "lime_wool": (112, 185, 25),
    "pink_wool": (237, 141, 172),
    "gray_wool": (62, 68, 71),
    "light_gray_wool": (142, 142, 134),
    "cyan_wool": (21, 137, 145),
    "purple_wool": (121, 42, 172),
    "blue_wool": (53, 57, 157),
    "brown_wool": (114, 71, 40),
    "green_wool": (84, 109, 27),
    "red_wool": (160, 39, 34),
    "black_wool": (28, 27, 31),
    "white_concrete": (207, 213, 214),
    "orange_concrete": (224, 97, 0),
    "magenta_concrete": (169, 48, 159),
    "light_blue_concrete": (36, 137, 199),
    "yellow_concrete": (240, 175, 21),
    "lime_concrete": (94, 168, 24),
    "pink_concrete": (213, 101, 142),
    "gray_concrete": (54, 57, 61),
    "light_gray_concrete": (125, 125, 115),
    "cyan_concrete": (21, 119, 136),
    "purple_concrete": (100, 32, 156),
    "blue_concrete": (44, 46, 143),
    "brown_concrete": (96, 60, 32),
    "green_concrete": (73, 91, 36),
    "red_concrete": (142, 32, 32),
    "black_concrete": (8, 10, 15),
    "white_terracotta": (209, 178, 161),
    "terracotta": (152, 94, 67),
    "orange_terracotta": (161, 83, 37),
    "brown_terracotta": (77, 51, 36),
    "red_terracotta": (143, 61, 47),
    "light_gray_terracotta": (135, 107, 98),
    "black_terracotta": (37, 23, 17),
    "white_glazed_terracotta": (188, 212, 202),
    "glass_light_block": (255, 240, 200),
    "stone_brick_slab": (122, 122, 122),
    "oak_slab": (162, 130, 78),
    "oak_stairs": (162, 130, 78),
    "stone_stairs": (125, 125, 125),
    "cobblestone_stairs": (110, 110, 110),
    "cobblestone_wall": (110, 110, 110),
    "oak_fence": (162, 130, 78),
    "oak_door": (162, 130, 78),
    "iron_door": (200, 200, 200),
    "oak_trapdoor": (162, 130, 78),
    "iron_bars": (170, 170, 170),
    "ladder": (150, 120, 70),
    "torchflower": (240, 170, 80),
    "dandelion": (240, 220, 60),
    "poppy": (200, 40, 40),
    "blue_orchid": (40, 160, 200),
    "sunflower": (240, 200, 60),
    "oak_sapling": (90, 150, 60),
    "cactus": (70, 130, 50),
    "sugar_cane": (150, 200, 110),
    "wheat": (200, 180, 70),
    "carrots": (60, 140, 40),
    "potatoes": (90, 150, 50),
    "beetroots": (150, 40, 40),
    "melon": (120, 160, 40),
    "pumpkin": (220, 140, 30),
    "carved_pumpkin": (220, 140, 30),
    "jack_o_lantern": (230, 150, 40),
    "bamboo": (140, 180, 60),
    "vine": (60, 110, 40),
    "lily_pad": (40, 110, 40),
    "seagrass": (60, 130, 80),
    "kelp": (60, 130, 70),
    "sea_pickle": (90, 130, 70),
    "coral_block": (200, 70, 130),
    "spawner": (40, 50, 60),
    "structure_block": (110, 100, 90),
    "barrier": (255, 0, 0),
    "light_block": (255, 240, 200),
    "bedrock_dummy": (85, 85, 85),
}

# Bedrock block id -> texture stem, for the resource-pack mode. Only the cases where they
# differ need an entry; the rest fall back to the id itself.
TEXTURE_ALIASES: dict[str, str] = {
    "minecraft:grass_block": "grass_top",
    "minecraft:oak_log": "log_oak",
    "minecraft:spruce_log": "log_spruce",
    "minecraft:birch_log": "log_birch",
    "minecraft:jungle_log": "log_jungle",
    "minecraft:acacia_log": "log_acacia",
    "minecraft:dark_oak_log": "log_big_oak",
    "minecraft:cobblestone": "cobblestone",
    "minecraft:oak_leaves": "leaves_oak",
    "minecraft:spruce_leaves": "leaves_spruce",
    "minecraft:birch_leaves": "leaves_birch",
    "minecraft:jungle_leaves": "leaves_jungle",
    "minecraft:redstone_block": "redstone_block",
}


def read_png_average(path: str) -> tuple[int, int, int] | None:
    """Average the non-transparent pixels of an 8-bit RGB/RGBA PNG. Stdlib only."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    offset = 8
    width = height = 0
    bit_depth = color_type = 0
    idat = bytearray()
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        chunk = data[offset + 8 : offset + 8 + length]
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
        elif chunk_type == b"IDAT":
            idat += chunk
        elif chunk_type == b"IEND":
            break
        offset += 12 + length
    if bit_depth != 8 or color_type not in (2, 6) or width == 0 or height == 0:
        return None
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error:
        return None
    channels = 4 if color_type == 6 else 3
    stride = width * channels
    total_r = total_g = total_b = 0
    count = 0
    previous = bytearray(stride)
    pos = 0
    for _ in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos : pos + stride])
        pos += stride
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + left) & 0xFF
            elif filter_type == 2:
                line[i] = (line[i] + up) & 0xFF
            elif filter_type == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        for x in range(width):
            base = x * channels
            alpha = line[base + 3] if channels == 4 else 255
            if alpha < 16:
                continue
            total_r += line[base]
            total_g += line[base + 1]
            total_b += line[base + 2]
            count += 1
        previous = line
    if count == 0:
        return None
    return (total_r // count, total_g // count, total_b // count)


def from_textures(texture_dir: str) -> dict[str, tuple[int, int, int]]:
    alias_reverse = {stem: block for block, stem in TEXTURE_ALIASES.items()}
    table: dict[str, tuple[int, int, int]] = {}
    for root, _dirs, files in os.walk(texture_dir):
        for name in files:
            if not name.lower().endswith(".png"):
                continue
            stem = os.path.splitext(name)[0].lower()
            colour = read_png_average(os.path.join(root, name))
            if colour is None:
                continue
            block = alias_reverse.get(stem, "minecraft:" + stem)
            table[block] = colour
    return table


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--textures", help="a vanilla resource-pack textures/blocks directory")
    parser.add_argument("--out", default="data/block_colors.json")
    args = parser.parse_args()

    if args.textures:
        table = from_textures(args.textures)
        if not table:
            raise SystemExit(f"no usable PNGs found under {args.textures}")
        source = f"measured from {args.textures} ({len(table)} textures)"
    else:
        table = {f"minecraft:{name}": rgb for name, rgb in BUILTIN.items()}
        source = f"hand-maintained default table ({len(table)} blocks)"

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as handle:
        json.dump({k: list(v) for k, v in sorted(table.items())}, handle, indent=1)
        handle.write("\n")
    print(f"wrote {args.out}: {source}")


if __name__ == "__main__":
    main()
