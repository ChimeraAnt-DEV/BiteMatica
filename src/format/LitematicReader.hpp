#pragma once

#include "format/SchematicFormat.hpp"
#include "nbt/NbtTag.hpp"

namespace bitematica::format {

/// Reads Litematica's `.litematic` (the modern Fabric/LiteLoader schematic format).
///
/// Layout: a root compound with `MinecraftDataVersion`, `Version`, `Metadata`
/// (name/author/description) and one or more `Regions`. Each region carries its own
/// `Position`/`Size` (top-left corner plus signed size) and a `BlockStates` LongArray
/// of packed palette indices, plus a `BlockStatePalette` List of compounds with
/// `Name` and a `Properties` compound. Multiple regions are merged into one box.
class LitematicReader {
public:
  static LoadResult read(const nbt::Tag &root);
};

} // namespace bitematica::format
