#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Schematic.hpp"
#include "render/BlockColors.hpp"
#include "render/ViewCamera.hpp"

namespace bitematica::render {

/// A shaded triangle in HUD screen space, ready to submit as a draw command.
struct Triangle {
  ScreenPoint a;
  ScreenPoint b;
  ScreenPoint c;
  uint32_t color{};
  float depth{}; // Average depth, for painter sorting.
};

/// Turns a schematic + camera into a flat triangle list.
///
/// The HUD draws filled triangles, so a block is emitted as the visible faces of its
/// cube, each face split into two triangles. Only faces whose outward normal points
/// toward the camera survive the cull, which roughly halves the triangle count and,
/// more importantly, stops back faces from z-fighting the front ones under a painter
/// sort. Output is capped so a huge schematic cannot stall the frame: past the cap the
/// caller is told the mesh was truncated and the toolbar shows it.
class MeshBuilder {
public:
  struct Result {
    std::vector<Triangle> triangles;
    bool truncated{};
    long long drawnBlocks{};
    long long visibleBlocks{};
  };

  /// Builds the mesh. `maxBlocks` bounds how many blocks are turned into geometry.
  static Result build(const core::Schematic &schematic, const ViewCamera &camera,
                      long long maxBlocks = 90000) {
    Result result;
    if (schematic.empty()) return result;

    const int sx = schematic.sizeX();
    const int sy = schematic.sizeY();
    const int sz = schematic.sizeZ();

    // Iterate blocks; count visible (non-air with an exposed face) first so the cap
    // and the "visible blocks" readout reflect real geometry, not raw volume.
    std::vector<std::pair<core::LocalPos, uint32_t>> visible;
    visible.reserve(4096);
    for (int y = 0; y < sy; ++y) {
      for (int z = 0; z < sz; ++z) {
        for (int x = 0; x < sx; ++x) {
          const uint32_t state = schematic.blockAt(x, y, z);
          if (state == 0 || state >= schematic.palette.size()) continue;
          const core::BlockState &block = schematic.palette.at(state);
          if (block.isAir()) continue;
          if (!hasExposedFace(schematic, x, y, z)) continue;
          visible.push_back({{x, y, z}, state});
        }
      }
    }
    result.visibleBlocks = static_cast<long long>(visible.size());
    if (visible.size() > static_cast<size_t>(maxBlocks)) {
      visible.resize(static_cast<size_t>(maxBlocks));
      result.truncated = true;
    }

    result.triangles.reserve(visible.size() * 6);
    for (const auto &[pos, state] : visible) {
      const Color base = BlockColors::forBlock(schematic.palette.at(state).name);
      if ((base.argb & 0xff000000u) == 0) continue; // fully transparent
      emitBlock(result.triangles, schematic, camera, pos.x, pos.y, pos.z, base);
    }
    result.drawnBlocks = static_cast<long long>(visible.size());

    // Painter's algorithm: far faces first so near faces overwrite them.
    std::sort(result.triangles.begin(), result.triangles.end(),
              [](const Triangle &l, const Triangle &r) { return l.depth > r.depth; });
    return result;
  }

private:
  /// True when at least one of a block's six neighbours is air or out of bounds.
  static bool hasExposedFace(const core::Schematic &schematic, int x, int y, int z) {
    auto solid = [&](int ox, int oy, int oz) {
      const uint32_t state = schematic.blockAt(x + ox, y + oy, z + oz);
      if (state == 0 || state >= schematic.palette.size()) return false;
      return !schematic.palette.at(state).isAir();
    };
    return !solid(0, 1, 0) || !solid(0, -1, 0) || !solid(1, 0, 0) ||
           !solid(-1, 0, 0) || !solid(0, 0, 1) || !solid(0, 0, -1);
  }

  static void emitBlock(std::vector<Triangle> &out, const core::Schematic &schematic,
                        const ViewCamera &camera, int x, int y, int z, Color base) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float fz = static_cast<float>(z);
    const float one = 1.0f;

    // Six faces as (normal, four corner offsets, shading). Shading gives the cube
    // readable depth without any lighting model.
    struct Face {
      float nx, ny, nz;
      float corners[4][3];
      float shade;
    };
    const Face faces[6] = {
        // +Y top
        {0, 1, 0, {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}, 1.00f},
        // -Y bottom
        {0, -1, 0, {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}}, 0.45f},
        // +X
        {1, 0, 0, {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}, 0.72f},
        // -X
        {-1, 0, 0, {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}}, 0.72f},
        // +Z
        {0, 0, 1, {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}}, 0.86f},
        // -Z
        {0, 0, -1, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, 0.86f},
    };

    for (const Face &face : faces) {
      // Cull: a normal pointing away from the camera is behind the block.
      if (camera.depthOfDirection(face.nx, face.ny, face.nz) > 0.0f) continue;

      ScreenPoint p[4];
      for (int i = 0; i < 4; ++i) {
        p[i] = camera.project(schematic, fx + face.corners[i][0] * one,
                              fy + face.corners[i][1] * one,
                              fz + face.corners[i][2] * one);
      }
      const uint32_t color = base.shade(face.shade).argb;

      Triangle first;
      first.a = p[0];
      first.b = p[1];
      first.c = p[2];
      first.color = color;
      first.depth = (p[0].depth + p[1].depth + p[2].depth) / 3.0f;

      Triangle second;
      second.a = p[0];
      second.b = p[2];
      second.c = p[3];
      second.color = color;
      second.depth = (p[0].depth + p[2].depth + p[3].depth) / 3.0f;

      out.push_back(first);
      out.push_back(second);
    }
  }
};

} // namespace bitematica::render
