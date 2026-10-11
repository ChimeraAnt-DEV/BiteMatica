#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/Schematic.hpp"
#include "render/BlockColors.hpp"
#include "world/Placement.hpp"

namespace bitematica::mesh {

/// One vertex of a section mesh: position in world-relative block units, a packed colour and a
/// shading factor. Positions are relative to the placement origin so a section's VBO is small.
struct MeshVertex {
  float x{};
  float y{};
  float z{};
  uint32_t color{};
  /// 0 = top face, 1 = side, 2 = bottom. Used by the shader for a little directional shading.
  uint8_t face{};
  uint8_t pad[3]{};
};

/// A triangle index triplet.
struct MeshTriangle {
  uint32_t a{};
  uint32_t b{};
  uint32_t c{};
};

/// The mesh for one 16x16x16 (or edge-clipped) section of the schematic.
struct SectionMesh {
  int sectionX{};
  int sectionY{};
  int sectionZ{};
  /// World-space minimum corner of this section, in blocks.
  float originX{};
  float originY{};
  float originZ{};
  std::vector<MeshVertex> vertices;
  std::vector<MeshTriangle> triangles;

  bool empty() const { return triangles.empty(); }
};

/// Builds per-section meshes from a schematic.
///
/// The previous approach projected every block to screen space each frame, in software, on the
/// render thread. This replaces it with geometry built once: the schematic is split into 16^3
/// sections, each block contributes only its *exposed* faces (a face whose neighbour is solid is
/// never emitted), and coplanar faces of the same colour are merged greedily so a flat wall becomes
/// a handful of quads instead of hundreds. The result is uploaded as a VBO per section and drawn
/// with the camera matrices, which is what makes world-space, depth-tested rendering possible.
///
/// Everything here is pure and host-testable: it produces vertices and indices and never touches a
/// GL context. Uploading and drawing are the renderer's job.
class SectionMesher {
public:
  static constexpr int kSectionSize = 16;

  struct Result {
    std::vector<SectionMesh> sections;
    long long emittedQuads{};
    long long culledFaces{};
    bool truncated{};
  };

  /// Builds the section meshes for a schematic under a placement.
  ///
  /// @param schematic the parsed schematic
  /// @param placement where the schematic sits in the world (origin/rotation/mirror)
  /// @param layerMin,layerMax inclusive Y range in schematic-local space to mesh (layer modes)
  /// @param maxTriangles triangle budget; meshing stops and `truncated` is set when exceeded
  static Result build(const core::Schematic &schematic, const world::Placement &placement,
                      int layerMin, int layerMax, long long maxTriangles = 1500000);

  /// Builds a single section's mesh. Exposed so a section can be rebuilt on an edit without
  /// re-meshing the whole schematic.
  static SectionMesh buildSection(const core::Schematic &schematic,
                                  const world::Placement &placement, int sectionX, int sectionY,
                                  int sectionZ, int layerMin, int layerMax);

  /// One section's block-edge outline: line-segment vertices plus index pairs (two per segment).
  struct SectionOutline {
    int sectionX{};
    int sectionY{};
    int sectionZ{};
    float originX{};
    float originY{};
    float originZ{};
    std::vector<MeshVertex> vertices;
    /// Each entry is a segment (a, b, unused).
    std::vector<MeshTriangle> segments;
    bool empty() const { return segments.empty(); }
  };

  struct OutlineResult {
    std::vector<SectionOutline> sections;
    long long emittedEdges{};
    bool truncated{};
  };

  /// Builds the block-edge outlines (Phase B8): for each exposed block, its 12 cube edges,
  /// de-duplicated against neighbouring blocks within the section, as `GL_LINES` segments. This is
  /// not triangle edges — it is the box edge of each block, which is what reads as a Litematica
  /// outline. Respects the same layer filter and triangle/line budget.
  ///
  /// @param color the outline RGBA (packed 0xAARRGGBB)
  /// @param maxSegments budget; stops and sets `truncated` when exceeded
  static OutlineResult buildOutlines(const core::Schematic &schematic,
                                     const world::Placement &placement, int layerMin, int layerMax,
                                     uint32_t color, long long maxSegments = 200000);
};

} // namespace bitematica::mesh
