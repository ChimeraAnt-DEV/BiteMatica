#pragma once

#include "format/SchematicFormat.hpp"
#include "nbt/NbtTag.hpp"

namespace bitematica::format {

/// Reads the legacy MCEdit `.schematic` format used by many Bedrock tools.
///
/// Layout: `Width`/`Height`/`Length` shorts, a `Blocks` ByteArray of numeric block
/// ids, an optional `Data` ByteArray of metadata nibbles, and optional `AddBlocks`
/// for ids above 255. This format predates namespaced ids, so a numeric id is
/// converted to the modern `minecraft:<name>` with a small built-in table covering
/// the ids that commonly appear in Bedrock-oriented builds; an id outside the table
/// becomes `minecraft:unknown_<id>` rather than being dropped, so the shape is kept.
class McEditReader {
public:
  static LoadResult read(const nbt::Tag &root);
};

} // namespace bitematica::format
