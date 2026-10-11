#pragma once

#include <cmath>

#include "core/BlockPos.hpp"
#include "render/MathTypes.hpp"

namespace bitematica::litematica {

/// Ray-casts from the camera to find the block under the crosshair.
///
/// Litematica's tool modes all act on "the block you are looking at", so this is the shared
/// primitive: a voxel DDA (Amanatides-Woo) walk from the camera position along the view direction,
/// stopping at the first block the world reports as solid. It is pure arithmetic over a
/// `isSolid(x,y,z)` predicate, so it is host-testable with a synthetic world and needs no game hook.
///
/// The walk is bounded by `maxDistance` so a look into empty sky terminates.
struct RayHit {
  bool hit{false};
  core::BlockPos block{};
  /// The face that was entered, as a normal.
  core::BlockPos normal{};
  float distance{};
};

class BlockTargeting {
public:
  /// Casts a ray and returns the first solid block.
  ///
  /// @param origin camera position in world space
  /// @param direction view direction (need not be normalized)
  /// @param maxDistance how far to search, in blocks
  /// @param isSolid a predicate returning true for a solid world block
  template <typename IsSolid>
  static RayHit cast(const render::Vec3 &origin, const render::Vec3 &direction, float maxDistance,
                     IsSolid isSolid) {
    RayHit hit;
    const render::Vec3 dir = render::normalize(direction);
    if (render::length(dir) < 0.5f) return hit; // zero direction

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = dir.x > 0 ? 1 : (dir.x < 0 ? -1 : 0);
    const int stepY = dir.y > 0 ? 1 : (dir.y < 0 ? -1 : 0);
    const int stepZ = dir.z > 0 ? 1 : (dir.z < 0 ? -1 : 0);

    // Distance along the ray to the next boundary on each axis.
    auto tDelta = [](float d) { return d == 0.0f ? 1e30f : std::fabs(1.0f / d); };
    const float tDeltaX = tDelta(dir.x);
    const float tDeltaY = tDelta(dir.y);
    const float tDeltaZ = tDelta(dir.z);

    // Distance to the first boundary on each axis.
    float tMaxX = stepX == 0 ? 1e30f
                             : ((stepX > 0 ? (x + 1 - origin.x) : (origin.x - x)) / std::fabs(dir.x));
    float tMaxY = stepY == 0 ? 1e30f
                             : ((stepY > 0 ? (y + 1 - origin.y) : (origin.y - y)) / std::fabs(dir.y));
    float tMaxZ = stepZ == 0 ? 1e30f
                             : ((stepZ > 0 ? (z + 1 - origin.z) : (origin.z - z)) / std::fabs(dir.z));

    int lastAxis = -1;
    float travelled = 0.0f;
    while (travelled <= maxDistance) {
      if (isSolid(x, y, z)) {
        hit.hit = true;
        hit.block = core::BlockPos{x, y, z};
        hit.distance = travelled;
        switch (lastAxis) {
          case 0: hit.normal = core::BlockPos{-stepX, 0, 0}; break;
          case 1: hit.normal = core::BlockPos{0, -stepY, 0}; break;
          case 2: hit.normal = core::BlockPos{0, 0, -stepZ}; break;
          default: hit.normal = core::BlockPos{}; break;
        }
        return hit;
      }
      // Step to the nearest boundary.
      if (tMaxX < tMaxY && tMaxX < tMaxZ) {
        x += stepX;
        travelled = tMaxX;
        tMaxX += tDeltaX;
        lastAxis = 0;
      } else if (tMaxY < tMaxZ) {
        y += stepY;
        travelled = tMaxY;
        tMaxY += tDeltaY;
        lastAxis = 1;
      } else {
        z += stepZ;
        travelled = tMaxZ;
        tMaxZ += tDeltaZ;
        lastAxis = 2;
      }
    }
    return hit;
  }

  /// The block a placement's origin should snap to for "place here": the block adjacent to the hit
  /// face, which is where a new build would start.
  static core::BlockPos placementAnchor(const RayHit &hit) {
    return core::BlockPos{hit.block.x + hit.normal.x, hit.block.y + hit.normal.y,
                          hit.block.z + hit.normal.z};
  }
};

} // namespace bitematica::litematica
