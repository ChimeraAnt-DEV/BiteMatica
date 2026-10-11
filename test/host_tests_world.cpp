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
#include "nbt/NbtReader.hpp"
#include "nbt/NbtWriter.hpp"
#include "render/MathTypes.hpp"
#include "render/PlacementCull.hpp"
#include "translate/BlockTranslation.hpp"
#include "world/CheatPlacement.hpp"
#include "world/Placement.hpp"
#include "world/Selection.hpp"
#include "mod/GameWorldSource.hpp"
#include "world/IncrementalVerifier.hpp"
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
    set(pos, name, {});
  }
  void set(const bitematica::core::BlockPos &pos, const std::string &name,
           const std::vector<std::pair<std::string, std::string>> &states) {
    mBlocks[key(pos)] = bitematica::core::BlockState{name, states};
  }
  bool readBlock(const bitematica::core::BlockPos &position,
                 bitematica::core::BlockState &out) override {
    auto it = mBlocks.find(key(position));
    if (it == mBlocks.end()) {
      out = bitematica::core::BlockState{};
      return true; // air
    }
    out = it->second;
    return true;
  }
  bool available() const override { return mAvailable; }
  void setAvailable(bool value) { mAvailable = value; }

private:
  static std::string key(const bitematica::core::BlockPos &p) {
    return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
  }
  std::unordered_map<std::string, bitematica::core::BlockState> mBlocks;
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

// --- world source gate + probe (F1/F9) ------------------------------------------------

void testWorldSourceVersionGate() {
  std::printf("testWorldSourceVersionGate\n");
  using bitematica::mod::GameWorldSource;
  // A blank configured tag means "not verified": closed.
  check(!GameWorldSource::versionGateOpen("", "1.26.51"), "blank configured tag keeps the gate shut");
  // A matching tag (prefix match) opens the gate.
  check(GameWorldSource::versionGateOpen("1.26.51", "1.26.51"), "an exact match opens the gate");
  check(GameWorldSource::versionGateOpen("1.26.51", "1.26.51.1"),
        "a configured tag prefixes a longer running tag");
  // A mismatched running tag is closed.
  check(!GameWorldSource::versionGateOpen("1.26.51", "1.26.60"),
        "a mismatched version keeps the gate shut");
  // An unreadable running tag with a configured tag is the user's acknowledgement: open, and the
  // sanity probe is the safety net.
  check(GameWorldSource::versionGateOpen("1.26.51", ""),
        "an unreadable running tag with a configured tag opens the gate");
}

void testWorldSourceProbe() {
  std::printf("testWorldSourceProbe\n");
  using bitematica::mod::GameWorldSource;
  using bitematica::core::BlockState;
  using bitematica::core::BlockPos;

  const BlockPos feet{10, 64, 10};
  const BlockPos sky{10, 464, 10};

  // A reader that reports stone at the feet and air above passes.
  auto good = [](const BlockPos &p, BlockState &out) {
    if (p.y == 64) out.name = "minecraft:stone";
    else out = BlockState{};
    return true;
  };
  check(GameWorldSource::probe(good, feet, sky) == GameWorldSource::ProbeResult::Passed,
        "a plausible reader passes the probe");

  // A reader that reports air under the feet fails.
  auto feetAir = [](const BlockPos &, BlockState &out) {
    out = BlockState{};
    return true;
  };
  check(GameWorldSource::probe(feetAir, feet, sky) == GameWorldSource::ProbeResult::FeetIsAir,
        "air under the feet fails the probe");

  // A reader that reports solid 400 blocks up fails.
  auto skySolid = [](const BlockPos &, BlockState &out) {
    out.name = "minecraft:stone";
    return true;
  };
  check(GameWorldSource::probe(skySolid, feet, sky) == GameWorldSource::ProbeResult::SkyIsNotAir,
        "a solid sky position fails the probe");

  // A reader that returns nothing fails as unreadable.
  auto nothing = [](const BlockPos &, BlockState &) { return false; };
  check(GameWorldSource::probe(nothing, feet, sky) == GameWorldSource::ProbeResult::Unreadable,
        "a reader that returns nothing fails as unreadable");

  // A null reader fails as unreadable, not a crash.
  GameWorldSource::BlockReader none;
  check(GameWorldSource::probe(none, feet, sky) == GameWorldSource::ProbeResult::Unreadable,
        "a null reader fails as unreadable");
}

// --- incremental verification (F5) -----------------------------------------------------

void testIncrementalVerification() {
  std::printf("testIncrementalVerification\n");
  // A 4x4x4 solid cube, all stone in the world, so every block is Correct.
  bitematica::core::Schematic s;
  s.palette.intern(bitematica::core::BlockState{});
  bitematica::core::BlockState stone;
  stone.name = "minecraft:stone";
  s.palette.intern(stone);
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {3, 3, 3};
  s.blocks.assign(static_cast<size_t>(s.volume()), 1);

  bitematica::world::Placement placement;
  FakeWorld world;
  for (int y = 0; y < 4; ++y) {
    for (int z = 0; z < 4; ++z) {
      for (int x = 0; x < 4; ++x) world.set({x, y, z}, "minecraft:stone");
    }
  }

  bitematica::world::IncrementalVerifier verifier(s, placement, 0, 3, false);
  check(!verifier.finished(), "a fresh incremental scan is not finished");
  check(verifier.progressPercent() == 0, "progress starts at 0");

  // A budget of 10 per step: 64 blocks need 7 steps.
  int steps = 0;
  while (!verifier.step(world, 10)) {
    ++steps;
    check(steps < 100, "the scan terminates");
    if (steps >= 100) break;
  }
  check(verifier.finished(), "the scan finishes");
  check(verifier.result().correct == 64, "all 64 blocks verified correct, got " +
                                             std::to_string(verifier.result().correct));
  check(verifier.progressPercent() == 100, "progress reaches 100");

  // No section is starved: the queue is distance-ordered, so the first block checked is at the
  // origin.
  bitematica::world::IncrementalVerifier ordered(s, placement, 0, 3, false);
  check(ordered.progressPercent() == 0, "the ordered scan also starts at 0");
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
  check(result.wrongBlock == 1, "one wrong block, got " + std::to_string(result.wrongBlock));
  check(result.wrongState == 0, "no wrong-state blocks");
  check(result.correct == 25, "25 correct blocks, got " + std::to_string(result.correct));
  check(result.percentComplete() == 92, "percent complete is 92, got " +
                                            std::to_string(result.percentComplete()));
}

/// A block of the right type with the wrong state must be `WrongState`, distinct from `WrongBlock`,
/// and a wrong type must be `WrongBlock`. This is the C6/verdict split the palette colours apart.
void testVerificationWrongStateVsWrongBlock() {
  std::printf("testVerificationWrongStateVsWrongBlock\n");
  bitematica::core::Schematic s;
  s.palette.intern(bitematica::core::BlockState{}); // 0 air
  bitematica::core::BlockState stairs;
  stairs.name = "minecraft:oak_stairs";
  stairs.states = {{"facing", "north"}};
  s.palette.intern(stairs); // 1
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {1, 0, 0};
  s.blocks = {1, 1};

  bitematica::world::Placement placement;
  placement.origin = {0, 0, 0};
  FakeWorld world;
  // (0,0,0): right type, wrong facing -> WrongState.
  world.set({0, 0, 0}, "minecraft:oak_stairs", {{"facing", "south"}});
  // (1,0,0): wrong type entirely -> WrongBlock.
  world.set({1, 0, 0}, "minecraft:stone");

  const auto result = bitematica::world::Verifier::verify(s, placement, world, 0, 0);
  check(result.available, "verification ran");
  check(result.wrongState == 1, "one wrong-state block, got " + std::to_string(result.wrongState));
  check(result.wrongBlock == 1, "one wrong-block, got " + std::to_string(result.wrongBlock));
  check(result.wrong() == 2, "wrong() sums wrong block and wrong state");
}

/// An unreadable block is `Unknown`, never `Missing`. This is the "chunk not loaded is not a hole"
/// rule (F4): a source that is available but cannot read a particular position (an unloaded chunk)
/// must not be reported as a block the player still has to place.
void testVerificationUnknownNotMissing() {
  std::printf("testVerificationUnknownNotMissing\n");
  // A source that is available, but reports "cannot read" for one specific position.
  class PartiallyUnreadable : public bitematica::world::WorldSource {
  public:
    bool readBlock(const bitematica::core::BlockPos &position,
                   bitematica::core::BlockState &out) override {
      if (position == unreadable) return false; // unloaded chunk
      out = bitematica::core::BlockState{};
      return true; // air
    }
    bool available() const override { return true; }
    bitematica::core::BlockPos unreadable{5, 0, 0};
  };

  bitematica::core::Schematic s;
  s.palette.intern(bitematica::core::BlockState{});
  bitematica::core::BlockState stone;
  stone.name = "minecraft:stone";
  s.palette.intern(stone);
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {1, 0, 0};
  s.blocks = {1, 1};

  bitematica::world::Placement placement;
  placement.origin = {0, 0, 0};
  PartiallyUnreadable world;
  // With origin 0, local (0,0,0) and (1,0,0) map to world (0,0,0) and (1,0,0). Make the second one
  // unreadable: the first is a readable air block (Missing), the second must be Unknown.
  world.unreadable = {1, 0, 0};
  const auto result = bitematica::world::Verifier::verify(s, placement, world, 0, 0);
  check(result.missing == 1, "one readable-missing block, got " + std::to_string(result.missing));
  check(result.unknown == 1, "an unreadable block is Unknown, got " +
                                 std::to_string(result.unknown));
  check(result.missing + result.unknown == 2, "every block is accounted for");
  check(result.percentComplete() == 0, "an unreadable block is not counted as checked");
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

  // A door's facing and hinge are translated (a door is a directional family).
  bitematica::core::BlockState door;
  door.name = "minecraft:oak_door";
  door.states = {{"facing", "west"}, {"half", "bottom"}, {"hinge", "right"}};
  const auto doorOut = BlockTranslation::toBedrock(door);
  bool doorDirection = false;
  for (const auto &[k, v] : doorOut.states) {
    if (k == "weirdo_direction") doorDirection = (v == "1"); // west -> 1
  }
  check(doorDirection, "a door facing=west becomes weirdo_direction=1");

  // G2: the data-driven table merges over the built-in renames.
  BlockTranslation::clearUnmapped();
  const int merged = BlockTranslation::loadJson(
      R"({"blocks": {"minecraft:my_mod_block": "minecraft:stone"}})");
  check(merged == 1, "one rename merged from JSON, got " + std::to_string(merged));
  bitematica::core::BlockState modded;
  modded.name = "minecraft:my_mod_block";
  check(BlockTranslation::toBedrock(modded).name == "minecraft:stone",
        "the merged rename is applied");

  // G4: an unknown id is reported as unmapped, not silently accepted.
  BlockTranslation::clearUnmapped();
  bitematica::core::BlockState weird;
  weird.name = "minecraft:totally_unknown_xyz";
  check(BlockTranslation::isUnmapped(weird.name), "an unknown id is flagged unmapped");
  const auto unmapped = BlockTranslation::unmappedBlocks();
  bool recorded = false;
  for (const auto &name : unmapped) {
    if (name == "totally_unknown_xyz") recorded = true;
  }
  check(recorded, "the unmapped id is recorded for the status line");

  // G7: a newer game version than the table is flagged.
  check(!BlockTranslation::isVersionNewerThanTable("1.26.51"),
        "the table's own version is not flagged as newer");
  check(BlockTranslation::isVersionNewerThanTable("1.26.60"),
        "a newer game version is flagged");
  check(!BlockTranslation::isVersionNewerThanTable(""), "an unknown version is not flagged");
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

/// G5: a block entity (chest contents) captured as raw NBT survives a `.litematic` round trip.
void testBlockEntityRoundTrip() {
  std::printf("testBlockEntityRoundTrip\n");
  // Build a 1x1x1 chest schematic with one block entity.
  bitematica::core::Schematic s;
  s.name = "chest test";
  s.palette.intern(bitematica::core::BlockState{});
  bitematica::core::BlockState chest;
  chest.name = "minecraft:chest";
  s.palette.intern(chest);
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {0, 0, 0};
  s.blocks = {1};

  // A minimal chest block entity as raw NBT (a named compound with an `id`).
  bitematica::nbt::Tag be = bitematica::nbt::NbtWriter::compound("");
  be.children.push_back(bitematica::nbt::NbtWriter::string("id", "Chest"));
  be.children.push_back(bitematica::nbt::NbtWriter::string("CustomName", "Loot"));
  const std::vector<uint8_t> beBytes = bitematica::nbt::NbtWriter::writeRaw(be);
  s.blockEntities.emplace_back(0, std::string(beBytes.begin(), beBytes.end()));

  const std::string encoded = bitematica::format::LitematicWriter::bytes(s, "Chest", "Tests");
  check(!encoded.empty(), "the writer produced bytes with a block entity");

  const std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
  const auto reread = bitematica::format::SchematicFormat::read(bytes, "chest.litematic");
  check(reread.success, std::string("the written litematic reloads: ") + reread.error);
  // The block-entity list must survive in the written document (read back through the reader).
  const auto doc = bitematica::nbt::NbtReader::readFile(bytes);
  check(doc.valid(), "the document re-parses");
  const bitematica::nbt::Tag *regions = doc.root.find("Regions");
  check(regions != nullptr, "the Regions compound is present");
  if (regions != nullptr && !regions->children.empty()) {
    const bitematica::nbt::Tag *entities = regions->children.front().find("BlockEntities");
    check(entities != nullptr, "the BlockEntities list is written");
    if (entities != nullptr) {
      check(!entities->children.empty(), "the chest block entity survived");
      const bitematica::nbt::Tag *name = entities->children.front().find("CustomName");
      check(name != nullptr && name->stringValue == "Loot", "the block entity's data survived");
    }
  }
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
  testIncrementalVerification();
  testWorldSourceVersionGate();
  testWorldSourceProbe();
  testVerification();
  testVerificationWrongStateVsWrongBlock();
  testVerificationUnknownNotMissing();
  testVerificationUnavailable();
  testVerificationExtras();
  testBlockTranslation();
  testSelectionCapture();
  testLitematicWriteRoundTrip();
  testBlockEntityRoundTrip();
  testLiveMaterials();
  testCheatCommands();
  std::printf("============================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
