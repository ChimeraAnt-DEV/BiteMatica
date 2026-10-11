#pragma once

#include <cmath>

#include "core/BlockPos.hpp"
#include "litematica/BlockTargeting.hpp"
#include "render/MathTypes.hpp"

namespace bitematica::litematica {

/// The screen-relative nudge axes (D5): the six directions a player can predict from where they are
/// looking, snapped to the nearest world axis.
///
/// "Forward" is the horizontal component of the view direction snapped to ±X or ±Z (whichever the
/// player is more aligned with); "right" is 90 degrees clockwise from it; "up" is always +Y. This is
/// the only nudge scheme a user can reason about without a gizmo, so the placement controls use it.
struct NudgeAxes {
  core::BlockPos forward{0, 0, -1};
  core::BlockPos right{1, 0, 0};
  core::BlockPos up{0, 1, 0};

  /// Builds the axes from a view direction (need not be normalized).
  static NudgeAxes fromView(const render::Vec3 &direction) {
    NudgeAxes axes;
    // Snap the horizontal view direction to the dominant world axis.
    const float ax = std::fabs(direction.x);
    const float az = std::fabs(direction.z);
    if (ax >= az) {
      axes.forward = core::BlockPos{direction.x >= 0 ? 1 : -1, 0, 0};
      // Right is 90 degrees clockwise from forward (viewed from above).
      axes.right = core::BlockPos{0, 0, direction.x >= 0 ? 1 : -1};
    } else {
      axes.forward = core::BlockPos{0, 0, direction.z >= 0 ? 1 : -1};
      axes.right = core::BlockPos{direction.z >= 0 ? -1 : 1, 0, 0};
    }
    return axes;
  }

  /// The delta for a named nudge direction.
  core::BlockPos delta(int direction, int steps) const {
    const core::BlockPos axis = direction == 0   ? forward
                                : direction == 1 ? core::BlockPos{-right.x, -right.y, -right.z}
                                : direction == 2 ? right
                                : direction == 3 ? core::BlockPos{-forward.x, -forward.y, -forward.z}
                                : direction == 4 ? up
                                                 : core::BlockPos{0, -1, 0};
    return core::BlockPos{axis.x * steps, axis.y * steps, axis.z * steps};
  }
};

/// "Set origin here" (D4): where a placement's origin should land, given the targeted block and a
/// user choice of placement mode.
enum class OriginPlacement {
  OnTopOfBlock,  ///< the origin sits on the block's upper face (the usual "build up from here")
  InsideBlock,   ///< the origin is the targeted block itself
  AtFeet,        ///< the origin is at the player's feet, floored
};

/// The origin for a targeted block under a placement mode.
inline core::BlockPos originForTarget(const RayHit &hit, OriginPlacement mode) {
  switch (mode) {
    case OriginPlacement::OnTopOfBlock:
      // The empty block directly above the targeted block, so the build rises from its top face.
      return core::BlockPos{hit.block.x, hit.block.y + 1, hit.block.z};
    case OriginPlacement::InsideBlock:
      return hit.block;
    case OriginPlacement::AtFeet:
      // The block beside the hit face, at the player's level: what `placementAnchor` gives.
      return core::BlockPos{hit.block.x + hit.normal.x, hit.block.y + hit.normal.y,
                            hit.block.z + hit.normal.z};
  }
  return hit.block;
}

/// The origin at the player's feet.
inline core::BlockPos originAtFeet(const render::Vec3 &feetPosition) {
  return core::BlockPos{static_cast<int>(std::floor(feetPosition.x)),
                        static_cast<int>(std::floor(feetPosition.y)),
                        static_cast<int>(std::floor(feetPosition.z))};
}

} // namespace bitematica::litematica
