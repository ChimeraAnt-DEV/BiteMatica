#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Schematic.hpp"

namespace bitematica::format {

/// One load attempt: the schematic, or a reason the caller can show the player.
struct LoadResult {
  bool success{};
  std::string error;
  core::Schematic schematic;

  static LoadResult ok(core::Schematic schematic) {
    LoadResult result;
    result.success = true;
    result.schematic = std::move(schematic);
    return result;
  }
  static LoadResult fail(std::string message) {
    LoadResult result;
    result.error = std::move(message);
    return result;
  }
};

/// Which on-disk format a file holds.
enum class FileKind {
  Unknown,
  Litematic,   // .litematic  (Litematica / malilib, gzip NBT)
  SpongeSchem, // .schem      (WorldEdit / Sponge, gzip NBT)
  McEdit,      // .schematic  (MCEdit legacy, gzip NBT)
  Structure,   // .nbt        (Bedrock structure block, raw/gzip/zlib NBT)
};

/// Reads every schematic dialect BiteMatica supports and normalizes it.
///
/// Detection is content-first: the extension only picks the order to try, and the
/// reader confirms the shape it finds. That is what lets a `.litematic` and a
/// `.schem` share one entry point while still failing with a specific message
/// ("no Regions compound") rather than "unsupported file".
class SchematicFormat {
public:
  /// Reads a file from raw bytes, guessing the format.
  static LoadResult read(const std::vector<uint8_t> &bytes,
                         const std::string &fileName = {});

  /// Reads a file from disk.
  static LoadResult readPath(const std::string &path);

  /// Classifies bytes without fully parsing, using extension then magic/shape.
  static FileKind detect(const std::vector<uint8_t> &bytes,
                         const std::string &fileName);

  /// The human-readable name of a kind, for the toolbar and diagnostics.
  static const char *kindName(FileKind kind);

  /// True when a filename has an extension BiteMatica can open.
  static bool looksLikeSchematic(const std::string &fileName);
};

} // namespace bitematica::format
