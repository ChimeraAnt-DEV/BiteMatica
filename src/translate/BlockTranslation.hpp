#pragma once

#include <string>
#include <vector>

#include "core/BlockState.hpp"

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
};

} // namespace bitematica::translate
