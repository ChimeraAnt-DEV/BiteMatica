#include "format/LitematicReader.hpp"

#include <algorithm>
#include <cstdint>

namespace bitematica::format {
namespace {

using core::BlockState;
using core::LocalPos;
using core::Schematic;

/// Number of palette bits needed to index `paletteSize` entries, minimum of 2.
/// Litematica pads the width so a single-entry palette still uses 2 bits.
int bitsFor(int paletteSize) {
  int bits = 2;
  while ((1 << bits) < paletteSize) ++bits;
  return bits;
}

BlockState readPaletteEntry(const nbt::Tag &entry) {
  BlockState state;
  const nbt::Tag *nameTag = entry.find("Name");
  state.name = nameTag != nullptr ? nameTag->asString("minecraft:air") : "minecraft:air";
  if (const nbt::Tag *properties = entry.find("Properties")) {
    if (properties->isCompound()) {
      for (const nbt::Tag &property : properties->children) {
        state.states.emplace_back(property.name, property.asString());
      }
    }
  }
  return state;
}

/// Unpacks a Litematica `BlockStates` LongArray into palette indices.
///
/// Values are packed little-endian-bit-order into 64-bit longs, and an entry may
/// straddle a long boundary. A 1.16+ file is `bits` wide; older files always used
/// 4 bits and relied on the array being long enough to disambiguate. When the array
/// is larger than `bits` needs, the extra longs are the old-format padding and the
/// smaller width is the real one.
std::vector<uint32_t> unpack(const std::vector<int64_t> &packed, size_t volume,
                             int paletteSize, int bits) {
  std::vector<uint32_t> out(volume, 0);

  // Legacy files always packed at 4 bits and padded the array; if the array is too
  // short for the computed width, the 4-bit layout is the one that actually fits.
  int width = bits;
  const size_t perLong = width > 0 ? 64 / static_cast<size_t>(width) : 0;
  const size_t needed = perLong > 0 ? (volume + perLong - 1) / perLong : 0;
  if (width <= 0 || packed.size() < needed) {
    width = 4;
  }
  if (width > 64) width = 64;
  (void)paletteSize;

  const uint64_t mask = width >= 64 ? ~0ull : ((1ull << width) - 1);
  const size_t stride = 64 / static_cast<size_t>(width);
  for (size_t i = 0; i < volume; ++i) {
    const size_t longIndex = i / stride;
    const size_t offset = (i % stride) * static_cast<size_t>(width);
    if (longIndex >= packed.size()) break;
    const uint64_t value = (static_cast<uint64_t>(packed[longIndex]) >> offset) & mask;
    out[i] = static_cast<uint32_t>(value);
  }
  return out;
}

struct Region {
  LocalPos position;
  int sizeX{};
  int sizeY{};
  int sizeZ{};
  std::vector<uint32_t> blocks; // in region-local order
};

} // namespace

LoadResult LitematicReader::read(const nbt::Tag &root) {
  const nbt::Tag *regions = root.find("Regions");
  if (regions == nullptr || !regions->isCompound() || regions->children.empty()) {
    return LoadResult::fail("litematic has no Regions compound");
  }

  std::vector<Region> parsed;
  for (const nbt::Tag &regionTag : regions->children) {
    const nbt::Tag *posTag = regionTag.find("Position");
    const nbt::Tag *sizeTag = regionTag.find("Size");
    const nbt::Tag *paletteTag = regionTag.find("BlockStatePalette");
    const nbt::Tag *blocksTag = regionTag.find("BlockStates");
    if (posTag == nullptr || sizeTag == nullptr || paletteTag == nullptr ||
        blocksTag == nullptr) {
      continue; // A region missing a required member is skipped, not fatal.
    }

    Region region;
    // Position children are ints named x/y/z.
    region.position.x = static_cast<int>(posTag->find("x") ? posTag->find("x")->asInt() : 0);
    region.position.y = static_cast<int>(posTag->find("y") ? posTag->find("y")->asInt() : 0);
    region.position.z = static_cast<int>(posTag->find("z") ? posTag->find("z")->asInt() : 0);

    const int sx = static_cast<int>(sizeTag->find("x") ? sizeTag->find("x")->asInt() : 0);
    const int sy = static_cast<int>(sizeTag->find("y") ? sizeTag->find("y")->asInt() : 0);
    const int sz = static_cast<int>(sizeTag->find("z") ? sizeTag->find("z")->asInt() : 0);
    // A negative size means the region extends in the negative direction; the reader
    // normalizes to a positive size and shifts the position to the new minimum corner.
    region.sizeX = std::abs(sx);
    region.sizeY = std::abs(sy);
    region.sizeZ = std::abs(sz);
    if (sx < 0) region.position.x += sx + 1;
    if (sy < 0) region.position.y += sy + 1;
    if (sz < 0) region.position.z += sz + 1;
    if (region.sizeX <= 0 || region.sizeY <= 0 || region.sizeZ <= 0) continue;

    const int paletteSize = paletteTag->size();
    if (paletteSize <= 0) continue;
    const size_t volume = static_cast<size_t>(region.sizeX) * region.sizeY * region.sizeZ;
    const int bits = bitsFor(paletteSize);
    region.blocks = unpack(blocksTag->array, volume, paletteSize, bits);
    parsed.push_back(std::move(region));
  }

  if (parsed.empty()) {
    return LoadResult::fail("litematic contains no readable regions");
  }

  // Merge every region into one box spanning their union.
  LocalPos minPos{parsed.front().position};
  LocalPos maxPos{parsed.front().position};
  for (const Region &region : parsed) {
    minPos.x = std::min(minPos.x, region.position.x);
    minPos.y = std::min(minPos.y, region.position.y);
    minPos.z = std::min(minPos.z, region.position.z);
    maxPos.x = std::max(maxPos.x, region.position.x + region.sizeX - 1);
    maxPos.y = std::max(maxPos.y, region.position.y + region.sizeY - 1);
    maxPos.z = std::max(maxPos.z, region.position.z + region.sizeZ - 1);
  }

  Schematic schematic;
  schematic.sourceFormat = "litematic";
  schematic.bounds = {minPos, maxPos};
  schematic.allocateBlocks();

  // Each region owns its palette, so walk the region tags again to map their
  // palette indices into the merged box's shared palette.
  size_t regionIndex = 0;
  for (const nbt::Tag &regionTag : regions->children) {
    if (regionIndex >= parsed.size()) break;
    const Region &region = parsed[regionIndex];
    ++regionIndex;
    const nbt::Tag *paletteTag = regionTag.find("BlockStatePalette");
    if (paletteTag == nullptr) continue;

    // Region-local block indices are X-major; translate to the merged box.
    std::vector<uint32_t> mergedPalette(paletteTag->size());
    for (int i = 0; i < paletteTag->size(); ++i) {
      mergedPalette[i] = schematic.palette.intern(readPaletteEntry(paletteTag->children[i]));
    }
    for (int y = 0; y < region.sizeY; ++y) {
      for (int z = 0; z < region.sizeZ; ++z) {
        for (int x = 0; x < region.sizeX; ++x) {
          const size_t localIndex =
              (static_cast<size_t>(y) * region.sizeZ + z) * region.sizeX + x;
          if (localIndex >= region.blocks.size()) continue;
          const uint32_t pal = region.blocks[localIndex];
          if (pal >= mergedPalette.size()) continue;
          const int wx = region.position.x - minPos.x + x;
          const int wy = region.position.y - minPos.y + y;
          const int wz = region.position.z - minPos.z + z;
          schematic.setBlock(wx, wy, wz, mergedPalette[pal]);
        }
      }
    }
  }

  if (const nbt::Tag *metadata = root.find("Metadata")) {
    schematic.name = metadata->find("Name") ? metadata->find("Name")->asString("litematic")
                                            : "litematic";
    schematic.author = metadata->find("Author") ? metadata->find("Author")->asString("") : "";
    schematic.description =
        metadata->find("Description") ? metadata->find("Description")->asString("") : "";
  } else {
    schematic.name = "litematic";
  }

  return LoadResult::ok(std::move(schematic));
}

} // namespace bitematica::format
