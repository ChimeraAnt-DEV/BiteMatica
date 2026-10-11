#pragma once

#include "format/SchematicFormat.hpp"
#include "nbt/NbtTag.hpp"

namespace bitematica::format {

/// Reads Bedrock's own structure file (`.nbt` / `.mcstructure`).
///
/// This is the one format that is already Bedrock-native, so it is what BiteMatica
/// exports to as well as imports from. Layout: `format_version`, a `size` int list,
/// a `palette` compound whose `default` child holds a `block_palette` list, and a
/// `block_indices` list of two int lists (main layer, then the extra layer used for
/// waterlogging). Only the main layer is read; block indices are X-major.
class BedrockStructureReader {
public:
  static LoadResult read(const nbt::Tag &root);
};

} // namespace bitematica::format
