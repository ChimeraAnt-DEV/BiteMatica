#pragma once

#include <string>
#include <vector>

#include "core/BlockState.hpp"
#include "core/BlockShape.hpp"

namespace bitematica::translate {

/// Translates a Java Edition block state into the closest Bedrock block state.
///
/// Java and Bedrock do not share a block-state vocabulary. A Java schematic says
/// `minecraft:oak_stairs[facing=north,half=bottom]`; Bedrock's stairs are
/// `minecraft:oak_stairs` with `weirdo_direction`/`upside_down_bit`, and many Java blocks
/// (`grass_block`, `oak_planks` variants, `redstone_wire`) have different names or states entirely.
/// Loading a Java schematic without translating produces blocks that are "right-ish" by id but
/// wrong by state — stairs that face the wrong way, slabs that are half-height in the wrong half.
///
/// This is a hand-maintained table, not a runtime dependency: pulling in a full Geyser-style
/// mapping at runtime would be a large data file to ship and version. The table covers the
/// families where the state actually differs (stairs, slabs, doors, trapdoors, fences, walls,
/// logs, leaves, redstone, waterlogging) and falls back to passing the id through unchanged with
/// its states dropped, which is correct for the many blocks whose id is identical on both sides.
///
/// `BlockTranslationTest` pins the direction inversions (Java `facing` -> Bedrock
/// `weirdo_direction` is *not* an identity) and the pass-through fallback.
class BlockTranslation {
public:
  /// Translates a state, returning the Bedrock equivalent.
  static core::BlockState toBedrock(const core::BlockState &java);

  /// True when a block id has an explicit rule (as opposed to the pass-through fallback).
  static bool hasExplicitRule(const std::string &javaName);

  /// A short description of what was translated, for the load diagnostics panel.
  static std::string describe(const core::BlockState &java, const core::BlockState &bedrock);

  /// True when a block id has neither a rename rule nor a state rule and is not a known
  /// pass-through id. Such a block is drawn as a distinctive placeholder (G4) and listed under
  /// "Unmapped" rather than silently treated as the right block.
  static bool isUnmapped(const std::string &javaName);

  /// Merges a `data/block_translation.json` table over the built-in renames (G2). The JSON is
  /// `{ "blocks": { "minecraft:oak_planks": "minecraft:planks", ... } }`. Returns the number
  /// merged. A malformed entry is skipped, not fatal.
  static int loadJson(const std::string &json);
  /// Loads a file if present; a missing file is not an error.
  static int loadFile(const std::string &path);

  /// The distinct unmapped ids seen since the last `clearUnmapped()`, for the status line and log.
  static std::vector<std::string> unmappedBlocks();
  static void clearUnmapped();

  /// The placeholder block drawn for an unmapped id (G4): a distinctive magenta cube.
  static core::BlockState placeholderFor(const std::string &javaName);

  /// The `block_version` constant for the structure writer (G7), and the highest known-good
  /// version. A game version newer than the table triggers a warning.
  static constexpr int kBlockVersion = 18163713;
  static constexpr const char *kKnownGoodVersions = "1.26.33,1.26.50,1.26.51";
  /// True when the running game version is newer than the known-good table.
  static bool isVersionNewerThanTable(const std::string &runningVersion);

private:
  /// Records an id in the unmapped registry (capped, de-duplicated).
  static void recordUnmapped(const std::string &id);
};

} // namespace bitematica::translate
