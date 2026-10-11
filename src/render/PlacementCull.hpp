#pragma once

#include "render/MathTypes.hpp"

namespace bitematica::render {

/// The matrices a placement draw needs, kept separate so they cannot be confused.
///
/// This distinction is the whole point of the type: `mvp` is the *world*->clip matrix the shader
/// multiplies by, and the shader applies the placement's rotation and translation itself. `frustum`
/// and `localCamera` are in placement-local space and are used only for culling. Applying the
/// placement transform to `mvp` as well would transform every vertex twice — the ghost would sit at
/// roughly double the offset with doubled rotation.
struct PlacementCull {
  /// World -> clip. Uploaded as `uMvp`. Must NOT include the placement transform.
  Mat4 mvp{};
  /// Extracted from `viewProjection * localToWorld`; section boxes are axis-aligned in this space.
  Frustum frustum{};
  /// The camera expressed in placement-local space, for the distance cull.
  Vec3 localCamera{};
};

/// Builds the mvp/frustum/camera triple for a placement.
///
/// @param viewProjection the plain world -> clip matrix from the render context
/// @param origin the placement's world origin
/// @param yawDegrees the placement's yaw (0/90/180/270)
/// @param worldCamera the camera in world space
inline PlacementCull computePlacementCull(const Mat4 &viewProjection, const Vec3 &origin,
                                          float yawDegrees, const Vec3 &worldCamera) {
  PlacementCull cull;
  // The shader owns the placement transform, so the mvp stays the plain view-projection.
  cull.mvp = viewProjection;

  const Mat4 localToWorld = placementLocalToWorld(origin, yawDegrees);
  const Mat4 localViewProjection = Mat4::multiply(viewProjection, localToWorld);
  cull.frustum = Frustum::fromViewProjection(localViewProjection);
  cull.localCamera = worldToPlacementLocal(origin, yawDegrees, worldCamera);
  return cull;
}

} // namespace bitematica::render
