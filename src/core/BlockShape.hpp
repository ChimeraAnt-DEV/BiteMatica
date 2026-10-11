#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "core/BlockState.hpp"

namespace bitematica::core {

/// An axis-aligned box in block-local space, coordinates in [0,1].
struct ShapeBox {
  float minX{}, minY{}, minZ{};
  float maxX{1.0f}, maxY{1.0f}, maxZ{1.0f};
};

/// The geometry of a block as a small list of axis-aligned boxes.
///
/// Bedrock's non-cube blocks (slabs, stairs, fences, panes, doors, ...) are, from the renderer's
/// point of view, just a handful of boxes. Modelling them here — as data derived from the block's
/// state — is what makes a schematic of stairs read as stairs instead of a full cube, which is the
/// difference between a useful ghost and a wall of boxes (Phase C3).
///
/// The table covers the common families the brief lists. Anything not covered is a **full cube**,
/// with `isCube()` true, so an unknown shape never silently becomes something wrong — it becomes the
/// block's own bounding box, which is what a schematic viewer shows for an unmodelled shape.
///
/// Pure and header-only, so the shape decisions are host-testable without a GPU.
class BlockShape {
public:
  /// The boxes for a block state, in block-local [0,1] space.
  static std::vector<ShapeBox> boxesFor(const BlockState &state) {
    const std::string name = baseName(state);
    const auto &stateMap = state.states;

    // Slabs: bottom or top half.
    if (endsWith(name, "_slab")) {
      const bool top = valueOf(stateMap, "top_slot_bit") == "1" ||
                       valueOf(stateMap, "half") == "top" ||
                       valueOf(stateMap, "vertical_half") == "top";
      return top ? std::vector<ShapeBox>{{0, 0.5f, 0, 1, 1, 1}}
                 : std::vector<ShapeBox>{{0, 0, 0, 1, 0.5f, 1}};
    }

    // Carpets / moss carpet / snow layers: a thin plate.
    if (name == "minecraft:moss_carpet" || endsWith(name, "_carpet")) {
      return {{0, 0, 0, 1, 0.0625f, 1}};
    }
    if (name == "minecraft:snow_layer") {
      const int layers = clampInt(valueOf(stateMap, "height"), 0, 8, 1);
      const float h = 0.125f * static_cast<float>(layers == 0 ? 1 : layers);
      return {{0, 0, 0, 1, h, 1}};
    }

    // Pressure plates and buttons: thin.
    if (endsWith(name, "_pressure_plate")) {
      return {{0.0625f, 0, 0.0625f, 0.9375f, 0.0625f, 0.9375f}};
    }
    if (endsWith(name, "_button")) {
      // Face-dependent; the common case is a wall button. A thin box on the given face.
      const std::string face = valueOf(stateMap, "facing_direction");
      return buttonBox(face);
    }

    // Stairs: two boxes (bottom slab + upper step), offset by facing.
    if (endsWith(name, "_stairs")) {
      const bool upsideDown = valueOf(stateMap, "upside_down_bit") == "1" ||
                              valueOf(stateMap, "half") == "top";
      // Bedrock weirdo_direction: 0=east,1=west,2=south,3=north.
      const int dir = clampInt(valueOf(stateMap, "weirdo_direction"), 0, 3, 0);
      return stairsBoxes(dir, upsideDown);
    }

    // Fences: a central post plus (a single-arm approximation of) the connecting arms. The full
    // connection set needs neighbour data the mesher has; the post is the reliable part and the
    // arms are drawn when the state names a connection.
    if (endsWith(name, "_fence") || name == "minecraft:nether_brick_fence") {
      std::vector<ShapeBox> boxes = {{0.375f, 0, 0.375f, 0.625f, 1, 0.625f}};
      if (valueOf(stateMap, "north") == "1" || valueOf(stateMap, "north") == "true") {
        boxes.push_back({0.4375f, 0.5625f, 0, 0.5625f, 0.9375f, 0.375f});
        boxes.push_back({0.4375f, 0.1875f, 0, 0.5625f, 0.5625f, 0.375f});
      }
      if (valueOf(stateMap, "south") == "1" || valueOf(stateMap, "south") == "true") {
        boxes.push_back({0.4375f, 0.5625f, 0.625f, 0.5625f, 0.9375f, 1});
        boxes.push_back({0.4375f, 0.1875f, 0.625f, 0.5625f, 0.5625f, 1});
      }
      if (valueOf(stateMap, "west") == "1" || valueOf(stateMap, "west") == "true") {
        boxes.push_back({0, 0.5625f, 0.4375f, 0.375f, 0.9375f, 0.5625f});
        boxes.push_back({0, 0.1875f, 0.4375f, 0.375f, 0.5625f, 0.5625f});
      }
      if (valueOf(stateMap, "east") == "1" || valueOf(stateMap, "east") == "true") {
        boxes.push_back({0.625f, 0.5625f, 0.4375f, 1, 0.9375f, 0.5625f});
        boxes.push_back({0.625f, 0.1875f, 0.4375f, 1, 0.5625f, 0.5625f});
      }
      return boxes;
    }

    // Walls: a post (full height) plus a lower connecting wall.
    if (endsWith(name, "_wall")) {
      std::vector<ShapeBox> boxes = {{0.3125f, 0, 0.3125f, 0.6875f, 1, 0.6875f}};
      if (valueOf(stateMap, "wall_connection_type_north") != "none" &&
          !valueOf(stateMap, "wall_connection_type_north").empty()) {
        boxes.push_back({0.3125f, 0, 0, 0.6875f, 0.8125f, 0.3125f});
      }
      if (!valueOf(stateMap, "wall_connection_type_south").empty() &&
          valueOf(stateMap, "wall_connection_type_south") != "none") {
        boxes.push_back({0.3125f, 0, 0.6875f, 0.6875f, 0.8125f, 1});
      }
      if (!valueOf(stateMap, "wall_connection_type_west").empty() &&
          valueOf(stateMap, "wall_connection_type_west") != "none") {
        boxes.push_back({0, 0, 0.3125f, 0.3125f, 0.8125f, 0.6875f});
      }
      if (!valueOf(stateMap, "wall_connection_type_east").empty() &&
          valueOf(stateMap, "wall_connection_type_east") != "none") {
        boxes.push_back({0.6875f, 0, 0.3125f, 1, 0.8125f, 0.6875f});
      }
      return boxes;
    }

    // Glass panes / iron bars: a thin plate plus (approximate) connection arms.
    if (endsWith(name, "_pane") || name == "minecraft:iron_bars") {
      std::vector<ShapeBox> boxes = {{0.4375f, 0, 0, 0.5625f, 1, 1}};
      boxes.push_back({0, 0, 0.4375f, 1, 1, 0.5625f});
      return boxes;
    }

    // Doors: a full-height thin slab, offset by the door's facing.
    if (endsWith(name, "_door")) {
      return doorBox(valueOf(stateMap, "direction"));
    }

    // Trapdoors: a thin plate; bottom by default.
    if (endsWith(name, "_trapdoor")) {
      const bool top = valueOf(stateMap, "upside_down_bit") == "1" ||
                       valueOf(stateMap, "half") == "top";
      return top ? std::vector<ShapeBox>{{0, 0.8125f, 0, 1, 1, 1}}
                 : std::vector<ShapeBox>{{0, 0, 0, 1, 0.1875f, 1}};
    }

    // Chests / barrels / beds: a slightly inset box.
    if (name == "minecraft:chest" || name == "minecraft:trapped_chest" ||
        name == "minecraft:ender_chest" || name == "minecraft:barrel") {
      return {{0.0625f, 0, 0.0625f, 0.9375f, 0.875f, 0.9375f}};
    }

    // Torches and lanterns: a thin centre post.
    if (name == "minecraft:torch" || name == "minecraft:soul_torch" ||
        name == "minecraft:redstone_torch") {
      return {{0.4375f, 0, 0.4375f, 0.5625f, 0.625f, 0.5625f}};
    }
    if (name == "minecraft:lantern" || name == "minecraft:soul_lantern") {
      return {{0.3125f, 0.5625f, 0.3125f, 0.6875f, 0.9375f, 0.6875f}};
    }

    // Fluids: a slightly lower, translucent cube. The surface sits below the block top unless the
    // state says it is a source/full block. Flowing direction is ignored — a flowing-agnostic shape
    // is enough to read a pond as water (C4).
    if (name == "minecraft:water" || name == "minecraft:flowing_water" ||
        name == "minecraft:lava" || name == "minecraft:flowing_lava") {
      const std::string level = valueOf(stateMap, "liquid_depth");
      const int depth = clampInt(level, 0, 15, 0);
      // Bedrock liquid_depth: 0 is a source (full), higher values are shallower.
      const float height = depth == 0 ? 1.0f : std::max(0.125f, 1.0f - 0.0625f * depth);
      return {{0, 0, 0, 1, height, 1}};
    }

    // Everything else: the block's own full cube.
    return {{0, 0, 0, 1, 1, 1}};
  }

  /// True for blocks that are never drawn at all (air, structure void, barrier unless toggled, and
  /// light blocks), so the mesher skips them rather than drawing a placeholder cube.
  ///
  /// @param drawBarriers when true, `minecraft:barrier` is drawn (an optional debug toggle); it is
  ///        off by default because a barrier is invisible in-game.
  static bool neverRenders(const BlockState &state, bool drawBarriers = false) {
    const std::string name = baseName(state);
    if (state.isAir()) return true;
    if (name == "minecraft:structure_void") return true;
    if (name == "minecraft:light_block") return true;
    if (name == "minecraft:barrier" && !drawBarriers) return true;
    return false;
  }

  /// True when the block is a plain full cube (the shape is the whole block).
  static bool isCube(const BlockState &state) {
    const auto boxes = boxesFor(state);
    return boxes.size() == 1 && boxes[0].minX == 0.0f && boxes[0].minY == 0.0f &&
           boxes[0].minZ == 0.0f && boxes[0].maxX == 1.0f && boxes[0].maxY == 1.0f &&
           boxes[0].maxZ == 1.0f;
  }

private:
  static std::string baseName(const BlockState &state) {
    const std::string &name = state.name;
    return name.rfind("minecraft:", 0) == 0 ? name : ("minecraft:" + name);
  }

  static bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  static std::string valueOf(const std::vector<std::pair<std::string, std::string>> &states,
                             const std::string &key) {
    for (const auto &[k, v] : states) {
      if (k == key) return v;
    }
    return {};
  }

  static int clampInt(const std::string &text, int lo, int hi, int fallback) {
    if (text.empty()) return fallback;
    int value = 0;
    bool any = false;
    for (char c : text) {
      if (c >= '0' && c <= '9') {
        value = value * 10 + (c - '0');
        any = true;
      }
    }
    if (!any) return fallback;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
  }

  static std::vector<ShapeBox> buttonBox(const std::string &face) {
    // Bedrock facing_direction: 0=down,1=up,2=north,3=south,4=west,5=east.
    const int dir = clampInt(face, 0, 5, 2);
    switch (dir) {
      case 0: return {{0.3125f, 0.875f, 0.375f, 0.6875f, 1, 0.625f}};
      case 1: return {{0.3125f, 0, 0.375f, 0.6875f, 0.125f, 0.625f}};
      case 3: return {{0.3125f, 0.375f, 0.875f, 0.6875f, 0.625f, 1}};
      case 4: return {{0, 0.375f, 0.3125f, 0.125f, 0.625f, 0.6875f}};
      case 5: return {{0.875f, 0.375f, 0.3125f, 1, 0.625f, 0.6875f}};
      case 2:
      default: return {{0.3125f, 0.375f, 0, 0.6875f, 0.625f, 0.125f}};
    }
  }

  static std::vector<ShapeBox> stairsBoxes(int dir, bool upsideDown) {
    // Two boxes: the full bottom slab, and the half-depth upper step on the facing side.
    std::vector<ShapeBox> boxes;
    if (!upsideDown) {
      boxes.push_back({0, 0, 0, 1, 0.5f, 1});
      switch (dir) {
        case 0: boxes.push_back({0.5f, 0.5f, 0, 1, 1, 1}); break;  // east
        case 1: boxes.push_back({0, 0.5f, 0, 0.5f, 1, 1}); break;  // west
        case 2: boxes.push_back({0, 0.5f, 0.5f, 1, 1, 1}); break;  // south
        case 3: default: boxes.push_back({0, 0.5f, 0, 1, 1, 0.5f}); break;  // north
      }
    } else {
      boxes.push_back({0, 0.5f, 0, 1, 1, 1});
      switch (dir) {
        case 0: boxes.push_back({0.5f, 0, 0, 1, 0.5f, 1}); break;
        case 1: boxes.push_back({0, 0, 0, 0.5f, 0.5f, 1}); break;
        case 2: boxes.push_back({0, 0, 0.5f, 1, 0.5f, 1}); break;
        case 3: default: boxes.push_back({0, 0, 0, 1, 0.5f, 0.5f}); break;
      }
    }
    return boxes;
  }

  static std::vector<ShapeBox> doorBox(const std::string &direction) {
    // Bedrock `direction`: 0=east,1=south,2=west,3=north. A door is a thin slab on that side.
    const int dir = clampInt(direction, 0, 3, 0);
    switch (dir) {
      case 0: return {{0.8125f, 0, 0, 1, 1, 1}};   // east face
      case 1: return {{0, 0, 0.8125f, 1, 1, 1}};   // south face
      case 2: return {{0, 0, 0, 0.1875f, 1, 1}};   // west face
      case 3: default: return {{0, 0, 0, 1, 1, 0.1875f}};  // north face
    }
  }
};

} // namespace bitematica::core
