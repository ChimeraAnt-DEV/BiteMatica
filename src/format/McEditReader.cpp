#include "format/McEditReader.hpp"

#include <string>
#include <unordered_map>

namespace bitematica::format {
namespace {

using core::BlockState;
using core::Schematic;

/// The subset of the legacy numeric block-id table that appears in Bedrock builds.
/// A missing id is rendered as `minecraft:unknown_<id>` so a reader never silently
/// loses blocks, and the count still reflects the true volume.
const std::unordered_map<int, std::string> &legacyIdTable() {
  static const std::unordered_map<int, std::string> table = {
      {0, "minecraft:air"},          {1, "minecraft:stone"},
      {2, "minecraft:grass_block"},  {3, "minecraft:dirt"},
      {4, "minecraft:cobblestone"},  {5, "minecraft:oak_planks"},
      {6, "minecraft:oak_sapling"},  {7, "minecraft:bedrock"},
      {8, "minecraft:water"},        {9, "minecraft:water"},
      {10, "minecraft:lava"},        {11, "minecraft:lava"},
      {12, "minecraft:sand"},        {13, "minecraft:gravel"},
      {14, "minecraft:gold_ore"},    {15, "minecraft:iron_ore"},
      {16, "minecraft:coal_ore"},    {17, "minecraft:oak_log"},
      {18, "minecraft:oak_leaves"},  {19, "minecraft:sponge"},
      {20, "minecraft:glass"},
      {35, "minecraft:white_wool"},  {41, "minecraft:gold_block"},
      {42, "minecraft:iron_block"},  {45, "minecraft:bricks"},
      {46, "minecraft:tnt"},         {47, "minecraft:bookshelf"},
      {48, "minecraft:mossy_cobblestone"},
      {49, "minecraft:obsidian"},    {50, "minecraft:torch"},
      {52, "minecraft:spawner"},     {53, "minecraft:oak_stairs"},
      {54, "minecraft:chest"},       {56, "minecraft:diamond_ore"},
      {57, "minecraft:diamond_block"},
      {58, "minecraft:crafting_table"},
      {61, "minecraft:furnace"},     {65, "minecraft:ladder"},
      {73, "minecraft:redstone_ore"}, {79, "minecraft:ice"},
      {80, "minecraft:snow_block"},  {82, "minecraft:clay"},
      {87, "minecraft:netherrack"},  {88, "minecraft:soul_sand"},
      {89, "minecraft:glowstone"},   {98, "minecraft:stone_bricks"},
      {102, "minecraft:glass_pane"}, {110, "minecraft:mycelium"},
      {112, "minecraft:nether_bricks"},
      {116, "minecraft:enchanting_table"},
      {121, "minecraft:end_stone"},  {129, "minecraft:emerald_ore"},
      {133, "minecraft:emerald_block"},
      {137, "minecraft:command_block"},
      {152, "minecraft:redstone_block"},
      {155, "minecraft:quartz_block"},
      {159, "minecraft:white_terracotta"},
      {161, "minecraft:acacia_leaves"},
      {162, "minecraft:acacia_log"},
      {168, "minecraft:prismarine"},
      {173, "minecraft:coal_block"},
      {179, "minecraft:red_sandstone"},
      {201, "minecraft:purpur_block"},
      {206, "minecraft:end_stone_bricks"},
  };
  return table;
}

std::string legacyName(int id) {
  const auto &table = legacyIdTable();
  auto it = table.find(id);
  if (it != table.end()) return it->second;
  return "minecraft:unknown_" + std::to_string(id);
}

} // namespace

LoadResult McEditReader::read(const nbt::Tag &root) {
  const nbt::Tag *widthTag = root.find("Width");
  const nbt::Tag *heightTag = root.find("Height");
  const nbt::Tag *lengthTag = root.find("Length");
  const nbt::Tag *blocksTag = root.find("Blocks");
  if (widthTag == nullptr || heightTag == nullptr || lengthTag == nullptr ||
      blocksTag == nullptr) {
    return LoadResult::fail("schematic is missing Width/Height/Length/Blocks");
  }

  const int sx = static_cast<int>(widthTag->asInt());
  const int sy = static_cast<int>(heightTag->asInt());
  const int sz = static_cast<int>(lengthTag->asInt());
  if (sx <= 0 || sy <= 0 || sz <= 0) {
    return LoadResult::fail("schematic has an empty region");
  }

  Schematic schematic;
  schematic.sourceFormat = "schematic";
  schematic.bounds = {{0, 0, 0}, {sx - 1, sy - 1, sz - 1}};
  schematic.allocateBlocks();

  const nbt::Tag *dataTag = root.find("Data");
  const nbt::Tag *addTag = root.find("AddBlocks");
  const size_t volume = static_cast<size_t>(sx) * sy * sz;

  for (size_t i = 0; i < volume && i < blocksTag->array.size(); ++i) {
    int id = static_cast<int>(blocksTag->array[i] & 0xff);
    int meta = 0;
    if (dataTag != nullptr && i < dataTag->array.size()) {
      meta = static_cast<int>(dataTag->array[i] & 0x0f);
    }
    // AddBlocks packs a 4-bit high nibble per block, two blocks per byte.
    if (addTag != nullptr && i / 2 < addTag->array.size()) {
      const uint8_t packed = static_cast<uint8_t>(addTag->array[i / 2] & 0xff);
      const int high = (i % 2 == 0) ? (packed & 0x0f) : ((packed >> 4) & 0x0f);
      id |= (high << 8);
    }

    BlockState state;
    state.name = legacyName(id);
    if (meta != 0) state.states.emplace_back("data", std::to_string(meta));
    const uint32_t paletteIndex = schematic.palette.intern(state);

    // MCEdit is also Y-major: index = (y * length + z) * width + x.
    const int x = static_cast<int>(i % sx);
    const int z = static_cast<int>((i / sx) % sz);
    const int y = static_cast<int>(i / (static_cast<size_t>(sx) * sz));
    schematic.setBlock(x, y, z, paletteIndex);
  }

  if (const nbt::Tag *metadata = root.find("Metadata")) {
    if (const nbt::Tag *name = metadata->find("Name")) schematic.name = name->asString("schematic");
    if (const nbt::Tag *author = metadata->find("Author"))
      schematic.author = author->asString();
  }
  if (schematic.name == "unnamed") schematic.name = "schematic";

  return LoadResult::ok(std::move(schematic));
}

} // namespace bitematica::format
