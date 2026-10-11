#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Schematic.hpp"
#include "world/Placement.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::world {

/// The verification state of one schematic block against the live world.
enum class BlockStatus : uint8_t {
  Correct = 0,  ///< the world block matches what the schematic wants
  Missing = 1,  ///< the world has air (or a different block) where the schematic wants one
  Wrong = 2,    ///< the world has a block there, but not the right one
  Extra = 3,    ///< the world has a block where the schematic wants air
  Unknown = 4,  ///< the world could not be read (no source / out of range)
};

/// The result of verifying a placement against the world.
struct VerifyResult {
  bool available{false};
  std::string error;
  long long correct{};
  long long missing{};
  long long wrong{};
  long long extra{};
  long long unknown{};
  /// Per schematic-local block status, indexed like `Schematic::blocks`. Empty when unavailable.
  std::vector<BlockStatus> statuses;

  long long total() const { return correct + missing + wrong + extra + unknown; }
  /// Percentage of checked blocks that are correct, 0 when nothing was checked.
  int percentComplete() const {
    const long long checked = correct + missing + wrong + extra;
    if (checked <= 0) return 0;
    return static_cast<int>((correct * 100) / checked);
  }
};

/// Compares a placement's schematic against the live world.
///
/// This is the feature that makes the mod useful: it turns "here is where the blocks go" into "here
/// is what you still have to place". The comparison is intentionally strict about state — a block
/// of the right type with the wrong state is `Wrong`, not `Correct` — and it reports `Extra` for
/// world blocks sitting where the schematic wants air, so a build that was started and abandoned is
/// visible rather than silently accepted.
class Verifier {
public:
  /// Verifies every non-air (and, optionally, air) block of a placement.
  ///
  /// @param schematic the schematic
  /// @param placement where it sits
  /// @param source the world to compare against
  /// @param layerMin,layerMax inclusive local Y range to check (layer modes)
  /// @param reportExtras also check air cells for stray world blocks
  static VerifyResult verify(const core::Schematic &schematic, const Placement &placement,
                             WorldSource &source, int layerMin, int layerMax,
                             bool reportExtras = false);

  /// The display colour for a status, as 0xAARRGGBB.
  static uint32_t colorFor(BlockStatus status);
};

} // namespace bitematica::world
