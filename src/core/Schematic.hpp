#pragma once

#include <string>
#include <vector>

#include "core/BlockState.hpp"
#include "core/BlockPos.hpp"

namespace bitematica::core {

/// A parsed schematic: a palette plus one palette index per block in a box.
///
/// The structure is deliberately the common denominator of every format BiteMatica
/// reads. Litematica's `.litematic`, WorldEdit's `.schem`, MCEdit's `.schematic` and
/// the modern structure `.nbt` all reduce to "a box of palette ids", so a single
/// in-memory shape is what lets the renderer and the material list stay format-blind.
///
/// Blocks are addressed with `(y * sizeZ + z) * sizeX + x`, the same Y-major order
/// the mesh builder walks so a render pass touches memory sequentially.
struct Schematic {
  std::string name{"unnamed"};
  std::string author;
  /// The format the file was read from, for diagnostics: "litematic", "schem", ...
  std::string sourceFormat;
  std::string description;

  /// Inclusive local bounds of the populated region.
  Bounds bounds{};
  /// World placement metadata carried by some formats, in schematic-local space.
  BlockPos originOffset{};

  BlockPalette palette;
  /// One palette index per block, sized bounds.volume(); 0 is expected to be air.
  std::vector<uint32_t> blocks;
  /// Bedrock-only: block-entity NBT keyed by local position index, kept as raw bytes so a
  /// block entity (chest contents, sign text) survives a load even before it is interpreted.
  std::vector<std::pair<int, std::string>> blockEntities;

  int sizeX() const { return bounds.sizeX(); }
  int sizeY() const { return bounds.sizeY(); }
  int sizeZ() const { return bounds.sizeZ(); }
  long long volume() const { return bounds.volume(); }
  bool empty() const { return blocks.empty(); }

  /// True when a local position is inside the populated region.
  bool inBounds(const LocalPos &p) const { return bounds.contains(p); }

  /// Index into `blocks` for a local grid position; caller must have checked bounds.
  int indexOf(int x, int y, int z) const {
    return (y * sizeZ() + z) * sizeX() + x;
  }

  /// Palette index at a local grid position, or 0 (air) when out of bounds.
  uint32_t blockAt(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= sizeX() || y >= sizeY() || z >= sizeZ()) {
      return 0;
    }
    const int index = indexOf(x, y, z);
    if (index < 0 || index >= static_cast<int>(blocks.size())) return 0;
    return blocks[static_cast<size_t>(index)];
  }

  const BlockState &stateAt(int x, int y, int z) const {
    const uint32_t paletteIndex = blockAt(x, y, z);
    if (paletteIndex < palette.size()) return palette.at(paletteIndex);
    return air();
  }

  static const BlockState &air() {
    static const BlockState kAir{};
    return kAir;
  }

  /// Allocates the block array for the current bounds, filled with 0 (air).
  void allocateBlocks() {
    blocks.assign(static_cast<size_t>(volume()), 0);
  }

  /// Writes a palette index into `blocks`, bounds-checked.
  void setBlock(int x, int y, int z, uint32_t paletteIndex) {
    if (x < 0 || y < 0 || z < 0 || x >= sizeX() || y >= sizeY() || z >= sizeZ()) return;
    blocks[static_cast<size_t>(indexOf(x, y, z))] = paletteIndex;
  }
};

} // namespace bitematica::core
