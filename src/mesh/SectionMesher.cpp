#include "mesh/SectionMesher.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "core/BlockShape.hpp"

namespace bitematica::mesh {
namespace {

using core::Schematic;

/// One cell of a greedy-meshing slab: the block's colour and whether the face is exposed.
struct MaskCell {
  uint32_t color{};
  bool filled{};
};

/// One face direction: the normal axis and sign, the two in-plane axes, and the shading weight.
///
/// `n` is the axis of the face normal (0=X, 1=Y, 2=Z); `positive` is the sign. `a` and `b` are the
/// two in-plane axes in a fixed order so a slab mask is indexed (a, b) uniformly. `face` tags the
/// vertex for the shader's directional shading.
struct FaceDir {
  int n;
  bool positive;
  int a;
  int b;
  uint8_t face;
  float shade;
};

// +Y, -Y, +X, -X, +Z, -Z.
constexpr FaceDir kFaces[6] = {
    {1, true, 0, 2, 0, 1.00f},
    {1, false, 0, 2, 2, 0.45f},
    {0, true, 1, 2, 1, 0.72f},
    {0, false, 1, 2, 1, 0.72f},
    {2, true, 0, 1, 1, 0.86f},
    {2, false, 0, 1, 1, 0.86f},
};

int axisSize(const Schematic &s, int axis) {
  return axis == 0 ? s.sizeX() : (axis == 1 ? s.sizeY() : s.sizeZ());
}

bool isSolid(const Schematic &s, int x, int y, int z) {
  const uint32_t state = s.blockAt(x, y, z);
  if (state == 0 || state >= s.palette.size()) return false;
  // Air, structure void, light blocks and barriers are not solid for meshing or culling: a block
  // next to one of them still shows that face, and they themselves are never drawn (C4).
  return !core::BlockShape::neverRenders(s.palette.at(state));
}

uint32_t shade(uint32_t argb, float factor) {
  const int a = static_cast<int>((argb >> 24) & 0xff);
  int r = static_cast<int>((argb >> 16) & 0xff);
  int g = static_cast<int>((argb >> 8) & 0xff);
  int b = static_cast<int>(argb & 0xff);
  r = std::clamp(static_cast<int>(r * factor), 0, 255);
  g = std::clamp(static_cast<int>(g * factor), 0, 255);
  b = std::clamp(static_cast<int>(b * factor), 0, 255);
  return static_cast<uint32_t>((a << 24) | (r << 16) | (g << 8) | b);
}

/// How the placement's mirror is applied to emitted geometry.
///
/// The shader applies the placement's yaw and origin but not its mirror, so the mirror must be baked
/// into the mesh's local coordinates. A mirror is a reflection, which reverses handedness, so the
/// triangle winding is flipped too — otherwise `glCullFace(GL_BACK)` would cull every mirrored face
/// and the mirrored ghost would be invisible.
struct MirrorTransform {
  bool mirrorX{false};
  bool mirrorZ{false};
  float sizeX{1.0f};
  float sizeZ{1.0f};

  bool active() const { return mirrorX || mirrorZ; }

  /// Applies the mirror to a global-local coordinate in place.
  void apply(float xyz[3]) const {
    if (mirrorX) xyz[0] = sizeX - xyz[0];
    if (mirrorZ) xyz[2] = sizeZ - xyz[2];
  }
};

/// Emits a quad on face plane `planeW` spanning `aLen` x `bLen` cells, in section-relative coords.
///
/// The four corners are built in (a, b, n) space and wound consistently so a single
/// `glCullFace(GL_BACK)` works. When a mirror is active the corners are reflected and the winding is
/// reversed, so the quad still faces outward after the reflection.
void emitQuad(SectionMesh &mesh, const FaceDir &face, float planeW, int aStart, int bStart,
              int aLen, int bLen, uint32_t color, float originX, float originY, float originZ,
              const MirrorTransform &mirror) {
  // (a, b) corners of the rectangle.
  const float ab[4][2] = {
      {static_cast<float>(aStart), static_cast<float>(bStart)},
      {static_cast<float>(aStart), static_cast<float>(bStart + bLen)},
      {static_cast<float>(aStart + aLen), static_cast<float>(bStart + bLen)},
      {static_cast<float>(aStart + aLen), static_cast<float>(bStart)},
  };
  // Reverse the winding for negative faces so the normal still points outward.
  int order[4] = {0, face.positive ? 1 : 3, 2, face.positive ? 3 : 1};

  const uint32_t shaded = shade(color, face.shade);
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int i = 0; i < 4; ++i) {
    const float *c = ab[order[i]];
    float xyz[3] = {0.0f, 0.0f, 0.0f};
    xyz[face.a] = c[0];
    xyz[face.b] = c[1];
    xyz[face.n] = planeW;
    mirror.apply(xyz);
    MeshVertex vertex;
    vertex.x = xyz[0] - originX;
    vertex.y = xyz[1] - originY;
    vertex.z = xyz[2] - originZ;
    vertex.color = shaded;
    vertex.face = face.face;
    mesh.vertices.push_back(vertex);
  }
  // A reflection flips the winding; emit the reverse order so the face still culls correctly.
  if (mirror.active()) {
    mesh.triangles.push_back({base, base + 2, base + 1});
    mesh.triangles.push_back({base, base + 3, base + 2});
  } else {
    mesh.triangles.push_back({base, base + 1, base + 2});
    mesh.triangles.push_back({base, base + 2, base + 3});
  }
}

/// Emits the six faces of one axis-aligned box, wound outward, in section-relative coordinates.
///
/// Used for non-cube block shapes (C3). A shape's boxes are independent of their neighbours, so the
/// exposed-face cull does not apply — the boxes are small and the extra triangles are bounded by the
/// shape table. Each face gets the directional shading the greedy path uses.
void emitBox(SectionMesh &mesh, const core::ShapeBox &box, uint32_t color, float originX,
             float originY, float originZ, const MirrorTransform &mirror) {
  // (axis, positive) for the six faces, matching the shading tags in `kFaces`.
  struct FaceDef {
    int n;
    bool positive;
    uint8_t faceTag;
    float shade;
  };
  const FaceDef faces[6] = {
      {1, true, 0, 1.00f}, {1, false, 2, 0.45f}, {0, true, 1, 0.72f},
      {0, false, 1, 0.72f}, {2, true, 1, 0.86f}, {2, false, 1, 0.86f},
  };
  float mn[3] = {box.minX, box.minY, box.minZ};
  float mx[3] = {box.maxX, box.maxY, box.maxZ};
  // Bake the mirror into the box: a reflection swaps the min and max on the mirrored axis.
  if (mirror.mirrorX) {
    const float lo = mirror.sizeX - mx[0];
    const float hi = mirror.sizeX - mn[0];
    mn[0] = lo;
    mx[0] = hi;
  }
  if (mirror.mirrorZ) {
    const float lo = mirror.sizeZ - mx[2];
    const float hi = mirror.sizeZ - mn[2];
    mn[2] = lo;
    mx[2] = hi;
  }

  for (const FaceDef &f : faces) {
    const float plane = f.positive ? mx[f.n] : mn[f.n];
    // The two in-plane axes, in the same fixed order as `kFaces` (a, b).
    const int a = f.n == 0 ? 1 : 0;
    const int b = f.n == 2 ? 1 : 2;
    const float a0 = mn[a];
    const float a1 = mx[a];
    const float b0 = mn[b];
    const float b1 = mx[b];
    // Corners in (a,b): (a0,b0), (a0,b1), (a1,b1), (a1,b0); reverse winding for a negative face.
    const float corners[4][2] = {{a0, b0}, {a0, b1}, {a1, b1}, {a1, b0}};
    const int order[4] = {0, f.positive ? 1 : 3, 2, f.positive ? 3 : 1};
    const uint32_t shaded = shade(color, f.shade);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
      float xyz[3] = {0.0f, 0.0f, 0.0f};
      xyz[a] = corners[order[i]][0];
      xyz[b] = corners[order[i]][1];
      xyz[f.n] = plane;
      MeshVertex vertex;
      vertex.x = xyz[0] - originX;
      vertex.y = xyz[1] - originY;
      vertex.z = xyz[2] - originZ;
      vertex.color = shaded;
      vertex.face = f.faceTag;
      mesh.vertices.push_back(vertex);
    }
    // A mirror reflects the box (min/max swapped above), which flips the winding; reverse it so the
    // face still culls correctly.
    if (mirror.active()) {
      mesh.triangles.push_back({base, base + 2, base + 1});
      mesh.triangles.push_back({base, base + 3, base + 2});
    } else {
      mesh.triangles.push_back({base, base + 1, base + 2});
      mesh.triangles.push_back({base, base + 2, base + 3});
    }
  }
}

} // namespace

SectionMesh SectionMesher::buildSection(const Schematic &schematic,
                                        const world::Placement &placement, int sectionX,
                                        int sectionY, int sectionZ, int layerMin, int layerMax) {
  // The placement's rotation is applied by the shader at draw time, but its **mirror is not**: it is
  // baked into the local coordinates here, because a reflection reverses triangle winding and the
  // shader has no way to express that. See `MirrorTransform`.
  SectionMesh mesh;
  mesh.sectionX = sectionX;
  mesh.sectionY = sectionY;
  mesh.sectionZ = sectionZ;
  mesh.originX = static_cast<float>(sectionX * kSectionSize);
  mesh.originY = static_cast<float>(sectionY * kSectionSize);
  mesh.originZ = static_cast<float>(sectionZ * kSectionSize);

  const int sizeX = schematic.sizeX();
  const int sizeY = schematic.sizeY();
  const int sizeZ = schematic.sizeZ();
  if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) return mesh;

  MirrorTransform mirror;
  mirror.mirrorX = placement.mirror == world::Mirror::X;
  mirror.mirrorZ = placement.mirror == world::Mirror::Z;
  mirror.sizeX = static_cast<float>(sizeX);
  mirror.sizeZ = static_cast<float>(sizeZ);

  // Non-cube blocks are meshed from their shape table, not the greedy full-cube path. They are
  // collected here and emitted after the greedy pass, so a slab or stair reads as its own shape
  // (C3) instead of a full cube. Full-cube blocks stay on the greedy path, which is where the
  // exposed-face culling and merging happen.
  struct ShapeBlock {
    int x, y, z;
    uint32_t color;
  };
  std::vector<ShapeBlock> shapeBlocks;

  // Collect the non-cube blocks in this section once, so they are meshed by shape rather than as
  // cubes. A block is "non-cube" when its shape table gives something other than the full block.
  const int secMinX = sectionX * kSectionSize;
  const int secMinY = sectionY * kSectionSize;
  const int secMinZ = sectionZ * kSectionSize;
  const int secMaxX = std::min(secMinX + kSectionSize, sizeX);
  const int secMaxY = std::min(secMinY + kSectionSize, sizeY);
  const int secMaxZ = std::min(secMinZ + kSectionSize, sizeZ);
  for (int y = std::max(secMinY, layerMin); y < std::min(secMaxY, layerMax + 1); ++y) {
    for (int z = secMinZ; z < secMaxZ; ++z) {
      for (int x = secMinX; x < secMaxX; ++x) {
        const uint32_t state = schematic.blockAt(x, y, z);
        if (state == 0 || state >= schematic.palette.size()) continue;
        const core::BlockState &block = schematic.palette.at(state);
        if (core::BlockShape::neverRenders(block)) continue;
        if (core::BlockShape::isCube(block)) continue;
        shapeBlocks.push_back({x, y, z, render::BlockColors::forBlock(block.name).argb});
      }
    }
  }

  std::vector<MaskCell> mask;
  for (const FaceDir &face : kFaces) {
    const int aSize = axisSize(schematic, face.a);
    const int bSize = axisSize(schematic, face.b);
    const int nSize = axisSize(schematic, face.n);
    const int nSectionMin =
        (face.n == 0 ? sectionX : (face.n == 1 ? sectionY : sectionZ)) * kSectionSize;
    const int nSectionMax = std::min(nSectionMin + kSectionSize, nSize);

    for (int w = std::max(0, nSectionMin); w < nSectionMax; ++w) {
      mask.assign(static_cast<size_t>(aSize) * bSize, MaskCell{});
      for (int b = 0; b < bSize; ++b) {
        for (int a = 0; a < aSize; ++a) {
          int xyz[3] = {0, 0, 0};
          xyz[face.n] = w;
          xyz[face.a] = a;
          xyz[face.b] = b;
          const int x = xyz[0], y = xyz[1], z = xyz[2];

          if (y < layerMin || y > layerMax) continue;
          if (!isSolid(schematic, x, y, z)) continue;

          const uint32_t state = schematic.blockAt(x, y, z);
          // A non-cube block is meshed by its shape, not as a full cube, so it does not fill the
          // greedy mask. Its neighbours still cull against it (it is solid), so a slab under a
          // full-cube block still hides that block's bottom face.
          if (!core::BlockShape::isCube(schematic.palette.at(state))) continue;

          const int sign = face.positive ? 1 : -1;
          const int nx = x + (face.n == 0 ? sign : 0);
          const int ny = y + (face.n == 1 ? sign : 0);
          const int nz = z + (face.n == 2 ? sign : 0);
          if (isSolid(schematic, nx, ny, nz)) continue; // interior face

          MaskCell &cell = mask[static_cast<size_t>(a) + static_cast<size_t>(b) * aSize];
          cell.filled = true;
          cell.color = render::BlockColors::forBlock(schematic.palette.at(state).name).argb;
        }
      }

      for (int b = 0; b < bSize; ++b) {
        for (int a = 0; a < aSize;) {
          const MaskCell &cell = mask[static_cast<size_t>(a) + static_cast<size_t>(b) * aSize];
          if (!cell.filled) {
            ++a;
            continue;
          }
          int width = 1;
          while (a + width < aSize) {
            const MaskCell &next =
                mask[static_cast<size_t>(a + width) + static_cast<size_t>(b) * aSize];
            if (!next.filled || next.color != cell.color) break;
            ++width;
          }
          int height = 1;
          bool growing = true;
          while (growing && b + height < bSize) {
            for (int k = 0; k < width; ++k) {
              const MaskCell &next =
                  mask[static_cast<size_t>(a + k) + static_cast<size_t>(b + height) * aSize];
              if (!next.filled || next.color != cell.color) {
                growing = false;
                break;
              }
            }
            if (growing) ++height;
          }

          const float planeW = static_cast<float>(w) + (face.positive ? 1.0f : 0.0f);
          emitQuad(mesh, face, planeW, a, b, width, height, cell.color, mesh.originX,
                   mesh.originY, mesh.originZ, mirror);

          for (int dv = 0; dv < height; ++dv) {
            for (int du = 0; du < width; ++du) {
              mask[static_cast<size_t>(a + du) + static_cast<size_t>(b + dv) * aSize].filled =
                  false;
            }
          }
          a += width;
        }
      }
    }
  }

  // Emit the non-cube blocks from their shape table.
  for (const ShapeBlock &block : shapeBlocks) {
    const core::BlockState &state = schematic.palette.at(schematic.blockAt(block.x, block.y, block.z));
    for (const core::ShapeBox &box : core::BlockShape::boxesFor(state)) {
      emitBox(mesh, {box.minX + static_cast<float>(block.x), box.minY + static_cast<float>(block.y),
                     box.minZ + static_cast<float>(block.z),
                     box.maxX + static_cast<float>(block.x), box.maxY + static_cast<float>(block.y),
                     box.maxZ + static_cast<float>(block.z)},
              block.color, mesh.originX, mesh.originY, mesh.originZ, mirror);
    }
  }

  return mesh;
}

SectionMesher::OutlineResult SectionMesher::buildOutlines(const Schematic &schematic,
                                                          const world::Placement &placement,
                                                          int layerMin, int layerMax,
                                                          uint32_t color, long long maxSegments) {
  (void)placement; // geometry is local; the placement is applied at draw time.
  OutlineResult result;
  if (schematic.empty()) return result;

  const int sizeX = schematic.sizeX();
  const int sizeY = schematic.sizeY();
  const int sizeZ = schematic.sizeZ();
  const int sectionsX = (sizeX + kSectionSize - 1) / kSectionSize;
  const int sectionsY = (sizeY + kSectionSize - 1) / kSectionSize;
  const int sectionsZ = (sizeZ + kSectionSize - 1) / kSectionSize;

  // A block-edge outline: every *surface* block's 12 cube edges, de-duplicated within the section so
  // a shared edge is drawn once. This is the block silhouette (each exposed block outlined), which
  // is what reads as a Litematica outline — not the triangle edges of the fill mesh. Edges on a
  // section seam may be drawn by both sections; the only artefact is a marginally darker shared line.
  auto solid = [&](int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= sizeX || y >= sizeY || z >= sizeZ) return false;
    if (y < layerMin || y > layerMax) return false;
    return isSolid(schematic, x, y, z);
  };

  for (int sz = 0; sz < sectionsZ && !result.truncated; ++sz) {
    for (int sy = 0; sy < sectionsY && !result.truncated; ++sy) {
      for (int sx = 0; sx < sectionsX && !result.truncated; ++sx) {
        SectionOutline out;
        out.sectionX = sx;
        out.sectionY = sy;
        out.sectionZ = sz;
        out.originX = static_cast<float>(sx * kSectionSize);
        out.originY = static_cast<float>(sy * kSectionSize);
        out.originZ = static_cast<float>(sz * kSectionSize);

        const int minX = sx * kSectionSize;
        const int minY = sy * kSectionSize;
        const int minZ = sz * kSectionSize;
        const int maxX = std::min(minX + kSectionSize, sizeX);
        const int maxY = std::min(minY + kSectionSize, sizeY);
        const int maxZ = std::min(minZ + kSectionSize, sizeZ);

        // Canonical edge keys already emitted in this section. An edge is keyed by its axis and its
        // lower lattice endpoint, so two surface blocks sharing an edge agree on the key.
        std::unordered_set<std::uint64_t> emitted;

        auto addEdge = [&](int axis, int x, int y, int z) {
          const std::uint64_t key = (static_cast<std::uint64_t>(axis) << 42) |
                                    (static_cast<std::uint64_t>(x + 0x100000) << 28) |
                                    (static_cast<std::uint64_t>(y + 0x100000) << 14) |
                                    static_cast<std::uint64_t>(z + 0x100000);
          if (!emitted.insert(key).second) return;
          // The edge runs one block along `axis` from the lower corner (x,y,z).
          float a[3] = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
          float b[3] = {a[0], a[1], a[2]};
          b[axis] += 1.0f;
          const uint32_t base = static_cast<uint32_t>(out.vertices.size());
          for (const float *p : {a, b}) {
            MeshVertex v;
            v.x = p[0] - out.originX;
            v.y = p[1] - out.originY;
            v.z = p[2] - out.originZ;
            v.color = color;
            v.face = 1; // side tag; the line shader ignores it
            out.vertices.push_back(v);
          }
          out.segments.push_back({base, base + 1, 0});
        };

        for (int z = minZ; z < maxZ; ++z) {
          for (int y = minY; y < maxY; ++y) {
            for (int x = minX; x < maxX; ++x) {
              if (!solid(x, y, z)) continue;
              // Only surface blocks contribute edges; an interior block has no visible outline.
              const bool exposed = !solid(x - 1, y, z) || !solid(x + 1, y, z) ||
                                   !solid(x, y - 1, z) || !solid(x, y + 1, z) ||
                                   !solid(x, y, z - 1) || !solid(x, y, z + 1);
              if (!exposed) continue;
              // The four edges parallel to each axis, at the four corners of the block's cross-section.
              for (int c = 0; c < 4; ++c) {
                const int dx = (c & 1) ? 1 : 0;
                const int dz = (c & 2) ? 1 : 0;
                addEdge(1, x + dx, y, z + dz); // Y edges (vary y)
              }
              for (int c = 0; c < 4; ++c) {
                const int dy = (c & 1) ? 1 : 0;
                const int dz = (c & 2) ? 1 : 0;
                addEdge(0, x, y + dy, z + dz); // X edges (vary x)
              }
              for (int c = 0; c < 4; ++c) {
                const int dx = (c & 1) ? 1 : 0;
                const int dy = (c & 2) ? 1 : 0;
                addEdge(2, x + dx, y + dy, z); // Z edges (vary z)
              }
            }
          }
        }

        if (out.empty()) continue;
        result.emittedEdges += static_cast<long long>(out.segments.size());
        if (result.emittedEdges > maxSegments) {
          result.truncated = true;
          break;
        }
        result.sections.push_back(std::move(out));
      }
    }
  }
  return result;
}

SectionMesher::Result SectionMesher::build(const Schematic &schematic,
                                           const world::Placement &placement, int layerMin,
                                           int layerMax, long long maxTriangles) {
  Result result;
  if (schematic.empty()) return result;

  const int sectionsX = (schematic.sizeX() + kSectionSize - 1) / kSectionSize;
  const int sectionsY = (schematic.sizeY() + kSectionSize - 1) / kSectionSize;
  const int sectionsZ = (schematic.sizeZ() + kSectionSize - 1) / kSectionSize;

  long long triangles = 0;
  for (int sz = 0; sz < sectionsZ; ++sz) {
    for (int sy = 0; sy < sectionsY; ++sy) {
      for (int sx = 0; sx < sectionsX; ++sx) {
        SectionMesh section = buildSection(schematic, placement, sx, sy, sz, layerMin, layerMax);
        if (section.empty()) continue;
        triangles += static_cast<long long>(section.triangles.size());
        if (triangles > maxTriangles) {
          result.truncated = true;
          break;
        }
        result.emittedQuads += static_cast<long long>(section.triangles.size()) / 2;
        result.sections.push_back(std::move(section));
      }
      if (result.truncated) break;
    }
    if (result.truncated) break;
  }
  return result;
}

} // namespace bitematica::mesh
