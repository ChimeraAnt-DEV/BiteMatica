#pragma once

#include "format/SchematicFormat.hpp"
#include "nbt/NbtTag.hpp"

namespace bitematica::format {

/// Reads the Sponge/WorldEdit `.schem` format.
///
/// Layout: a root compound with `Version`, `Width`/`Height`/`Length` shorts, a
/// `Palette` compound mapping `"minecraft:stone[foo=bar]"` -> int id, and a
/// `BlockData` byte array of varint-encoded palette ids in Y-major order. Also the
/// source of the `Offset` int array that places the build relative to the player.
class SpongeReader {
public:
  static LoadResult read(const nbt::Tag &root);
};

} // namespace bitematica::format
