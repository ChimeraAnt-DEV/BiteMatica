#pragma once

#include <string>

#include "core/BlockPos.hpp"
#include "core/Schematic.hpp"

namespace bitematica::world {

/// A 90-degree rotation about the vertical axis.
enum class Rotation : int { None = 0, Cw90 = 1, Cw180 = 2, Cw270 = 3 };

/// A horizontal mirror applied before rotation.
enum class Mirror : int { None = 0, X = 1, Z = 2 };

/// A schematic anchored somewhere in the world.
///
/// The placement is the bridge between "a box of blocks" and "blocks at these world coordinates".
/// It is deliberately a value type with no game dependency, so the transform (rotate/mirror/Y
/// offset) and the world<->local mapping are unit-testable on a host — which matters, because a
/// rotation applied in the wrong order puts a build in the wrong place and is painful to debug on a
/// device.
///
/// The transform is applied in a fixed order: mirror, then rotation, then translation. That order is
/// part of the contract and is what the tests pin.
struct Placement {
  std::string name{"default"};
  /// World position of the schematic's minimum corner after rotation/mirror are applied.
  core::BlockPos origin{};
  Rotation rotation{Rotation::None};
  Mirror mirror{Mirror::None};
  /// Vertical offset applied after rotation, for lining a build up with the ground.
  int yOffset{};
  bool enabled{true};

  /// The size of the bounding box after the rotation swaps X and Z.
  static void rotatedSize(int sizeX, int sizeY, int sizeZ, Rotation rotation, int &outX,
                          int &outY, int &outZ) {
    outY = sizeY;
    if (rotation == Rotation::Cw90 || rotation == Rotation::Cw270) {
      outX = sizeZ;
      outZ = sizeX;
    } else {
      outX = sizeX;
      outZ = sizeZ;
    }
  }

  /// Maps a schematic-local block position to a world position under this placement.
  core::BlockPos toWorld(const core::Schematic &schematic, int x, int y, int z) const {
    int sx = x;
    int sz = z;
    const int sizeX = schematic.sizeX();
    const int sizeZ = schematic.sizeZ();

    // Mirror first, in local space.
    if (mirror == Mirror::X) sx = sizeX - 1 - sx;
    if (mirror == Mirror::Z) sz = sizeZ - 1 - sz;

    // Then rotate about the local origin.
    int rx = sx;
    int rz = sz;
    switch (rotation) {
      case Rotation::None:
        break;
      case Rotation::Cw90:
        // (x, z) -> (z, sizeX - 1 - x).
        rx = sz;
        rz = sizeX - 1 - sx;
        break;
      case Rotation::Cw180:
        rx = sizeX - 1 - sx;
        rz = sizeZ - 1 - sz;
        break;
      case Rotation::Cw270:
        rx = sizeZ - 1 - sz;
        rz = sx;
        break;
    }

    return core::BlockPos{origin.x + rx, origin.y + y + yOffset, origin.z + rz};
  }

  /// Maps a world position back to a schematic-local grid position.
  ///
  /// Returns false when the world position is outside the placement's box. This is the inverse of
  /// {@link toWorld}; the tests check the round trip for every rotation/mirror pair.
  bool toLocal(const core::Schematic &schematic, const core::BlockPos &world,
               core::LocalPos &out) const {
    int rx = world.x - origin.x;
    int ry = world.y - origin.y - yOffset;
    int rz = world.z - origin.z;

    const int sizeX = schematic.sizeX();
    const int sizeZ = schematic.sizeZ();
    int extentX = sizeX;
    int extentZ = sizeZ;
    if (rotation == Rotation::Cw90 || rotation == Rotation::Cw270) {
      extentX = sizeZ;
      extentZ = sizeX;
    }
    if (ry < 0 || ry >= schematic.sizeY()) return false;
    if (rx < 0 || rx >= extentX || rz < 0 || rz >= extentZ) return false;

    // Undo rotation.
    int sx = rx;
    int sz = rz;
    switch (rotation) {
      case Rotation::None:
        break;
      case Rotation::Cw90:
        sx = sizeX - 1 - rz;
        sz = rx;
        break;
      case Rotation::Cw180:
        sx = sizeX - 1 - rx;
        sz = sizeZ - 1 - rz;
        break;
      case Rotation::Cw270:
        sx = rz;
        sz = sizeZ - 1 - rx;
        break;
    }
    // Undo mirror.
    if (mirror == Mirror::X) sx = sizeX - 1 - sx;
    if (mirror == Mirror::Z) sz = sizeZ - 1 - sz;

    if (sx < 0 || sx >= sizeX || sz < 0 || sz >= sizeZ) return false;
    out = core::LocalPos{sx, ry, sz};
    return true;
  }

  /// The placement's world-space bounding box, inclusive.
  void worldBounds(const core::Schematic &schematic, core::BlockPos &min,
                   core::BlockPos &max) const {
    int ex = 0;
    int ey = 0;
    int ez = 0;
    rotatedSize(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ(), rotation, ex, ey, ez);
    min = core::BlockPos{origin.x, origin.y + yOffset, origin.z};
    max = core::BlockPos{origin.x + ex - 1, origin.y + yOffset + ey - 1, origin.z + ez - 1};
  }

  /// Advances the rotation by one 90-degree step.
  void rotateCw() { rotation = static_cast<Rotation>((static_cast<int>(rotation) + 1) % 4); }
  void rotateCcw() { rotation = static_cast<Rotation>((static_cast<int>(rotation) + 3) % 4); }
  /// Cycles the mirror through none -> X -> Z -> none.
  void cycleMirror() { mirror = static_cast<Mirror>((static_cast<int>(mirror) + 1) % 3); }

  void move(int dx, int dy, int dz) {
    origin.x += dx;
    origin.y += dy;
    origin.z += dz;
  }

  /// True when the world position is inside this placement's box.
  bool contains(const core::Schematic &schematic, const core::BlockPos &world) const {
    core::LocalPos local{};
    return toLocal(schematic, world, local);
  }
};

} // namespace bitematica::world
