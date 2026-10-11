#include "format/SpongeReader.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

#include "format/BlockStateNaming.hpp"

namespace bitematica::format {
namespace {

using core::BlockState;
using core::Schematic;

/// `.schem` `BlockData` is a stream of LEB128 varints, one per block, but a value
/// above 127 is stored as a two-byte "marker" varint (the high bytes are a flag, not
/// part of a multi-byte read). Sponge's reader treats a byte with the high bit set as
/// a two-byte value: the low 7 bits of the first byte and the following byte.
std::vector<uint32_t> decodeVarInts(const std::vector<int64_t> &blockData,
                                    size_t expected) {
  std::vector<uint32_t> out;
  out.reserve(expected);
  size_t i = 0;
  while (i < blockData.size()) {
    const uint8_t first = static_cast<uint8_t>(blockData[i] & 0xff);
    if ((first & 0x80) == 0) {
      out.push_back(first);
      ++i;
    } else {
      if (i + 1 >= blockData.size()) break;
      const uint8_t second = static_cast<uint8_t>(blockData[i + 1] & 0xff);
      const uint32_t value = (static_cast<uint32_t>(first & 0x7f) << 8) | second;
      out.push_back(value);
      i += 2;
    }
  }
  return out;
}

} // namespace

LoadResult SpongeReader::read(const nbt::Tag &root) {
  const nbt::Tag *widthTag = root.find("Width");
  const nbt::Tag *heightTag = root.find("Height");
  const nbt::Tag *lengthTag = root.find("Length");
  const nbt::Tag *paletteTag = root.find("Palette");
  const nbt::Tag *blockDataTag = root.find("BlockData");

  if (widthTag == nullptr || heightTag == nullptr || lengthTag == nullptr) {
    return LoadResult::fail("schem is missing Width/Height/Length");
  }
  if (paletteTag == nullptr || !paletteTag->isCompound()) {
    return LoadResult::fail("schem is missing a Palette compound");
  }
  if (blockDataTag == nullptr) {
    // A `.schem` with no BlockData is the (rare) empty variant; treat it as unsupported
    // rather than returning a schematic of pure air.
    return LoadResult::fail("schem has no BlockData array");
  }

  const int sx = static_cast<int>(widthTag->asInt());
  const int sy = static_cast<int>(heightTag->asInt());
  const int sz = static_cast<int>(lengthTag->asInt());
  if (sx <= 0 || sy <= 0 || sz <= 0) {
    return LoadResult::fail("schem has an empty region");
  }

  Schematic schematic;
  schematic.sourceFormat = "schem";
  schematic.bounds = {{0, 0, 0}, {sx - 1, sy - 1, sz - 1}};
  schematic.allocateBlocks();

  // The palette compound maps a state string to its numeric id, so invert it into a
  // vector indexed by id. Ids are not guaranteed contiguous, so size to the max id.
  int maxId = 0;
  for (const nbt::Tag &entry : paletteTag->children) {
    maxId = std::max(maxId, static_cast<int>(entry.asInt()));
  }
  std::vector<uint32_t> idToPalette(static_cast<size_t>(maxId) + 1, 0);
  for (const nbt::Tag &entry : paletteTag->children) {
    const int id = static_cast<int>(entry.asInt());
    if (id < 0) continue;
    BlockState state = parseJavaState(entry.name);
    idToPalette[static_cast<size_t>(id)] = schematic.palette.intern(state);
  }

  const size_t volume = static_cast<size_t>(sx) * sy * sz;
  const std::vector<uint32_t> indices = decodeVarInts(blockDataTag->array, volume);

  // Sponge stores blocks Y-major: index = (y * length + z) * width + x.
  for (size_t i = 0; i < indices.size() && i < volume; ++i) {
    const uint32_t paletteIndex = indices[i];
    if (paletteIndex >= schematic.palette.size()) continue;
    const int x = static_cast<int>(i % sx);
    const int z = static_cast<int>((i / sx) % sz);
    const int y = static_cast<int>(i / (static_cast<size_t>(sx) * sz));
    schematic.setBlock(x, y, z, paletteIndex);
  }

  if (const nbt::Tag *offset = root.find("Offset")) {
    if (offset->array.size() >= 3) {
      schematic.originOffset = {static_cast<int>(offset->array[0]),
                                static_cast<int>(offset->array[1]),
                                static_cast<int>(offset->array[2])};
    }
  }

  if (const nbt::Tag *metadata = root.find("Metadata")) {
    if (const nbt::Tag *name = metadata->find("Name")) schematic.name = name->asString("schem");
    if (const nbt::Tag *author = metadata->find("Author"))
      schematic.author = author->asString();
    if (const nbt::Tag *description = metadata->find("Description"))
      schematic.description = description->asString();
  }
  if (schematic.name == "unnamed") schematic.name = "schem";

  return LoadResult::ok(std::move(schematic));
}

} // namespace bitematica::format
