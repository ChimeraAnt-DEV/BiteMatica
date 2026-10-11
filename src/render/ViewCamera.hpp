#pragma once

#include <cmath>

#include "core/Schematic.hpp"

namespace bitematica::render {

/// A screen-space point produced by projecting a block coordinate.
struct ScreenPoint {
  float x{};
  float y{};
  float depth{}; // Larger is farther from the camera; used for painter sorting.
};

/// The camera used to project a schematic into the 2D HUD.
///
/// The HUD only accepts flat draw commands, so the "3D" view is an isometric-style
/// rotation computed here and drawn as polygons. The camera keeps yaw, pitch and a
/// zoom, plus a world offset so the schematic can be parked in front of the player.
/// Everything is pure arithmetic, so the projection is unit-testable with no renderer.
class ViewCamera {
public:
  float yawDegrees{45.0f};
  float pitchDegrees{-30.0f};
  /// Pixels per block at the current zoom level.
  float scale{24.0f};
  /// Screen position the schematic's center is projected around, in HUD pixels.
  float centerX{};
  float centerY{};
  /// Offset of the schematic's center from the camera focus, in block units.
  float panX{};
  float panY{};
  float panZ{};

  /// Projects a schematic-local block position (block corner) to screen space.
  ///
  /// `schematic` supplies the centering box so a build rotates about its own middle.
  ScreenPoint project(const core::Schematic &schematic, float x, float y, float z) const {
    const float cx = schematic.sizeX() * 0.5f;
    const float cy = schematic.sizeY() * 0.5f;
    const float cz = schematic.sizeZ() * 0.5f;
    return projectCentered(x - cx + panX, y - cy + panY, z - cz + panZ);
  }

  /// Projects a point already expressed relative to the schematic center.
  ScreenPoint projectCentered(float x, float y, float z) const {
    const float yaw = yawDegrees * 3.14159265358979f / 180.0f;
    const float pitch = pitchDegrees * 3.14159265358979f / 180.0f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);

    // Yaw around the vertical axis, then pitch around the screen-horizontal axis.
    const float rx = x * cy - z * sy;
    const float rz = x * sy + z * cy;
    const float ry = y * cp - rz * sp;
    const float depth = y * sp + rz * cp;

    ScreenPoint point;
    point.x = centerX + rx * scale;
    point.y = centerY - ry * scale;
    point.depth = depth;
    return point;
  }

  /// The projected length of one block along each screen axis, for sizing draws.
  float blockSize() const { return scale; }

  /// Projects a direction (not a point) and returns only its depth component.
  ///
  /// A face whose outward normal has a negative depth points toward the camera and is
  /// the one worth drawing; the others are on the far side of the block and would only
  /// be overdrawn. This is the face-culling test, kept here so it uses the same
  /// rotation as `project`.
  float depthOfDirection(float x, float y, float z) const {
    const float yaw = yawDegrees * 3.14159265358979f / 180.0f;
    const float pitch = pitchDegrees * 3.14159265358979f / 180.0f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float rz = x * sy + z * cy;
    return y * sp + rz * cp;
  }
};

} // namespace bitematica::render
