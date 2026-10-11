#pragma once

#include <cstdint>

#include "render/MathTypes.hpp"

namespace bitematica::render {

/// The live camera for one frame, in world space.
struct CameraFrame {
  Mat4 viewProjection{};
  Mat4 view{};
  Mat4 projection{};
  Vec3 position{};
  Vec3 forward{};
  int viewportWidth{};
  int viewportHeight{};
  bool valid{false};
};

/// Finds and reads the game's live camera matrices.
///
/// The preloader's per-frame render callback is gated to specific game builds, so it cannot be the
/// foundation for a mod that has to run on every 1.26 build. The camera, however, is just data: the
/// game keeps its view and projection matrices in its own writable memory. This scans for a
/// perspective projection matrix (the characteristic 1/tan(fov) diagonal and -1 in m[11]) that is
/// immediately preceded by a rigid view matrix, caches the address once, and then reads it cheaply
/// each frame.
///
/// Everything a placement needs is derived from the matrices themselves — the eye position is the
/// inverse translation of the view matrix and the forward vector is its third row — so no
/// game-build-specific player accessor is required either. That is what makes world-space rendering
/// work on 1.26 through 1.26.52 and beyond.
class GameCamera {
public:
  /// Runs the one-time scan. Safe to call from any thread; only the first call scans.
  static void resolveOnce();

  /// True when the scan found a camera block.
  static bool resolved();

  /// Reads and validates the camera for this frame. Cheap; safe on the render thread.
  static CameraFrame read();

  /// Sets the GL viewport size the frame reports (read separately, since it is GL state).
  static void setViewport(int width, int height);
};

} // namespace bitematica::render
