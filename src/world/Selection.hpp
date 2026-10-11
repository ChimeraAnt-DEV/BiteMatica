#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/BlockPos.hpp"
#include "core/Schematic.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::world {

/// Two-corner region selection and capture.
///
/// The selection tool marks a first and second corner (from a hit-result or a ray-cast, both of
/// which the mod derives from the player's position and view direction), then captures every block
/// in the box into a Schematic that can be saved as a `.litematic`. This is how a player saves a
/// region they built in Bedrock and shares it.
class Selection {
public:
  void setCornerA(const core::BlockPos &position) {
    mA = position;
    mHasA = true;
  }
  void setCornerB(const core::BlockPos &position) {
    mB = position;
    mHasB = true;
  }
  bool complete() const { return mHasA && mHasB; }
  void clear() {
    mHasA = false;
    mHasB = false;
  }
  const core::BlockPos &cornerA() const { return mA; }
  const core::BlockPos &cornerB() const { return mB; }

  /// The inclusive bounds of the selection, normalized so min <= max.
  void bounds(core::BlockPos &min, core::BlockPos &max) const {
    min = core::BlockPos{std::min(mA.x, mB.x), std::min(mA.y, mB.y), std::min(mA.z, mB.z)};
    max = core::BlockPos{std::max(mA.x, mB.x), std::max(mA.y, mB.y), std::max(mA.z, mB.z)};
  }

  /// The selection's volume in blocks.
  long long volume() const {
    if (!complete()) return 0;
    core::BlockPos min;
    core::BlockPos max;
    bounds(min, max);
    return static_cast<long long>(max.x - min.x + 1) * (max.y - min.y + 1) * (max.z - min.z + 1);
  }

  /// Captures the selection from the world into a Schematic.
  ///
  /// Returns false with `error` when the selection is incomplete, exceeds `maxBlocks`, or the world
  /// is not readable. Every captured block goes through the Java->Bedrock translation on the way
  /// out only if `translate` is set; capture from a Bedrock world is already Bedrock-native.
  bool capture(WorldSource &source, const std::string &name, long long maxBlocks,
               core::Schematic &out, std::string &error) const;

private:
  core::BlockPos mA{};
  core::BlockPos mB{};
  bool mHasA{false};
  bool mHasB{false};
};

} // namespace bitematica::world
