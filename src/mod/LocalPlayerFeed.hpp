#pragma once

#include <cstdint>

#include "render/MathTypes.hpp"

namespace bitematica::mod {

/// Reads the local player's position and view rotation directly from the game.
///
/// This is the mechanism the launcher's own preloader uses for its in-world features, and it is the
/// one that actually works across the 1.26 builds: it resolves `ClientInstance`'s local-player
/// vtable slot **by RTTI name** (so no per-build code address) and reads two fields off the returned
/// `Actor` — position at `+0x230` and view rotation at `+0x238`. Those offsets are shared by
/// 1.26.33 / 1.26.50 / 1.26.51, the exact range this mod targets.
///
/// It replaces the earlier approach of scanning the whole game module for a matrix, which was slow
/// and speculative. A hook on the accessor the game itself calls every frame is both cheaper and
/// grounded in what the game actually does.
///
/// Fail-closed: if the slot does not resolve, or a value is not finite, the feed reports "no data"
/// and the caller draws nothing rather than guessing a position.
class LocalPlayerFeed {
public:
  /// Installs the hook. Idempotent. Returns true when the feed is live.
  static bool install();

  /// True once a live position has been seen (within the freshness window).
  static bool available();

  /// Reads the latest position. False when no live player has been seen.
  static bool readPosition(float out[3]);

  /// Reads the latest view rotation in degrees: yaw then pitch.
  static bool readRotation(float out[2]);

  /// A one-line diagnostic for the on-screen panel.
  static const char *diagnostic();

  /// The Actor field offsets, exposed so a test can assert the known values.
  static std::uintptr_t positionOffset();
  static std::uintptr_t rotationOffset();

  /// Builds the camera from the live player when no real camera is available.
  ///
  /// This is the reliable fallback: the eye is the player's head and the view direction comes from
  /// the player's own yaw/pitch, so it is correct in first person and close in third. It needs only
  /// the feed above, not any SDK matrix API, so it works on every build the feed works on.
  ///
  /// `outView` and `outProjection` are provided separately as well as combined, because the renderer
  /// needs them apart to build the camera-relative clip matrix that stays exact at world coordinate
  /// ±3e7 (see `render::cameraRelativeViewProjection`).
  static bool buildLocalCamera(float fovDegrees, float aspectRatio, float nearPlane, float farPlane,
                               render::Mat4 &outViewProjection, render::Vec3 &outEye,
                               render::Vec3 &outForward, render::Mat4 &outView,
                               render::Mat4 &outProjection);
};

} // namespace bitematica::mod
