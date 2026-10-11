#pragma once

#include <cstdint>
#include <functional>

namespace bitematica::core {

/// Integer block coordinate. Matches Bedrock's signed 32-bit block positions.
struct BlockPos {
  int x{};
  int y{};
  int z{};

  constexpr BlockPos() = default;
  constexpr BlockPos(int px, int py, int pz) : x(px), y(py), z(pz) {}

  constexpr BlockPos operator+(const BlockPos &other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  constexpr BlockPos operator-(const BlockPos &other) const {
    return {x - other.x, y - other.y, z - other.z};
  }
  friend constexpr bool operator==(const BlockPos &a, const BlockPos &b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
  }
};

/// A position inside a schematic's local, origin-relative grid.
struct LocalPos {
  int x{};
  int y{};
  int z{};
};

/// An axis-aligned region in schematic-local space, expressed as inclusive bounds.
struct Bounds {
  LocalPos min{};
  LocalPos max{};

  int sizeX() const { return max.x - min.x + 1; }
  int sizeY() const { return max.y - min.y + 1; }
  int sizeZ() const { return max.z - min.z + 1; }
  long long volume() const {
    return static_cast<long long>(sizeX()) * sizeY() * sizeZ();
  }
  bool contains(const LocalPos &p) const {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
  }
};

} // namespace bitematica::core
