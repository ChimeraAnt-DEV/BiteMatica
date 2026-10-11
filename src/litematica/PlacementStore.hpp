#pragma once

#include <string>
#include <vector>

#include "litematica/PlacementManager.hpp"

namespace bitematica::litematica {

/// Persists the placement list to `placements.json` (D1).
///
/// Only the *placement* is saved, never the schematic's blocks: a placement records which file it
/// came from (`schematicId`) and where it sits, and the schematic is re-read on load. That keeps the
/// file small and makes a placement reference a file the user can move or rename.
///
/// The format is versioned. A file from an unknown future version is refused with a named error
/// rather than half-read, and an unknown *older* version is upgraded field by field (missing fields
/// take their defaults). Writes are atomic: a temp file plus a rename, so a crash mid-write cannot
/// leave a truncated placements file.
///
/// Pure and host-testable: no Android, no filesystem beyond the two path-taking helpers.
class PlacementStore {
public:
  /// The current on-disk schema version.
  static constexpr int kVersion = 1;

  /// Serializes a placement list to JSON text.
  static std::string serialize(const std::vector<PlacedSchematic> &placements);

  /// Parses JSON text into a placement list.
  ///
  /// The schematic pointers are left null (the caller resolves them from `schematicId`). Returns
  /// false with `error` set on malformed input or an unsupported version.
  static bool parse(const std::string &json, std::vector<PlacedSchematic> &out, std::string &error);

  /// Writes to `path` atomically (temp file + rename). Returns false with `error` on failure.
  static bool writeFile(const std::string &path,
                        const std::vector<PlacedSchematic> &placements, std::string &error);

  /// Reads from `path`. A missing file is reported as an error so the caller can tell "no
  /// placements saved yet" from "the file is corrupt".
  static bool readFile(const std::string &path, std::vector<PlacedSchematic> &out,
                       std::string &error);
};

} // namespace bitematica::litematica
