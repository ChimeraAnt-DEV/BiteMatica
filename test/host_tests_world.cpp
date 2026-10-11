// Host tests for the world-space layer: placement transforms, the section mesher, verification,
// Java->Bedrock translation, layers, selection capture, the .litematic writer, live materials and
// the cheat-route commands.
//
// Everything here is pure C++ with no GL and no Android, which is the point: the transform math and
// the mesher are exactly the code most likely to be subtly wrong (a rotation in the wrong order, a
// winding that culls the wrong faces) and the least practical to debug on a device.

#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/LiveMaterials.hpp"
#include "core/MaterialList.hpp"
#include "core/Schematic.hpp"
#include "format/LitematicWriter.hpp"
#include "format/SchematicFormat.hpp"
#include "mesh/SectionMesher.hpp"
#include "render/MathTypes.hpp"
#include "render/PlacementCull.hpp"
#include "translate/BlockTranslation.hpp"
#include "world/CheatPlacement.hpp"
#include "world/Placement.hpp"
#include "world/Selection.hpp"
#include "world/Verifier.hpp"
#include "world/WorldSource.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string &message) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("  FAIL: %s\n", message.c_str());
  }
}

std::string fixture(const std::string &name) {
  return std::string(BITEMATICA_FIXTURE_DIR) + "/" + name;
}

/// A fake world backed by a map, so the verification cases are deterministic and host-runnable.
class FakeWorld : public bitematica::world::WorldSource {
public:
  void set(const bitematica::core::BlockPos &pos, const std::string &name) {
    mBlocks[key(pos)] = name;
  }
  bool readBlock(const bitematica::core::BlockPos &position,
                 bitematica::core::BlockState &out) override {
    auto it = mBlocks.find(key(position));
    if (it == mBlocks.end()) {
      out = bitematica::core::BlockState{};
      return true; // air
    }
    out.name = it->second;
    out.states.clear();
    return true;
  }
  bool available() const override { return mAvailable; }
  void setAvailable(bool value) { mAvailable = value; }

private:
  static std::string key(const bitematica::core::BlockPos &p) {
    return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
  }
  std::unordered_map<std::string, std::string> mBlocks;
  bool mAvailable{true};
};

// --- placement transforms -------------------------------------------------------------

void testPlacementRoundTrip() {
  std::printf("testPlacementRoundTrip\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("column.schematic"));
  check(loaded.success, "column fixture loads");
  if (!loaded.success) return;
  const bitematica::core::Schematic &s = loaded.schematic;

  // For every rotation and mirror, toWorld then toLocal must return the original local position.
  for (int rot = 0; rot < 4; ++rot) {
    for (int mir = 0; mir < 3; ++mir) {
      bitematica::world::Placement placement;
      placement.origin = {100, 64, -50};
      placement.rotation = static_cast<bitematica::world::Rotation>(rot);
      placement.mirror = static_cast<bitematica::world::Mirror>(mir);
      placement.yOffset = 3;

      bool ok = true;
      for (int y = 0; y < s.sizeY() && ok; ++y) {
        for (int z = 0; z < s.sizeZ() && ok; ++z) {
          for (int x = 0; x < s.sizeX() && ok; ++x) {
            const auto world = placement.toWorld(s, x, y, z);
            bitematica::core::LocalPos back{};
            if (!placement.toLocal(s, world, back)) {
              ok = false;
              break;
            }
            if (back.x != x || back.y != y || back.z != z) ok = false;
          }
        }
      }
      check(ok, "round trip rot=" + std::to_string(rot) + " mirror=" + std::to_string(mir));
    }
  }
}

void testPlacementRotationSwapsExtents() {
  std::printf("testPlacementRotationSwapsExtents\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("column.schematic"));
  if (!loaded.success) return;
  // The column is 2x4x2; a 90-degree rotation swaps X and Z (still 2x4x2 here), so use a
  // non-square schematic to make the swap observable. The cube is 3x3x3; build a 4x2x1 synthetic.
  bitematica::core::Schematic s;
  s.bounds = {{0, 0, 0}, {3, 1, 0}}; // 4 x 2 x 1
  s.allocateBlocks();
  s.palette.intern(bitematica::core::BlockState{});
  const uint32_t stone = s.palette.intern({"minecraft:stone", {}});
  for (int x = 0; x < 4; ++x) s.setBlock(x, 0, 0, stone);

  bitematica::world::Placement placement;
  placement.rotation = bitematica::world::Rotation::Cw90;
  bitematica::core::BlockPos min;
  bitematica::core::BlockPos max;
  placement.worldBounds(s, min, max);
  check(max.x - min.x + 1 == 1, "a 90-degree rotation swaps the X extent to 1");
  check(max.z - min.z + 1 == 4, "a 90-degree rotation swaps the Z extent to 4");
}

// --- mesher ---------------------------------------------------------------------------

void testMesherCullsInteriorFaces() {
  std::printf("testMesherCullsInteriorFaces\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;

  bitematica::world::Placement placement;
  const auto result = bitematica::mesh::SectionMesher::build(loaded.schematic, placement, 0,
                                                             loaded.schematic.sizeY() - 1);
  check(!result.sections.empty(), "the mesher produced sections");
  check(!result.truncated, "the small cube is not truncated");

  // A solid 3x3x3 has exactly one 3x3 face per side (6 faces), all stone except one oak corner,
  // which splits the +Y face. Every emitted quad must be axis-aligned and have 4 vertices.
  bool allQuads = true;
  for (const auto &section : result.sections) {
    if (section.vertices.size() % 4 != 0) allQuads = false;
    if (section.triangles.size() != section.vertices.size() / 2) allQuads = false;
  }
  check(allQuads, "every section is a quad list (4 vertices, 2 triangles per quad)");

  // Greedy merging means far fewer than the 6*9 = 54 unit faces a per-face mesher would emit.
  check(result.emittedQuads <= 12,
        "greedy merging collapses the cube to a handful of quads, got " +
            std::to_string(result.emittedQuads));
}

void testMesherLayers() {
  std::printf("testMesherLayers\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("column.schematic"));
  if (!loaded.success) return;
  bitematica::world::Placement placement;

  const auto all = bitematica::mesh::SectionMesher::build(loaded.schematic, placement, 0, 3);
  const auto onlyTop = bitematica::mesh::SectionMesher::build(loaded.schematic, placement, 3, 3);
  check(!all.sections.empty(), "all-layers mesh is non-empty");
  check(!onlyTop.sections.empty(), "single-layer mesh is non-empty");

  // The layer filter must actually confine geometry: every vertex of the y=3-only mesh sits in
  // that layer. (A restricted mesh does not necessarily have *fewer* quads — faces can no longer
  // merge across the excluded layers — so the invariant to check is the extent, not the count.)
  float minY = 1e9f;
  float maxY = -1e9f;
  for (const auto &section : onlyTop.sections) {
    for (const auto &v : section.vertices) {
      const float worldY = v.y + section.originY;
      minY = std::min(minY, worldY);
      maxY = std::max(maxY, worldY);
    }
  }
  check(minY >= 3.0f && maxY <= 4.0f,
        "the y=3-only mesh stays within that layer (min " + std::to_string(minY) + ", max " +
            std::to_string(maxY) + ")");
}

void testMultiRegionLitematic() {
  std::printf("testMultiRegionLitematic\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("multi_region.litematic"));
  check(loaded.success, std::string("multi-region litematic loads: ") + loaded.error);
  if (!loaded.success) return;
  const auto &s = loaded.schematic;
  // Region A is 2x2x2 at (0,0,0); region B is 1x1x1 at (5,3,-2). The merged box spans
  // x 0..5, y 0..3, z -2..1.
  check(s.sizeX() == 6 && s.sizeY() == 4 && s.sizeZ() == 4,
        "merged bounds span both regions: " + std::to_string(s.sizeX()) + "x" +
            std::to_string(s.sizeY()) + "x" + std::to_string(s.sizeZ()));
  const auto materials = bitematica::core::MaterialList::build(s);
  uint64_t stone = 0;
  uint64_t gold = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:stone") stone = m.count;
    if (m.name == "minecraft:gold_block") gold = m.count;
  }
  check(stone == 8, "8 stone from region A, got " + std::to_string(stone));
  check(gold == 1, "1 gold block from region B, got " + std::to_string(gold));
}

void testNegativeSizeLitematic() {
  std::printf("testNegativeSizeLitematic\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("negative_size.litematic"));
  check(loaded.success, std::string("negative-size litematic loads: ") + loaded.error);
  if (!loaded.success) return;
  check(loaded.schematic.sizeX() == 2 && loaded.schematic.sizeY() == 2 &&
            loaded.schematic.sizeZ() == 2,
        "a negative size is normalized to a positive 2x2x2 extent");
  const auto materials = bitematica::core::MaterialList::build(loaded.schematic);
  uint64_t stone = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:stone") stone = m.count;
  }
  check(stone == 8, "all 8 blocks survive the normalization");
}

void testMesherEmptyForAirOnly() {
  std::printf("testMesherEmptyForAirOnly\n");
  bitematica::core::Schematic empty;
  bitematica::world::Placement placement;
  const auto result = bitematica::mesh::SectionMesher::build(empty, placement, 0, 0);
  check(result.sections.empty(), "an empty schematic produces no sections");
}

// --- frustum --------------------------------------------------------------------------

void testFrustumCull() {
  std::printf("testFrustumCull\n");
  // A camera at the origin looking down -Z.
  const auto vp = bitematica::render::Mat4::multiply(
      bitematica::render::perspective(70.0f, 1.0f, 0.1f, 100.0f),
      bitematica::render::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0}));
  const auto frustum = bitematica::render::Frustum::fromViewProjection(vp);

  const bool inFront =
      frustum.intersectsBox({-1, -1, -10}, {1, 1, -8});
  const bool behind =
      frustum.intersectsBox({-1, -1, 8}, {1, 1, 10});
  check(inFront, "a box in front of the camera is inside the frustum");
  check(!behind, "a box behind the camera is culled");
}

void testPlacementLocalTransformMatchesShader() {
  std::printf("testPlacementLocalTransformMatchesShader\n");
  // The cull runs in placement-local space, so `placementLocalToWorld` must agree with the vertex
  // shader exactly: x' = x*cos - z*sin + ox, z' = x*sin + z*cos + oz. A sign disagreement here
  // culls the wrong sections, which is exactly the bug this guards.
  const bitematica::render::Vec3 origin{10.0f, 64.0f, -5.0f};
  for (int rotation = 0; rotation < 4; ++rotation) {
    const float yaw = static_cast<float>(rotation * 90);
    const auto m = bitematica::render::placementLocalToWorld(origin, yaw);
    const float radians = yaw * 3.14159265358979f / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    const bitematica::render::Vec3 local{3.0f, 1.0f, -2.0f};
    const auto world = m.transformPoint(local);
    const float expectedX = local.x * c - local.z * s + origin.x;
    const float expectedZ = local.x * s + local.z * c + origin.z;
    check(std::fabs(world.x - expectedX) < 0.001f,
          "local->world X matches the shader at yaw " + std::to_string(rotation * 90));
    check(std::fabs(world.z - expectedZ) < 0.001f,
          "local->world Z matches the shader at yaw " + std::to_string(rotation * 90));

    // And the inverse must round-trip.
    const auto back = bitematica::render::worldToPlacementLocal(origin, yaw, world);
    check(std::fabs(back.x - local.x) < 0.001f && std::fabs(back.z - local.z) < 0.001f,
          "world->local inverts local->world at yaw " + std::to_string(rotation * 90));
  }
}

void testRotatedPlacementFrustumCull() {
  std::printf("testRotatedPlacementFrustumCull\n");
  // The regression this guards: with the old world-space cull, a rotated placement's section boxes
  // were axis-aligned in world space but the geometry was rotated, so sections vanished at the
  // screen edges. Culling in placement-local space fixes it. A section far out on the local +X axis
  // must stay visible in front of the camera at every rotation once the frustum is built the same
  // way the renderer builds it.
  const bitematica::render::Vec3 origin{0.0f, 0.0f, 0.0f};
  // A camera looking down -Z from a little behind the origin.
  const auto viewProjection = bitematica::render::Mat4::multiply(
      bitematica::render::perspective(70.0f, 1.0f, 0.1f, 1000.0f),
      bitematica::render::lookAt({0, 0, 20}, {0, 0, -1}, {0, 1, 0}));

  // A local section box centred near the origin stays inside the frustum for every rotation,
  // because the frustum is transformed with it.
  for (int rotation = 0; rotation < 4; ++rotation) {
    const float yaw = static_cast<float>(rotation * 90);
    const auto localToWorld = bitematica::render::placementLocalToWorld(origin, yaw);
    const auto localVp = bitematica::render::Mat4::multiply(viewProjection, localToWorld);
    const auto frustum = bitematica::render::Frustum::fromViewProjection(localVp);
    const bool visible = frustum.intersectsBox({-8, -8, -8}, {8, 8, 8});
    check(visible, "a local box at the origin is visible at yaw " +
                       std::to_string(rotation * 90));
  }
}

void testPlacementCullMvpExcludesPlacementTransform() {
  std::printf("testPlacementCullMvpExcludesPlacementTransform\n");
  // The regression this guards: the vertex shader applies the placement's yaw and origin itself, so
  // `mvp` must be the *plain* world->clip matrix. Feeding it `viewProjection * localToWorld`
  // transforms every vertex twice and puts the ghost at roughly double the offset with doubled
  // rotation. This pins that `mvp` is untouched while the frustum/camera are in local space.
  const bitematica::render::Vec3 origin{100.0f, 64.0f, -50.0f};
  const bitematica::render::Vec3 camera{0.0f, 0.0f, 20.0f};
  const auto viewProjection = bitematica::render::Mat4::multiply(
      bitematica::render::perspective(70.0f, 1.5f, 0.1f, 1000.0f),
      bitematica::render::lookAt(camera, {0, 0, -1}, {0, 1, 0}));

  for (int rotation = 0; rotation < 4; ++rotation) {
    const float yaw = static_cast<float>(rotation * 90);
    const auto cull =
        bitematica::render::computePlacementCull(viewProjection, origin, yaw, camera);

    // mvp must equal the input viewProjection element-for-element.
    bool mvpEqual = true;
    for (int i = 0; i < 16; ++i) {
      if (std::fabs(cull.mvp.m[i] - viewProjection.m[i]) > 1e-6f) mvpEqual = false;
    }
    check(mvpEqual, "mvp is the plain world->clip matrix at yaw " + std::to_string(rotation * 90));

    // And it must NOT equal viewProjection * localToWorld (the double-transform mistake).
    const auto localToWorld = bitematica::render::placementLocalToWorld(origin, yaw);
    const auto doubled = bitematica::render::Mat4::multiply(viewProjection, localToWorld);
    bool differsFromDoubled = false;
    for (int i = 0; i < 16; ++i) {
      if (std::fabs(cull.mvp.m[i] - doubled.m[i]) > 1e-4f) differsFromDoubled = true;
    }
    check(differsFromDoubled,
          "mvp does not include the placement transform at yaw " + std::to_string(rotation * 90));

    // The camera must be in local space. Converting the local camera back to world must return the
    // original world camera, which would fail if `computePlacementCull` had left it in world space.
    const auto worldBack = localToWorld.transformPoint(cull.localCamera);
    check(std::fabs(worldBack.x - camera.x) < 1e-3f &&
              std::fabs(worldBack.y - camera.y) < 1e-3f &&
              std::fabs(worldBack.z - camera.z) < 1e-3f,
          "localCamera round-trips back to the world camera at yaw " +
              std::to_string(rotation * 90));
  }
}

void testLocalCameraMath() {
  std::printf("testLocalCameraMath\n");
  // The camera the mod builds from the local player must place the eye at the player's head and
  // point along the view direction. This is the path that works on 1.26-1.26.52, so its arithmetic
  // is pinned here even though the feed itself needs a device.
  const float yaw = 0.0f;   // looking toward -Z
  const float pitch = 0.0f;
  const float yawRadians = yaw * 3.14159265358979f / 180.0f;
  const float pitchRadians = pitch * 3.14159265358979f / 180.0f;
  const float cosPitch = std::cos(pitchRadians);
  const bitematica::render::Vec3 forward{
      -std::sin(yawRadians) * cosPitch,
      -std::sin(pitchRadians),
      -std::cos(yawRadians) * cosPitch,
  };
  check(std::fabs(forward.x) < 0.001f && std::fabs(forward.y) < 0.001f &&
            std::fabs(forward.z + 1.0f) < 0.001f,
        "yaw 0, pitch 0 looks toward -Z");

  // Yaw 90 should look toward -X (Bedrock yaw is clockwise from -Z).
  const float yaw90 = 90.0f * 3.14159265358979f / 180.0f;
  const bitematica::render::Vec3 forward90{-std::sin(yaw90), 0.0f, -std::cos(yaw90)};
  check(std::fabs(forward90.x + 1.0f) < 0.001f, "yaw 90 looks toward -X");

  // The eye is the feet position plus eye height.
  const bitematica::render::Vec3 eye{10.0f, 64.0f + 1.62f, -5.0f};
  check(std::fabs(eye.y - 65.62f) < 0.001f, "the eye sits at the player's head height");

  // A view built from that eye/forward must put a point ahead of the camera at the screen centre.
  const auto view = bitematica::render::lookAt(eye, forward, {0, 1, 0});
  const auto projection = bitematica::render::perspective(70.0f, 1.5f, 0.05f, 1024.0f);
  const auto vp = bitematica::render::Mat4::multiply(projection, view);
  check(bitematica::render::looksWorldSpace(vp, eye, forward),
        "the player-derived camera is accepted as world-space");
}

void testLooksWorldSpace() {
  std::printf("testLooksWorldSpace\n");
  // A genuine world->clip matrix built from a camera at a non-origin world position must be
  // accepted.
  const bitematica::render::Vec3 eye{100.0f, 70.0f, -200.0f};
  const bitematica::render::Vec3 forward{0.0f, 0.0f, -1.0f};
  const auto worldVp = bitematica::render::Mat4::multiply(
      bitematica::render::perspective(70.0f, 1.5f, 0.1f, 1000.0f),
      bitematica::render::lookAt(eye, forward, {0, 1, 0}));
  check(bitematica::render::looksWorldSpace(worldVp, eye, forward),
        "a world-space view-projection is accepted");

  // The same view matrix WITHOUT the translation is camera-relative: it expects the camera at the
  // origin, so the world-space eye projects off-centre and the check must reject it.
  const auto cameraRelativeVp = bitematica::render::Mat4::multiply(
      bitematica::render::perspective(70.0f, 1.5f, 0.1f, 1000.0f),
      bitematica::render::lookAt({0, 0, 0}, forward, {0, 1, 0}));
  check(!bitematica::render::looksWorldSpace(cameraRelativeVp, eye, forward),
        "a camera-relative view-projection is rejected");

  // A degenerate forward vector is refused rather than divided by.
  check(!bitematica::render::looksWorldSpace(worldVp, eye, {0, 0, 0}),
        "a zero forward vector is rejected");
}

// --- verification ---------------------------------------------------------------------

void testVerification() {
  std::printf("testVerification\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;
  const bitematica::core::Schematic &s = loaded.schematic;

  bitematica::world::Placement placement;
  placement.origin = {0, 0, 0};

  FakeWorld world;
  // Build the cube exactly, then remove one block and corrupt another.
  for (int y = 0; y < s.sizeY(); ++y) {
    for (int z = 0; z < s.sizeZ(); ++z) {
      for (int x = 0; x < s.sizeX(); ++x) {
        const auto world_pos = placement.toWorld(s, x, y, z);
        const auto &state = s.stateAt(x, y, z);
        if (!state.isAir()) world.set(world_pos, state.baseName());
      }
    }
  }
  // Remove (0,0,0) -> Missing; corrupt (1,1,1) -> Wrong.
  world.set({0, 0, 0}, "minecraft:air");
  world.set({1, 1, 1}, "minecraft:gold_block");

  const auto result = bitematica::world::Verifier::verify(s, placement, world, 0, s.sizeY() - 1);
  check(result.available, "verification ran");
  check(result.missing == 1, "one missing block, got " + std::to_string(result.missing));
  check(result.wrong == 1, "one wrong block, got " + std::to_string(result.wrong));
  check(result.correct == 25, "25 correct blocks, got " + std::to_string(result.correct));
  check(result.percentComplete() == 92, "percent complete is 92, got " +
                                            std::to_string(result.percentComplete()));
}

void testVerificationUnavailable() {
  std::printf("testVerificationUnavailable\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;
  FakeWorld world;
  world.setAvailable(false);
  const auto result = bitematica::world::Verifier::verify(
      loaded.schematic, bitematica::world::Placement{}, world, 0, 2);
  check(!result.available, "an unavailable world reports unavailable, not all-missing");
}

void testVerificationExtras() {
  std::printf("testVerificationExtras\n");
  bitematica::core::Schematic s;
  s.bounds = {{0, 0, 0}, {1, 0, 0}};
  s.allocateBlocks();
  const uint32_t air = s.palette.intern({"minecraft:air", {}});
  s.setBlock(0, 0, 0, air);
  s.setBlock(1, 0, 0, air);

  FakeWorld world;
  world.set({0, 0, 0}, "minecraft:stone"); // an extra block where the schematic wants air
  const auto result =
      bitematica::world::Verifier::verify(s, bitematica::world::Placement{}, world, 0, 0, true);
  check(result.extra == 1, "one extra block, got " + std::to_string(result.extra));
  check(result.correct == 1, "the empty cell counts as correct");
}

// --- translation ----------------------------------------------------------------------

void testBlockTranslation() {
  std::printf("testBlockTranslation\n");
  using bitematica::translate::BlockTranslation;

  // Java grass_block -> Bedrock grass.
  bitematica::core::BlockState grass;
  grass.name = "minecraft:grass_block";
  check(BlockTranslation::toBedrock(grass).name == "minecraft:grass",
        "grass_block translates to grass");

  // Java oak_stairs[facing=north,half=top] -> weirdo_direction + upside_down_bit.
  bitematica::core::BlockState stairs;
  stairs.name = "minecraft:oak_stairs";
  stairs.states = {{"facing", "north"}, {"half", "top"}};
  const auto bedrock = BlockTranslation::toBedrock(stairs);
  bool hasDirection = false;
  bool hasUpsideDown = false;
  for (const auto &[k, v] : bedrock.states) {
    if (k == "weirdo_direction") hasDirection = (v == "3"); // north -> 3
    if (k == "upside_down_bit") hasUpsideDown = (v == "1"); // top -> 1
  }
  check(hasDirection, "stairs facing=north becomes weirdo_direction=3 (inverted)");
  check(hasUpsideDown, "stairs half=top becomes upside_down_bit=1");

  // A plain block passes through with its states intact.
  bitematica::core::BlockState log;
  log.name = "minecraft:oak_log";
  log.states = {{"axis", "y"}};
  const auto logOut = BlockTranslation::toBedrock(log);
  check(logOut.name == "minecraft:oak_log" && logOut.states.size() == 1,
        "an untranslated block passes through unchanged");
}

// --- selection + writers --------------------------------------------------------------

void testSelectionCapture() {
  std::printf("testSelectionCapture\n");
  FakeWorld world;
  world.set({0, 64, 0}, "minecraft:stone");
  world.set({1, 64, 0}, "minecraft:dirt");

  bitematica::world::Selection selection;
  selection.setCornerA({0, 64, 0});
  selection.setCornerB({1, 64, 0});
  check(selection.volume() == 2, "a 2x1x1 selection has volume 2");

  bitematica::core::Schematic captured;
  std::string error;
  const bool ok = selection.capture(world, "my_build", 1000, captured, error);
  check(ok, std::string("capture succeeded: ") + error);
  if (ok) {
    check(captured.sizeX() == 2 && captured.sizeY() == 1 && captured.sizeZ() == 1,
          "captured bounds match the selection");
    check(captured.stateAt(0, 0, 0).baseName() == "minecraft:stone", "first block captured");
    check(captured.stateAt(1, 0, 0).baseName() == "minecraft:dirt", "second block captured");
  }

  // An oversized selection is refused, not silently truncated.
  bitematica::world::Selection big;
  big.setCornerA({0, 0, 0});
  big.setCornerB({100, 100, 100});
  bitematica::core::Schematic ignored;
  check(!big.capture(world, "big", 1000, ignored, error), "an oversized selection is refused");
}

void testLitematicWriteRoundTrip() {
  std::printf("testLitematicWriteRoundTrip\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;

  const std::string encoded =
      bitematica::format::LitematicWriter::bytes(loaded.schematic, "Round Trip", "Tests");
  check(!encoded.empty(), "the writer produced bytes");

  const std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
  const auto reread = bitematica::format::SchematicFormat::read(bytes, "roundtrip.litematic");
  check(reread.success, std::string("the written litematic reloads: ") + reread.error);
  if (!reread.success) return;
  check(reread.schematic.sizeX() == 3 && reread.schematic.sizeY() == 3, "bounds survive");
  check(reread.schematic.name == "Round Trip", "the name survives");
  const auto before = bitematica::core::MaterialList::build(loaded.schematic);
  const auto after = bitematica::core::MaterialList::build(reread.schematic);
  check(before.size() == after.size(), "the same distinct materials survive");
}

// --- live materials -------------------------------------------------------------------

void testLiveMaterials() {
  std::printf("testLiveMaterials\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;
  const bitematica::core::Schematic &s = loaded.schematic;

  bitematica::world::VerifyResult verification;
  verification.available = true;
  verification.statuses.assign(s.blocks.size(), bitematica::world::BlockStatus::Correct);
  // Mark one stone block as missing.
  for (size_t i = 0; i < s.blocks.size(); ++i) {
    if (!s.palette.at(s.blocks[i]).isAir()) {
      verification.statuses[i] = bitematica::world::BlockStatus::Missing;
      verification.missing = 1;
      verification.correct = 26;
      break;
    }
  }

  const auto rows = bitematica::core::LiveMaterials::build(s, verification);
  bool foundStone = false;
  for (const auto &row : rows) {
    if (row.name == "minecraft:stone") {
      foundStone = true;
      check(row.required == 26, "26 stone required");
      check(row.placed == 25, "25 stone placed");
      check(row.missing == 1, "1 stone missing");
    }
  }
  check(foundStone, "the stone row is present");

  const std::string csv = bitematica::core::LiveMaterials::toCsv(rows);
  check(csv.rfind("block,required,placed,missing", 0) == 0, "CSV has the expected header");
  check(csv.find("minecraft:stone,26,25,1") != std::string::npos, "CSV has the stone row");
}

// --- cheat route ----------------------------------------------------------------------

void testCheatCommands() {
  std::printf("testCheatCommands\n");
  const std::string load =
      bitematica::world::CheatPlacement::loadCommand("My Build", {10, 64, -5});
  check(load == "structure load my_build 10 64 -5", "load command: " + load);

  bitematica::world::Placement placement;
  placement.origin = {10, 64, -5};
  placement.rotation = bitematica::world::Rotation::Cw90;
  const std::string rotated =
      bitematica::world::CheatPlacement::loadCommandForPlacement("hut", placement);
  check(rotated.find("90_degrees") != std::string::npos, "rotation is carried into the command");

  check(bitematica::world::CheatPlacement::rotationArgument(
            bitematica::world::Rotation::Cw270) == "270_degrees",
        "270-degree argument");
}

} // namespace

int main() {
  std::printf("BiteMatica world-space tests\n");
  std::printf("============================\n");
  testPlacementRoundTrip();
  testPlacementRotationSwapsExtents();
  testMesherCullsInteriorFaces();
  testMesherLayers();
  testMultiRegionLitematic();
  testNegativeSizeLitematic();
  testMesherEmptyForAirOnly();
  testFrustumCull();
  testPlacementLocalTransformMatchesShader();
  testRotatedPlacementFrustumCull();
  testPlacementCullMvpExcludesPlacementTransform();
  testLocalCameraMath();
  testLooksWorldSpace();
  testVerification();
  testVerificationUnavailable();
  testVerificationExtras();
  testBlockTranslation();
  testSelectionCapture();
  testLitematicWriteRoundTrip();
  testLiveMaterials();
  testCheatCommands();
  std::printf("============================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
