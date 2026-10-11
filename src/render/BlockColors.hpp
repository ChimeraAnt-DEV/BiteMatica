#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bitematica::render {

/// A packed 0xAARRGGBB color.
struct Color {
  uint32_t argb{};

  static Color rgb(int r, int g, int b, int a = 255) {
    return {static_cast<uint32_t>((a & 0xff) << 24) | ((r & 0xff) << 16) |
            ((g & 0xff) << 8) | (b & 0xff)};
  }

  /// Multiplies RGB by `factor` (0..1), keeping alpha. Used for face shading.
  Color shade(float factor) const {
    const int a = static_cast<int>((argb >> 24) & 0xff);
    int r = static_cast<int>((argb >> 16) & 0xff);
    int g = static_cast<int>((argb >> 8) & 0xff);
    int b = static_cast<int>(argb & 0xff);
    r = std::clamp(static_cast<int>(r * factor), 0, 255);
    g = std::clamp(static_cast<int>(g * factor), 0, 255);
    b = std::clamp(static_cast<int>(b * factor), 0, 255);
    return {static_cast<uint32_t>((a << 24) | (r << 16) | (g << 8) | b)};
  }
};

/// Maps a block id to a representative color for the schematic overlay.
///
/// A hands-kept table covers the blocks that dominate real builds so a wall of stone
/// reads grey and grass reads green. Anything outside the table is colored by a hash
/// of its name, which keeps an unknown block visually distinct from its neighbours
/// rather than collapsing to one fallback color.
class BlockColors {
public:
  static Color forBlock(const std::string &name) {
    const std::string key = normalize(name);
    const auto &table = colorTable();
    auto it = table.find(key);
    if (it != table.end()) return it->second;

    // Deterministic hash color, biased to mid-tone so it reads on a dark overlay.
    uint32_t hash = 2166136261u;
    for (char c : key) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    const int r = 90 + static_cast<int>((hash >> 16) & 0x5f);
    const int g = 90 + static_cast<int>((hash >> 8) & 0x5f);
    const int b = 90 + static_cast<int>(hash & 0x5f);
    return Color::rgb(r, g, b);
  }

private:
  static std::string normalize(const std::string &name) {
    const size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
  }

  static const std::unordered_map<std::string, Color> &colorTable() {
    static const std::unordered_map<std::string, Color> table = {
        {"air", Color::rgb(0, 0, 0, 0)},
        {"stone", Color::rgb(125, 125, 125)},
        {"cobblestone", Color::rgb(110, 110, 110)},
        {"stone_bricks", Color::rgb(122, 122, 122)},
        {"mossy_cobblestone", Color::rgb(95, 120, 90)},
        {"dirt", Color::rgb(134, 96, 67)},
        {"grass_block", Color::rgb(100, 160, 70)},
        {"sand", Color::rgb(219, 207, 163)},
        {"sandstone", Color::rgb(216, 203, 155)},
        {"red_sand", Color::rgb(190, 100, 40)},
        {"gravel", Color::rgb(136, 126, 126)},
        {"clay", Color::rgb(160, 166, 179)},
        {"bedrock", Color::rgb(85, 85, 85)},
        {"obsidian", Color::rgb(20, 18, 29)},
        {"netherrack", Color::rgb(120, 50, 50)},
        {"soul_sand", Color::rgb(82, 62, 50)},
        {"glowstone", Color::rgb(220, 190, 110)},
        {"oak_log", Color::rgb(150, 120, 70)},
        {"oak_planks", Color::rgb(162, 130, 78)},
        {"spruce_planks", Color::rgb(114, 84, 48)},
        {"birch_planks", Color::rgb(200, 180, 130)},
        {"oak_leaves", Color::rgb(60, 130, 50)},
        {"glass", Color::rgb(180, 220, 235)},
        {"glass_pane", Color::rgb(180, 220, 235)},
        {"water", Color::rgb(50, 90, 200)},
        {"lava", Color::rgb(220, 100, 20)},
        {"ice", Color::rgb(150, 190, 240)},
        {"snow_block", Color::rgb(240, 240, 245)},
        {"bricks", Color::rgb(150, 97, 83)},
        {"nether_bricks", Color::rgb(44, 22, 26)},
        {"quartz_block", Color::rgb(235, 230, 224)},
        {"purpur_block", Color::rgb(169, 125, 169)},
        {"prismarine", Color::rgb(99, 171, 158)},
        {"end_stone", Color::rgb(219, 222, 158)},
        {"end_stone_bricks", Color::rgb(219, 222, 158)},
        {"iron_block", Color::rgb(220, 220, 220)},
        {"gold_block", Color::rgb(246, 208, 61)},
        {"diamond_block", Color::rgb(97, 219, 213)},
        {"emerald_block", Color::rgb(42, 203, 86)},
        {"redstone_block", Color::rgb(175, 24, 5)},
        {"lapis_block", Color::rgb(30, 67, 140)},
        {"coal_block", Color::rgb(16, 16, 16)},
        {"white_wool", Color::rgb(233, 236, 236)},
        {"red_wool", Color::rgb(160, 39, 34)},
        {"blue_wool", Color::rgb(53, 57, 157)},
        {"green_wool", Color::rgb(84, 109, 27)},
        {"yellow_wool", Color::rgb(248, 197, 39)},
        {"black_wool", Color::rgb(28, 27, 31)},
        {"orange_wool", Color::rgb(240, 118, 19)},
        {"purple_wool", Color::rgb(121, 42, 172)},
        {"white_concrete", Color::rgb(207, 213, 214)},
        {"red_concrete", Color::rgb(142, 32, 32)},
        {"blue_concrete", Color::rgb(44, 46, 143)},
        {"black_concrete", Color::rgb(8, 10, 15)},
        {"white_terracotta", Color::rgb(209, 178, 161)},
        {"terracotta", Color::rgb(152, 94, 67)},
        {"torch", Color::rgb(255, 210, 120)},
        {"lantern", Color::rgb(245, 200, 110)},
        {"sea_lantern", Color::rgb(190, 220, 210)},
        {"chest", Color::rgb(150, 110, 60)},
        {"crafting_table", Color::rgb(140, 100, 60)},
        {"furnace", Color::rgb(110, 110, 110)},
        {"bookshelf", Color::rgb(160, 130, 90)},
        {"tnt", Color::rgb(200, 60, 50)},
        {"command_block", Color::rgb(200, 150, 120)},
        {"beacon", Color::rgb(120, 220, 220)},
    };
    return table;
  }
};

} // namespace bitematica::render
