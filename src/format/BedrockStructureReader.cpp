#include "format/BedrockStructureReader.hpp"

#include <cstdint>
#include <vector>

#include "format/BlockStateNaming.hpp"

namespace bitematica::format {
namespace {

using core::BlockState;
using core::Schematic;

/// Reads one entry of `block_palette` into a portable BlockState.
BlockState readPaletteEntry(const nbt::Tag &entry) {
  std::vector<std::pair<std::string, std::string>> states;
  if (const nbt::Tag *statesTag = entry.find("states")) {
    if (statesTag->isCompound()) {
      for (const nbt::Tag &state : statesTag->children) {
        // Bedrock states can be byte/int/string; render uniformly as text.
        std::string value;
        if (state.isString()) {
          value = state.stringValue;
        } else if (state.type == nbt::TagType::Float || state.type == nbt::TagType::Double) {
          value = std::to_string(state.doubleValue);
        } else {
          value = std::to_string(state.intValue);
        }
        states.emplace_back(state.name, value);
      }
    }
  }
  const nbt::Tag *nameTag = entry.find("name");
  return parseBedrockState(nameTag != nullptr ? nameTag->asString("minecraft:air")
                                              : "minecraft:air",
                           states);
}

/// Finds the nested `block_palette` list, tolerating the two shapes Bedrock writes.
const nbt::Tag *findBlockPalette(const nbt::Tag &root) {
  if (const nbt::Tag *palette = root.find("palette")) {
    if (const nbt::Tag *def = palette->find("default")) {
      if (const nbt::Tag *list = def->find("block_palette")) return list;
    }
    // Some files nest a single unnamed entry directly.
    if (const nbt::Tag *list = palette->find("block_palette")) return list;
  }
  if (const nbt::Tag *list = root.find("block_palette")) return list;
  return nullptr;
}

} // namespace

LoadResult BedrockStructureReader::read(const nbt::Tag &root) {
  const nbt::Tag *sizeTag = root.find("size");
  if (sizeTag == nullptr || !sizeTag->isList() || sizeTag->size() < 3) {
    return LoadResult::fail("structure is missing a size list");
  }
  const int sx = static_cast<int>(sizeTag->children[0].asInt());
  const int sy = static_cast<int>(sizeTag->children[1].asInt());
  const int sz = static_cast<int>(sizeTag->children[2].asInt());
  if (sx <= 0 || sy <= 0 || sz <= 0) {
    return LoadResult::fail("structure has an empty size");
  }

  const nbt::Tag *paletteTag = findBlockPalette(root);
  if (paletteTag == nullptr || !paletteTag->isList()) {
    return LoadResult::fail("structure is missing a block_palette");
  }
  const nbt::Tag *indicesTag = root.find("block_indices");
  if (indicesTag == nullptr || !indicesTag->isList() || indicesTag->children.empty()) {
    return LoadResult::fail("structure is missing block_indices");
  }

  Schematic schematic;
  schematic.sourceFormat = "structure";
  schematic.bounds = {{0, 0, 0}, {sx - 1, sy - 1, sz - 1}};
  schematic.allocateBlocks();

  std::vector<uint32_t> paletteMap(paletteTag->children.size(), 0);
  for (size_t i = 0; i < paletteTag->children.size(); ++i) {
    paletteMap[i] = schematic.palette.intern(readPaletteEntry(paletteTag->children[i]));
  }

  // block_indices is a list of layers; the first is the main block layer.
  const nbt::Tag &mainLayer = indicesTag->children.front();
  const size_t volume = static_cast<size_t>(sx) * sy * sz;
  for (size_t i = 0; i < volume && i < mainLayer.children.size(); ++i) {
    const int paletteIndex = static_cast<int>(mainLayer.children[i].asInt(-1));
    if (paletteIndex < 0 || static_cast<size_t>(paletteIndex) >= paletteMap.size()) continue;
    const int x = static_cast<int>(i % sx);
    const int z = static_cast<int>((i / sx) % sz);
    const int y = static_cast<int>(i / (static_cast<size_t>(sx) * sz));
    schematic.setBlock(x, y, z, paletteMap[static_cast<size_t>(paletteIndex)]);
  }

  if (const nbt::Tag *origin = root.find("structure_world_origin")) {
    if (origin->isList() && origin->size() >= 3) {
      schematic.originOffset = {static_cast<int>(origin->children[0].asInt()),
                                static_cast<int>(origin->children[1].asInt()),
                                static_cast<int>(origin->children[2].asInt())};
    }
  }

  schematic.name = "structure";
  return LoadResult::ok(std::move(schematic));
}

} // namespace bitematica::format
