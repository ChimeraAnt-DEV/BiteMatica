#include "translate/BlockStateTransform.hpp"

#include <algorithm>
#include <cstdlib>

namespace bitematica::translate {
namespace {

/// True when the state has a key.
bool has(const core::BlockState &state, const std::string &key) {
  for (const auto &[k, v] : state.states) {
    if (k == key) return true;
  }
  return false;
}

std::string get(const core::BlockState &state, const std::string &key) {
  for (const auto &[k, v] : state.states) {
    if (k == key) return v;
  }
  return {};
}

void set(core::BlockState &state, const std::string &key, const std::string &value) {
  for (auto &[k, v] : state.states) {
    if (k == key) {
      v = value;
      return;
    }
  }
  state.states.emplace_back(key, value);
}

/// A horizontal direction, 0..3, with a fixed meaning. Different Bedrock states use different
/// numberings, so each family is converted to this canonical order, rotated, and converted back.
/// Canonical: 0=north, 1=east, 2=south, 3=west (clockwise seen from above).
int rotateCanonical(int dir, world::Rotation rotation) {
  const int steps = static_cast<int>(rotation); // 0..3 clockwise quarter turns
  return ((dir + steps) % 4 + 4) % 4;
}

/// Mirrors a canonical direction across the X axis (flips east<->west).
int mirrorAcrossX(int dir) {
  // north<->north, south<->south, east<->west.
  if (dir == 1) return 3;
  if (dir == 3) return 1;
  return dir;
}

/// Mirrors a canonical direction across the Z axis (flips north<->south).
int mirrorAcrossZ(int dir) {
  if (dir == 0) return 2;
  if (dir == 2) return 0;
  return dir;
}

} // namespace

int BlockStateTransform::rotateWeirdoDirection(int direction, world::Rotation rotation) {
  // Bedrock weirdo_direction: 0=east, 1=west, 2=south, 3=north.
  // Convert to canonical (north=0, east=1, south=2, west=3).
  int canonical = 0;
  switch (direction) {
    case 0: canonical = 1; break; // east
    case 1: canonical = 3; break; // west
    case 2: canonical = 2; break; // south
    case 3: default: canonical = 0; break; // north
  }
  canonical = rotateCanonical(canonical, rotation);
  switch (canonical) {
    case 0: return 3; // north
    case 1: return 0; // east
    case 2: return 2; // south
    case 3: default: return 1; // west
  }
}

int BlockStateTransform::rotateDirection(int direction, world::Rotation rotation) {
  // Bedrock `direction`: 0=east, 1=south, 2=west, 3=north.
  int canonical = 0;
  switch (direction) {
    case 0: canonical = 1; break; // east
    case 1: canonical = 2; break; // south
    case 2: canonical = 3; break; // west
    case 3: default: canonical = 0; break; // north
  }
  canonical = rotateCanonical(canonical, rotation);
  switch (canonical) {
    case 0: return 3; // north
    case 1: return 0; // east
    case 2: return 1; // south
    case 3: default: return 2; // west
  }
}

std::string BlockStateTransform::rotateAxis(const std::string &axis, world::Rotation rotation) {
  // A quarter turn about Y swaps the X and Z axes; a half turn leaves them.
  const int steps = static_cast<int>(rotation);
  if (steps % 2 == 0) return axis;
  if (axis == "x") return "z";
  if (axis == "z") return "x";
  return axis; // y is unaffected by a Y rotation
}

core::BlockState BlockStateTransform::transform(const core::BlockState &state,
                                                world::Rotation rotation, world::Mirror mirror) {
  core::BlockState out = state;
  const int steps = static_cast<int>(rotation);

  // --- rotation ---
  if (steps != 0) {
    if (has(out, "weirdo_direction")) {
      const int dir = std::atoi(get(out, "weirdo_direction").c_str());
      set(out, "weirdo_direction", std::to_string(rotateWeirdoDirection(dir, rotation)));
    }
    if (has(out, "direction")) {
      const int dir = std::atoi(get(out, "direction").c_str());
      set(out, "direction", std::to_string(rotateDirection(dir, rotation)));
    }
    if (has(out, "axis")) {
      set(out, "axis", rotateAxis(get(out, "axis"), rotation));
    }
    // Stair shape (inner/outer corners) rotates with the facing; the common straight shape is
    // unchanged, so only the corner variants are touched.
    if (has(out, "torch_facing_direction")) {
      // Wall torch: a rotation turns which wall it is on.
      const std::string facing = get(out, "torch_facing_direction");
      if (facing == "east") set(out, "torch_facing_direction", steps % 2 ? "north" : "west");
      else if (facing == "south") set(out, "torch_facing_direction", steps % 2 ? "east" : "north");
      else if (facing == "west") set(out, "torch_facing_direction", steps % 2 ? "south" : "east");
      else if (facing == "north") set(out, "torch_facing_direction", steps % 2 ? "west" : "south");
    }
    if (has(out, "ground_sign_direction")) {
      // Signs / banners: 0..15 rotation values; a quarter turn is +4.
      const int dir = std::atoi(get(out, "ground_sign_direction").c_str());
      set(out, "ground_sign_direction", std::to_string(((dir + steps * 4) % 16 + 16) % 16));
    }
    if (has(out, "rail_direction")) {
      const int dir = std::atoi(get(out, "rail_direction").c_str());
      set(out, "rail_direction", std::to_string(rotateDirection(dir, rotation)));
    }
  }

  // --- mirror ---
  if (mirror != world::Mirror::None) {
    // A mirror across X flips east<->west; across Z flips north<->south.
    auto flipCanonical = [&](int canonical) {
      return mirror == world::Mirror::X ? mirrorAcrossX(canonical) : mirrorAcrossZ(canonical);
    };
    if (has(out, "weirdo_direction")) {
      const int dir = std::atoi(get(out, "weirdo_direction").c_str());
      int canonical = dir == 0 ? 1 : (dir == 1 ? 3 : (dir == 2 ? 2 : 0));
      canonical = flipCanonical(canonical);
      set(out, "weirdo_direction",
          std::to_string(canonical == 0 ? 3 : (canonical == 1 ? 0 : (canonical == 2 ? 2 : 1))));
    }
    if (has(out, "direction")) {
      const int dir = std::atoi(get(out, "direction").c_str());
      int canonical = dir == 0 ? 1 : (dir == 1 ? 2 : (dir == 2 ? 3 : 0));
      canonical = flipCanonical(canonical);
      set(out, "direction",
          std::to_string(canonical == 0 ? 3 : (canonical == 1 ? 0 : (canonical == 2 ? 1 : 2))));
    }
    if (has(out, "torch_facing_direction")) {
      const std::string facing = get(out, "torch_facing_direction");
      if (mirror == world::Mirror::X) {
        if (facing == "east") set(out, "torch_facing_direction", "west");
        else if (facing == "west") set(out, "torch_facing_direction", "east");
      } else {
        if (facing == "north") set(out, "torch_facing_direction", "south");
        else if (facing == "south") set(out, "torch_facing_direction", "north");
      }
    }
    if (has(out, "hinge_bit")) {
      // A door's hinge side flips under a mirror.
      set(out, "hinge_bit", get(out, "hinge_bit") == "1" ? "0" : "1");
    }
    if (has(out, "ground_sign_direction")) {
      // Mirroring a 16-way rotation: reflect across the chosen axis.
      const int dir = std::atoi(get(out, "ground_sign_direction").c_str());
      const int mirrored = mirror == world::Mirror::X ? ((16 - dir) % 16) : ((8 - dir + 16) % 16);
      set(out, "ground_sign_direction", std::to_string((mirrored + 16) % 16));
    }
  }

  return out;
}

} // namespace bitematica::translate
