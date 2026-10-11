#include "mesh/SectionMesher.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

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
  return !s.palette.at(state).isAir();
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

/// Emits a quad on face plane `planeW` spanning `aLen` x `bLen` cells, in section-relative coords.
///
/// The four corners are built in (a, b, n) space and wound consistently so a single
/// `glCullFace(GL_BACK)` works.
void emitQuad(SectionMesh &mesh, const FaceDir &face, float planeW, int aStart, int bStart,
              int aLen, int bLen, uint32_t color, float originX, float originY, float originZ) {
  // (a, b) corners of the rectangle.
  const float ab[4][2] = {
      {static_cast<float>(aStart), static_cast<float>(bStart)},
      {static_cast<float>(aStart), static_cast<float>(bStart + bLen)},
      {static_cast<float>(aStart + aLen), static_cast<float>(bStart + bLen)},
      {static_cast<float>(aStart + aLen), static_cast<float>(bStart)},
  };
  // Reverse the winding for negative faces so the normal still points outward.
  const int order[4] = {0, face.positive ? 1 : 3, 2, face.positive ? 3 : 1};

  const uint32_t shaded = shade(color, face.shade);
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  for (int i = 0; i < 4; ++i) {
    const float *c = ab[order[i]];
    float xyz[3] = {0.0f, 0.0f, 0.0f};
    xyz[face.a] = c[0];
    xyz[face.b] = c[1];
    xyz[face.n] = planeW;
    MeshVertex vertex;
    vertex.x = xyz[0] - originX;
    vertex.y = xyz[1] - originY;
    vertex.z = xyz[2] - originZ;
    vertex.color = shaded;
    vertex.face = face.face;
    mesh.vertices.push_back(vertex);
  }
  mesh.triangles.push_back({base, base + 1, base + 2});
  mesh.triangles.push_back({base, base + 2, base + 3});
}

} // namespace

SectionMesh SectionMesher::buildSection(const Schematic &schematic,
                                        const world::Placement &placement, int sectionX,
                                        int sectionY, int sectionZ, int layerMin, int layerMax) {
  (void)placement; // geometry is built in local space; the placement is applied at draw time.
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

          const int sign = face.positive ? 1 : -1;
          const int nx = x + (face.n == 0 ? sign : 0);
          const int ny = y + (face.n == 1 ? sign : 0);
          const int nz = z + (face.n == 2 ? sign : 0);
          if (isSolid(schematic, nx, ny, nz)) continue; // interior face

          const uint32_t state = schematic.blockAt(x, y, z);
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
                   mesh.originY, mesh.originZ);

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
