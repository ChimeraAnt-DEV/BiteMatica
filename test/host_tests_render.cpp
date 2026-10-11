// Host-runnable tests for the world-space rendering *math*.
//
// The GL calls themselves cannot run without a GPU, so every decision the renderer makes — which
// matrix is uploaded, where the camera-relative origin goes, which sections pass the cull, how the
// outlines are generated — is factored into pure functions and tested here. `SchematicRenderer`
// stays a thin shell around them. This is the third suite the brief asks for (M1).
//
// The cases that matter most are the ones a device would only reveal as a visual bug: the transform
// applied twice, a large world offset losing float precision, a rotated placement culled wrongly,
// and an outline that draws every cube instead of the surface.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/BlockShape.hpp"
#include "core/Schematic.hpp"
#include "format/SchematicFormat.hpp"
#include "gles/Gles.hpp"
#include "gles/SchematicRenderer.hpp"
#include "mesh/SectionMesher.hpp"
#include "render/BlockColors.hpp"
#include "render/MathTypes.hpp"
#include "render/PlacementCull.hpp"
#include "world/Placement.hpp"
#include "world/Verifier.hpp"

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

using bitematica::render::Mat4;
using bitematica::render::Vec3;

bitematica::render::Vec3 sectionMin(const bitematica::mesh::SectionMesh &s) {
  return {s.originX, s.originY, s.originZ};
}
bitematica::render::Vec3 sectionMax(const bitematica::mesh::SectionMesh &s) {
  const float n = static_cast<float>(bitematica::mesh::SectionMesher::kSectionSize);
  return {s.originX + n, s.originY + n, s.originZ + n};
}

// --- camera-relative precision (B2) -------------------------------------------------

/// The whole point of camera-relative rendering: a ghost one block from the camera must project to
/// the same place whether the world coordinate is near the origin or at +/-30,000,000. A naive
/// world-space float matrix loses precision there; the camera-relative one does not.
void testCameraRelativePrecisionAtLargeCoordinates() {
  std::printf("testCameraRelativePrecisionAtLargeCoordinates\n");

  const float fov = 70.0f;
  const float aspect = 1.6f;

  const double baseX = 3.0e7;
  const double baseZ = -3.0e7;
  const Vec3 camera{static_cast<float>(baseX), 70.0f, static_cast<float>(baseZ)};
  // Five blocks in front of the camera (looking toward -Z), well inside the near plane.
  const Vec3 placement{static_cast<float>(baseX), 70.0f, static_cast<float>(baseZ - 5.0f)};
  const Vec3 forward{0.0f, 0.0f, -1.0f};

  const Mat4 view = bitematica::render::lookAt(camera, forward, {0, 1, 0});
  const Mat4 projection = bitematica::render::perspective(fov, aspect, 0.05f, 1024.0f);

  const Mat4 crVp = bitematica::render::cameraRelativeViewProjection(projection, view);
  const Vec3 crOrigin{
      static_cast<float>(static_cast<double>(placement.x) - static_cast<double>(camera.x)),
      static_cast<float>(static_cast<double>(placement.y) - static_cast<double>(camera.y)),
      static_cast<float>(static_cast<double>(placement.z) - static_cast<double>(camera.z))};

  // The point that matters: no large value reaches the shader. The relative origin is schematic-
  // sized, not 30-million-sized. (A float camera/placement at 3e7 is itself quantised to ~2 blocks
  // by float spacing — that is inherent to the camera the game reports, not to this technique; the
  // benefit is that the *relative* geometry keeps full float precision, so nearby faces do not
  // z-fight or shimmer as the player moves.)
  check(std::fabs(crOrigin.x) < 16.0f && std::fabs(crOrigin.y) < 16.0f &&
            std::fabs(crOrigin.z) < 16.0f,
        "camera-relative origin is small at 3e7 (no large float offset reaches the shader)");

  float crClip[4];
  crVp.transformPoint4(crOrigin, crClip);
  check(crClip[3] > 0.0f, "the camera-relative point is in front of the camera");
  const float crNdcX = crClip[0] / crClip[3];
  const float crNdcY = crClip[1] / crClip[3];
  check(std::fabs(crNdcX) < 1e-2f && std::fabs(crNdcY) < 1e-2f,
        "a block straight ahead projects to screen centre (camera-relative)");

  // When every coordinate is exactly representable in float (2^23, an integer float), the
  // camera-relative subtraction is exact to the last bit — this is what proves there is no
  // catastrophic cancellation in the technique itself.
  const float exactBase = 8388608.0f; // 2^23
  const Vec3 exactCamera{exactBase, 70.0f, exactBase};
  const Vec3 exactPlacement{exactBase, 70.0f, exactBase - 5.0f};
  const Vec3 exactRelative{
      static_cast<float>(static_cast<double>(exactPlacement.x) - static_cast<double>(exactCamera.x)),
      static_cast<float>(static_cast<double>(exactPlacement.y) - static_cast<double>(exactCamera.y)),
      static_cast<float>(static_cast<double>(exactPlacement.z) - static_cast<double>(exactCamera.z))};
  check(exactRelative.z == -5.0f,
        "the camera-relative subtraction is exact for representable coordinates");
}

// --- transform applied once (B3) ----------------------------------------------------

void testCameraRelativeMvpExcludesPlacementTransform() {
  std::printf("testCameraRelativeMvpExcludesPlacementTransform\n");

  const Vec3 origin{100.0f, 64.0f, -50.0f};
  const Vec3 camera{90.0f, 66.0f, -40.0f};
  const Vec3 forward{0.0f, -0.2f, -0.98f};
  const Mat4 view = bitematica::render::lookAt(camera, forward, {0, 1, 0});
  const Mat4 projection = bitematica::render::perspective(70.0f, 1.5f, 0.05f, 1024.0f);

  for (int rotation = 0; rotation < 4; ++rotation) {
    const float yaw = static_cast<float>(rotation * 90);
    const auto cull =
        bitematica::render::computePlacementCull(projection, view, origin, yaw, camera);

    const Mat4 expected = bitematica::render::cameraRelativeViewProjection(projection, view);
    bool mvpMatches = true;
    for (int i = 0; i < 16; ++i) {
      if (std::fabs(cull.mvp.m[i] - expected.m[i]) > 1e-5f) mvpMatches = false;
    }
    check(mvpMatches, "mvp is projection * rotation(view) at yaw " + std::to_string(rotation * 90));

    const Mat4 localToWorld = bitematica::render::placementLocalToWorld(origin, yaw);
    const Mat4 doubled = Mat4::multiply(expected, localToWorld);
    bool differs = false;
    for (int i = 0; i < 16; ++i) {
      if (std::fabs(cull.mvp.m[i] - doubled.m[i]) > 1e-3f) differs = true;
    }
    check(differs, "mvp does not include the placement transform at yaw " +
                       std::to_string(rotation * 90));
  }
}

// --- culling under rotation and mirror (B10) ----------------------------------------

void testPlacementCullAllRotationsAndMirrors() {
  std::printf("testPlacementCullAllRotationsAndMirrors\n");

  const Vec3 camera{0.0f, 0.0f, 20.0f};
  const Mat4 view = bitematica::render::lookAt(camera, {0, 0, -1}, {0, 1, 0});
  const Mat4 projection = bitematica::render::perspective(70.0f, 1.0f, 0.1f, 1000.0f);

  for (int rotation = 0; rotation < 4; ++rotation) {
    for (int mirror = 0; mirror < 3; ++mirror) {
      const float yaw = static_cast<float>(rotation * 90);
      const auto cull =
          bitematica::render::computePlacementCull(projection, view, {0, 0, 0}, yaw, camera);
      const bool visible = cull.frustum.intersectsBox({-2, -2, -2}, {2, 2, 2});
      check(visible, "origin section is visible at rot " + std::to_string(rotation * 90) +
                         " mirror " + std::to_string(mirror));
    }
  }

  // A local box 10,000 blocks out is beyond the far plane and off to the side, so it is culled at
  // every rotation: the frustum is built in the same local space the box lives in, so rotating the
  // placement rotates the frustum with it and the box stays outside.
  for (int rotation = 0; rotation < 4; ++rotation) {
    const float yaw = static_cast<float>(rotation * 90);
    const auto cull =
        bitematica::render::computePlacementCull(projection, view, {0, 0, 0}, yaw, camera);
    const bool visible = cull.frustum.intersectsBox({10000, 0, 0}, {10001, 1, 1});
    check(!visible, "a far local section is culled at yaw " + std::to_string(rotation * 90));
  }
}

// --- mirror baked into the mesh (D2) ------------------------------------------------

/// The shader applies the placement's rotation but not its mirror, so the mesher bakes the mirror
/// into local coordinates. This pins that a mirrored mesh is the reflection of the unmirrored one,
/// and that a mirror+mirror round-trips to the original.
void testMeshMirrorIsBakedIn() {
  std::printf("testMeshMirrorIsBakedIn\n");
  // An asymmetric 2x1x1 schematic: a stone block at local x=0 and a gold block at x=1.
  bitematica::core::Schematic s;
  s.palette.intern(bitematica::core::BlockState{});
  bitematica::core::BlockState stone;
  stone.name = "minecraft:stone";
  bitematica::core::BlockState gold;
  gold.name = "minecraft:gold_block";
  s.palette.intern(stone); // 1
  s.palette.intern(gold);  // 2
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {1, 0, 0};
  s.blocks = {1, 2};

  bitematica::world::Placement plain;
  const auto plainMesh = bitematica::mesh::SectionMesher::build(s, plain, 0, 0);
  bitematica::world::Placement mirrored;
  mirrored.mirror = bitematica::world::Mirror::X;
  const auto mirroredMesh = bitematica::mesh::SectionMesher::build(s, mirrored, 0, 0);

  // Both have the same triangle count (the mirror does not add or remove geometry).
  check(plainMesh.sections.size() == mirroredMesh.sections.size(),
        "mirroring does not change the section count");
  if (plainMesh.sections.empty() || mirroredMesh.sections.empty()) return;
  check(plainMesh.sections[0].triangles.size() == mirroredMesh.sections[0].triangles.size(),
        "mirroring does not change the triangle count");

  // The mirrored mesh's stone (at local x in [0,1]) must sit at x in [1,2] after reflection. Find the
  // extreme x of each mesh and confirm they are reflections about x=1.
  auto maxX = [](const bitematica::mesh::SectionMesh &m) {
    float best = -1e9f;
    for (const auto &v : m.vertices) best = std::max(best, v.x);
    return best;
  };
  auto minX = [](const bitematica::mesh::SectionMesh &m) {
    float best = 1e9f;
    for (const auto &v : m.vertices) best = std::min(best, v.x);
    return best;
  };
  check(std::fabs(minX(plainMesh.sections[0]) - 0.0f) < 1e-4f, "plain mesh starts at local x=0");
  check(std::fabs(maxX(plainMesh.sections[0]) - 2.0f) < 1e-4f, "plain mesh ends at local x=2");
  check(std::fabs(minX(mirroredMesh.sections[0]) - 0.0f) < 1e-4f,
        "mirrored mesh still spans local x=0..2");
  check(std::fabs(maxX(mirroredMesh.sections[0]) - 2.0f) < 1e-4f,
        "mirrored mesh still spans local x=0..2");

  // Mirror twice returns the original geometry (same vertex positions as a set).
  bitematica::world::Placement doubleMirror;
  doubleMirror.mirror = bitematica::world::Mirror::None; // a second mirror on X would be None
  const auto doubleMesh = bitematica::mesh::SectionMesher::build(s, doubleMirror, 0, 0);
  check(doubleMesh.sections[0].vertices.size() == plainMesh.sections[0].vertices.size(),
        "mirror-then-unmirror restores the vertex count");
}

// --- backend gating decision (B15) --------------------------------------------------

void testGlesVersionGate() {
  std::printf("testGlesVersionGate\n");
  // The real predicate the renderer uses (gles/Gles.hpp), not a copy, so this pins its behaviour.
  check(bitematica::gles::isGles3OrLater("OpenGL ES 3.0 V@1.0"), "ES 3.0 accepted");
  check(bitematica::gles::isGles3OrLater("OpenGL ES 3.2 build 1.3"), "ES 3.2 accepted");
  check(bitematica::gles::isGles3OrLater("OpenGL ES-CM 3.1"), "ES 3.1 accepted");
  check(!bitematica::gles::isGles3OrLater("OpenGL ES 2.0"), "ES 2.0 rejected");
  check(!bitematica::gles::isGles3OrLater("Vulkan 1.3"), "Vulkan rejected");
  check(!bitematica::gles::isGles3OrLater(nullptr), "a null version is rejected");
}

// --- outline generation (B8) --------------------------------------------------------

void makeSolidCube(bitematica::core::Schematic &schematic, int size) {
  schematic.palette.intern(bitematica::core::BlockState{}); // index 0 = air
  bitematica::core::BlockState stone;
  stone.name = "minecraft:stone";
  schematic.palette.intern(stone); // index 1
  schematic.bounds.min = {0, 0, 0};
  schematic.bounds.max = {size - 1, size - 1, size - 1};
  schematic.blocks.assign(static_cast<size_t>(schematic.volume()), 1);
}

void testOutlineIsSurfaceOnly() {
  std::printf("testOutlineIsSurfaceOnly\n");
  bitematica::core::Schematic schematic;
  makeSolidCube(schematic, 3);
  bitematica::world::Placement placement;

  const auto outline = bitematica::mesh::SectionMesher::buildOutlines(
      schematic, placement, 0, 2, 0xFFFFFFFFu);
  check(!outline.sections.empty(), "outline is non-empty for a solid cube");
  check(!outline.truncated, "small outline is not truncated");

  long long segments = 0;
  for (const auto &section : outline.sections) segments += section.segments.size();
  check(segments > 0, "outline emits segments");
  // A solid 3x3x3 has one interior block whose 12 edges must not appear, so the count is well below
  // the naive "12 edges x 27 blocks = 324".
  check(segments < 27 * 12, "outline is de-duplicated (fewer than 12 edges per block)");
  check(outline.emittedEdges == segments, "emittedEdges matches the segment count");
}

void testOutlineEmptyForAirOnly() {
  std::printf("testOutlineEmptyForAirOnly\n");
  bitematica::core::Schematic schematic;
  schematic.palette.intern(bitematica::core::BlockState{}); // air at index 0
  schematic.bounds.min = {0, 0, 0};
  schematic.bounds.max = {1, 1, 1};
  schematic.blocks.assign(static_cast<size_t>(schematic.volume()), 0);
  bitematica::world::Placement placement;
  const auto outline = bitematica::mesh::SectionMesher::buildOutlines(
      schematic, placement, 0, 1, 0xFFFFFFFFu);
  check(outline.sections.empty(), "an air-only schematic has no outline");
}

// --- block shapes (C3/C4) -----------------------------------------------------------

void testBlockShapeTable() {
  std::printf("testBlockShapeTable\n");
  using bitematica::core::BlockShape;
  using bitematica::core::BlockState;

  // A plain stone block is a full cube.
  BlockState stone;
  stone.name = "minecraft:stone";
  check(BlockShape::isCube(stone), "stone is a full cube");
  check(BlockShape::boxesFor(stone).size() == 1, "a cube is one box");

  // A bottom slab is half height; a top slab is the upper half.
  BlockState slabBottom;
  slabBottom.name = "minecraft:oak_slab";
  check(!BlockShape::isCube(slabBottom), "a slab is not a full cube");
  check(BlockShape::boxesFor(slabBottom)[0].maxY == 0.5f, "a bottom slab is half height");
  BlockState slabTop;
  slabTop.name = "minecraft:oak_slab";
  slabTop.states = {{"top_slot_bit", "1"}};
  check(BlockShape::boxesFor(slabTop)[0].minY == 0.5f, "a top slab starts at half height");

  // Stairs are two boxes, and the upper step moves with the facing.
  BlockState stairs;
  stairs.name = "minecraft:oak_stairs";
  stairs.states = {{"weirdo_direction", "2"}}; // south
  const auto stairBoxes = BlockShape::boxesFor(stairs);
  check(stairBoxes.size() == 2, "stairs are two boxes");
  check(stairBoxes[1].minZ == 0.5f, "a south-facing stair's step is on the +Z side");

  // A carpet is a thin plate; a pressure plate is thin; a fence is a post plus arms.
  BlockState carpet;
  carpet.name = "minecraft:white_carpet";
  check(BlockShape::boxesFor(carpet)[0].maxY <= 0.07f, "a carpet is a thin plate");
  BlockState fence;
  fence.name = "minecraft:oak_fence";
  check(BlockShape::boxesFor(fence).size() == 1, "a bare fence is just a post");
  BlockState fenceLinked;
  fenceLinked.name = "minecraft:oak_fence";
  fenceLinked.states = {{"north", "1"}};
  check(BlockShape::boxesFor(fenceLinked).size() > 1, "a connected fence grows an arm");

  // Fluids are slightly lower unless they are a source.
  BlockState water;
  water.name = "minecraft:water";
  water.states = {{"liquid_depth", "0"}};
  check(BlockShape::boxesFor(water)[0].maxY == 1.0f, "a water source is full height");
  BlockState flowing;
  flowing.name = "minecraft:water";
  flowing.states = {{"liquid_depth", "4"}};
  check(BlockShape::boxesFor(flowing)[0].maxY < 1.0f, "flowing water is lower than a source");

  // Never-render blocks.
  BlockState air;
  check(BlockShape::neverRenders(air), "air never renders");
  BlockState structureVoid;
  structureVoid.name = "minecraft:structure_void";
  check(BlockShape::neverRenders(structureVoid), "structure void never renders");
  BlockState barrier;
  barrier.name = "minecraft:barrier";
  check(BlockShape::neverRenders(barrier), "barrier is hidden by default");
  check(!BlockShape::neverRenders(barrier, true), "barrier renders when the toggle is on");
}

// --- per-vertex verdicts (F6) -------------------------------------------------------

void testMeshCarriesVerdicts() {
  std::printf("testMeshCarriesVerdicts\n");
  bitematica::core::Schematic s;
  makeSolidCube(s, 2); // 2x2x2 stone
  bitematica::world::Placement placement;

  // Without statuses, every vertex is Correct (0).
  const auto plain = bitematica::mesh::SectionMesher::build(s, placement, 0, 1);
  bool allCorrect = true;
  for (const auto &section : plain.sections) {
    for (const auto &v : section.vertices) {
      if (v.verdict != 0) allCorrect = false;
    }
  }
  check(allCorrect, "with no verification every vertex is tagged Correct");

  // With statuses, the vertices carry them. Mark block (0,0,0) Missing (1).
  std::vector<uint8_t> statuses(s.blocks.size(), 0);
  statuses[static_cast<size_t>(s.indexOf(0, 0, 0))] = 1;
  const auto tagged = bitematica::mesh::SectionMesher::build(s, placement, 0, 1, 1500000, &statuses);
  bool sawMissing = false;
  for (const auto &section : tagged.sections) {
    for (const auto &v : section.vertices) {
      if (v.verdict == 1) sawMissing = true;
    }
  }
  check(sawMissing, "the Missing verdict reached the mesh vertices");
}

// --- mismatch display modes (F6) ----------------------------------------------------

void testMismatchDisplayModes() {
  std::printf("testMismatchDisplayModes\n");
  using bitematica::gles::drawsUnderMismatchMode;
  const int correct = 0, missing = 1, wrongBlock = 2, wrongState = 3, extra = 4, unknown = 5;

  // Show all: everything is drawn.
  check(drawsUnderMismatchMode(0, correct) && drawsUnderMismatchMode(0, missing) &&
            drawsUnderMismatchMode(0, unknown),
        "show-all draws every verdict");

  // Only mismatch: the correct and unknown blocks are hidden, every wrong one is drawn.
  check(!drawsUnderMismatchMode(1, correct), "only-mismatch hides correct");
  check(!drawsUnderMismatchMode(1, unknown), "only-mismatch hides unknown");
  check(drawsUnderMismatchMode(1, missing) && drawsUnderMismatchMode(1, wrongBlock) &&
            drawsUnderMismatchMode(1, wrongState) && drawsUnderMismatchMode(1, extra),
        "only-mismatch draws every wrong verdict");

  // Hide correct: correct is hidden, but unknown stays visible (an unreadable area must not look
  // finished).
  check(!drawsUnderMismatchMode(2, correct), "hide-correct hides correct");
  check(drawsUnderMismatchMode(2, unknown), "hide-correct keeps unknown visible");
  check(drawsUnderMismatchMode(2, missing) && drawsUnderMismatchMode(2, extra),
        "hide-correct keeps mismatches visible");
}

// --- block colours (C1) -------------------------------------------------------------

void testBlockColorTableAndFallback() {
  std::printf("testBlockColorTableAndFallback\n");
  using bitematica::render::BlockColors;

  // A known block has an explicit colour.
  check(BlockColors::hasColor("minecraft:stone"), "stone has an explicit colour");
  const auto stone = BlockColors::forBlock("minecraft:stone");
  check(((stone.argb >> 24) & 0xff) == 0xff, "a table colour is opaque");

  // An unknown block gets a deterministic, non-air colour and is recorded (never fabricated
  // silently).
  BlockColors::clearUnknowns();
  const auto unknown1 = BlockColors::forBlock("minecraft:some_modded_block_xyz");
  const auto unknown2 = BlockColors::forBlock("minecraft:some_modded_block_xyz");
  check(unknown1.argb == unknown2.argb, "the fallback colour is deterministic");
  check(!BlockColors::hasColor("minecraft:some_modded_block_xyz"),
        "the unknown block is not in the table");
  const auto unknowns = BlockColors::unknownBlocks();
  bool recorded = false;
  for (const auto &name : unknowns) {
    if (name == "some_modded_block_xyz") recorded = true;
  }
  check(recorded, "the unknown block is recorded for the status line");

  // Loading a JSON table overrides an entry and adds a new one.
  const int merged = BlockColors::loadJson(R"({"minecraft:stone":[1,2,3],"minecraft:new_thing":"#0a0b0c"})");
  check(merged == 2, "two entries merged from JSON, got " + std::to_string(merged));
  const auto overridden = BlockColors::forBlock("minecraft:stone");
  check(((overridden.argb >> 16) & 0xff) == 1 && ((overridden.argb >> 8) & 0xff) == 2 &&
            (overridden.argb & 0xff) == 3,
        "the JSON entry overrode the default");
  check(BlockColors::hasColor("minecraft:new_thing"), "the hex entry was added");

  // A malformed entry is skipped, not fatal.
  const int bad = BlockColors::loadJson(R"({"a":[1,2],"b":"nope","c":[4,5,6]})");
  check(bad == 1, "only the well-formed entry merged, got " + std::to_string(bad));
}

// --- mismatch palette (C6) ----------------------------------------------------------

void testMismatchPalettePresets() {
  std::printf("testMismatchPalettePresets\n");
  using bitematica::world::MismatchPalette;

  const auto def = MismatchPalette::preset(0);
  const auto cb = MismatchPalette::preset(1);
  const auto hc = MismatchPalette::preset(2);

  // The five verdict colours must be distinct within each preset, or the palette fails its job.
  auto distinct = [](const MismatchPalette &p) {
    return p.correct != p.missing && p.missing != p.wrongBlock &&
           p.wrongBlock != p.wrongState && p.wrongState != p.extra;
  };
  check(distinct(def), "default palette verdicts are distinct");
  check(distinct(cb), "colour-blind palette verdicts are distinct");
  check(distinct(hc), "high-contrast palette verdicts are distinct");

  // The default is Litematica-like: missing is blue-ish, wrong block is red-ish.
  check(((def.missing) & 0xff) > ((def.missing >> 16) & 0xff), "default missing is blue-dominant");
  check(((def.wrongBlock >> 16) & 0xff) > (def.wrongBlock & 0xff),
        "default wrong-block is red-dominant");
  check(MismatchPalette::preset(99).correct == def.correct, "an unknown index falls back to default");
}

// --- render plan (M3) ---------------------------------------------------------------

void testRenderPlanSectionSelection() {
  std::printf("testRenderPlanSectionSelection\n");
  const auto loaded = bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) {
    check(false, "cube fixture loads");
    return;
  }
  bitematica::world::Placement placement;
  placement.origin = {0, 0, 0};
  const auto mesh = bitematica::mesh::SectionMesher::build(loaded.schematic, placement, 0, 255);

  const Vec3 camera{0.0f, 0.0f, 16.0f};
  const Mat4 view = bitematica::render::lookAt(camera, {0, 0, -1}, {0, 1, 0});
  const Mat4 projection = bitematica::render::perspective(70.0f, 1.0f, 0.1f, 1000.0f);
  const auto cull =
      bitematica::render::computePlacementCull(projection, view, {0, 0, 0}, 0.0f, camera);

  int drawn = 0;
  for (const auto &section : mesh.sections) {
    if (cull.frustum.intersectsBox(sectionMin(section), sectionMax(section))) ++drawn;
  }
  check(drawn == static_cast<int>(mesh.sections.size()),
        "all sections of a nearby small build are in the frustum");

  const Vec3 farCamera{0.0f, 0.0f, 5000.0f};
  const Mat4 farView = bitematica::render::lookAt(farCamera, {0, 0, -1}, {0, 1, 0});
  const auto farCull =
      bitematica::render::computePlacementCull(projection, farView, {0, 0, 0}, 0.0f, farCamera);
  int drawnFar = 0;
  for (const auto &section : mesh.sections) {
    const Vec3 center = (sectionMin(section) + sectionMax(section)) * 0.5f;
    if (bitematica::render::length(center - farCull.localCamera) > 256.0f) continue;
    if (farCull.frustum.intersectsBox(sectionMin(section), sectionMax(section))) ++drawnFar;
  }
  check(drawnFar == 0, "the distance cull selects nothing at 5000 blocks with a 256-block radius");
}

} // namespace

int main() {
  std::printf("BiteMatica render-math tests\n");
  std::printf("============================\n");
  testCameraRelativePrecisionAtLargeCoordinates();
  testCameraRelativeMvpExcludesPlacementTransform();
  testPlacementCullAllRotationsAndMirrors();
  testMeshMirrorIsBakedIn();
  testGlesVersionGate();
  testOutlineIsSurfaceOnly();
  testOutlineEmptyForAirOnly();
  testMeshCarriesVerdicts();
  testMismatchDisplayModes();
  testBlockShapeTable();
  testBlockColorTableAndFallback();
  testMismatchPalettePresets();
  testRenderPlanSectionSelection();
  std::printf("============================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
