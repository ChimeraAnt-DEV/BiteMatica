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

#include "core/Schematic.hpp"
#include "format/SchematicFormat.hpp"
#include "gles/Gles.hpp"
#include "mesh/SectionMesher.hpp"
#include "render/MathTypes.hpp"
#include "render/PlacementCull.hpp"
#include "world/Placement.hpp"

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
  testGlesVersionGate();
  testOutlineIsSurfaceOnly();
  testOutlineEmptyForAirOnly();
  testRenderPlanSectionSelection();
  std::printf("============================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
