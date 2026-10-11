#pragma once

#include <string>

#include "core/Schematic.hpp"

namespace bitematica::format {

/// Writes an in-memory schematic as a Bedrock structure file (`.mcstructure`).
///
/// This is the round-trip half of the `.nbt` reader and the toolbar's export action:
/// a player can load a Litematica build and hand a Bedrock-native file to the structure
/// block or another tool. The output is uncompressed NBT (the form the game reads),
/// and the palette is written with string states so no state is lost on the way out.
class StructureWriter {
public:
  /// Serializes `schematic` to `path`. Returns false with `error` on failure.
  static bool write(const core::Schematic &schematic, const std::string &path,
                    std::string &error);

  /// The bytes of a structure file, for tests that do not want to touch the disk.
  static std::string bytes(const core::Schematic &schematic);
};

} // namespace bitematica::format
