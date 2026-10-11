#pragma once

#include "render/MathTypes.hpp"

namespace bitematica::render {

/// The matrices a placement draw needs, kept separate so they cannot be confused.
///
/// The transform chain is applied in **exactly one place**, and this type is where it is decided:
///
///   block local --(mirror)--> --(rotation about placement origin)--> --(+ placementOrigin)-->
///   --(− cameraPosition, in double, then float)--> viewProjectionCameraRelative
///
/// The shader applies mirror + rotation + placement origin (its vertex shader does the yaw and
/// translation; mirror is baked into the mesh). The CPU applies only the camera-relative subtraction,
/// which is the part that must be in double precision. So `mvp` is the **camera-relative** clip
/// matrix and `cameraRelativeOrigin` is the placement origin measured from the camera; the shader
/// computes `mvp * vec4(rotatedLocal + cameraRelativeOrigin, 1)`. The placement transform is applied
/// once (in the shader) and the camera subtraction is applied once (on the CPU) — never both twice.
///
/// `frustum` and `localCamera` are in placement-local space and are used only for culling, where
/// section boxes are axis-aligned regardless of the placement's rotation.
struct PlacementCull {
  /// Camera-relative world -> clip, uploaded as `uMvp`. Includes neither the placement transform nor
  /// a world-space translation.
  Mat4 mvp{};
  /// The placement origin measured from the camera, in float, computed from doubles.
  Vec3 cameraRelativeOrigin{};
  /// Extracted from `mvp * localToWorld`; section boxes are axis-aligned in this space.
  Frustum frustum{};
  /// The camera expressed in placement-local space, for the distance cull.
  Vec3 localCamera{};
};

/// Builds the mvp/origin/frustum/camera quadruple for a placement.
///
/// @param projection the view -> clip matrix (needed for the camera-relative mvp)
/// @param view the world -> view matrix (its rotation is what the camera-relative mvp keeps)
/// @param origin the placement's world origin (float is fine for a schematic-sized box)
/// @param yawDegrees the placement's yaw (0/90/180/270)
/// @param worldCamera the camera in world space
inline PlacementCull computePlacementCull(const Mat4 &projection, const Mat4 &view,
                                          const Vec3 &origin, float yawDegrees,
                                          const Vec3 &worldCamera) {
  PlacementCull cull;
  // Camera-relative clip matrix: consumes (world − camera), no large translation, so it is exact at
  // world coordinate ±3e7. The placement transform is NOT folded in here (the shader owns it).
  cull.mvp = cameraRelativeViewProjection(projection, view);
  // The placement origin relative to the camera, in double precision then narrowed to float. The
  // result is schematic-sized, so the narrowing is exact to float precision.
  cull.cameraRelativeOrigin = {
      static_cast<float>(static_cast<double>(origin.x) - static_cast<double>(worldCamera.x)),
      static_cast<float>(static_cast<double>(origin.y) - static_cast<double>(worldCamera.y)),
      static_cast<float>(static_cast<double>(origin.z) - static_cast<double>(worldCamera.z))};

  // The placement-local transform is relative to the placement origin, so it composes with the
  // camera-relative mvp without a world translation.
  const Mat4 localToWorld = placementLocalToWorld(cull.cameraRelativeOrigin, yawDegrees);
  const Mat4 localViewProjection = Mat4::multiply(cull.mvp, localToWorld);
  cull.frustum = Frustum::fromViewProjection(localViewProjection);
  cull.localCamera = worldToPlacementLocal(origin, yawDegrees, worldCamera);
  return cull;
}

/// Convenience overload for callers that only have the combined view-projection (world-space).
///
/// The combined matrix is world->clip, so `projection`/`view` are not separable from it; this builds
/// the placement-local frustum from `viewProjection * localToWorld` (valid, because the composition
/// is world->local->clip) and leaves the mvp as the plain world->clip matrix with the world origin.
/// Use it only when the separate matrices are unavailable; the primary path is the overload above.
inline PlacementCull computePlacementCull(const Mat4 &viewProjection, const Vec3 &origin,
                                          float yawDegrees, const Vec3 &worldCamera) {
  PlacementCull cull;
  cull.mvp = viewProjection;
  cull.cameraRelativeOrigin = origin;
  const Mat4 localToWorld = placementLocalToWorld(origin, yawDegrees);
  cull.frustum = Frustum::fromViewProjection(Mat4::multiply(viewProjection, localToWorld));
  cull.localCamera = worldToPlacementLocal(origin, yawDegrees, worldCamera);
  return cull;
}

} // namespace bitematica::render
