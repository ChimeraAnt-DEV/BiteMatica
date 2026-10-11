#pragma once

#include <string>
#include <vector>

#include "core/Schematic.hpp"

namespace bitematica::format {

/// Writes an in-memory schematic as a Litematica `.litematic` file.
///
/// This is the selection tool's other half: a player marks two corners, the mod captures the region
/// into a Schematic, and this writes it back out in the same format Litematica reads, so a build
/// made on Bedrock can be shared with Java Edition players. The output is gzip-wrapped NBT with the
/// `Regions`/`BlockStates` long-array packing the reader expects, and it round-trips through
/// {@link LitematicReader} (pinned by the tests).
class LitematicWriter {
public:
  /// Serializes a schematic to `.litematic` bytes.
  static std::string bytes(const core::Schematic &schematic, const std::string &name = {},
                           const std::string &author = {});

  /// Writes a `.litematic` to a path. Returns false with `error` on failure.
  static bool write(const core::Schematic &schematic, const std::string &path,
                    const std::string &name, const std::string &author, std::string &error);
};

} // namespace bitematica::format
