// Host-runnable tests for BiteMatica's format, mesh and UI layers.
//
// The point of running these on a host build (no Android, no preloader) is that the
// parts that can be wrong in a way a device would only reveal at runtime are the parts
// that do not actually need the device: NBT decoding, palette unpacking, the mesh
// projector and the toolbar hit boxes. The tests read the real fixture files generated
// by test/make_fixtures.py through the production readers, so a passing suite means the
// readers accept genuine wire-format data, not a mock of it.
//
// Build: see CMakeLists.txt target `bitematica_tests` (or the header comment in build.sh).

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/MaterialList.hpp"
#include "core/Schematic.hpp"
#include "format/SchematicFormat.hpp"
#include "format/StructureWriter.hpp"
#include "mod/Config.hpp"
#include "mod/ControllerNavigator.hpp"
#include "mod/JsonConfig.hpp"
#include "mod/SchematicLibrary.hpp"
#include "nbt/NbtReader.hpp"
#include "nbt/NbtWriter.hpp"
#include "render/HudOverlay.hpp"

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

void checkNear(float actual, float expected, float tolerance, const std::string &message) {
  check(std::fabs(actual - expected) <= tolerance, message);
}

std::string fixture(const std::string &name) {
  return std::string(BITEMATICA_FIXTURE_DIR) + "/" + name;
}

// --- NBT round trip -----------------------------------------------------------------

void testNbtRoundTrip() {
  std::printf("testNbtRoundTrip\n");
  using namespace bitematica::nbt;

  Tag root = NbtWriter::compound("Root");
  root.children.push_back(NbtWriter::intTag("Answer", 42));
  root.children.push_back(NbtWriter::string("Greeting", "hello"));
  root.children.push_back(NbtWriter::byteTag("Flag", 1));
  Tag list = NbtWriter::list("Numbers", TagType::Int);
  list.children.push_back(NbtWriter::intTag("", 1));
  list.children.push_back(NbtWriter::intTag("", 2));
  list.children.push_back(NbtWriter::intTag("", 3));
  root.children.push_back(list);

  const std::vector<uint8_t> encoded = NbtWriter::writeFile(root, true);
  const Document decoded = NbtReader::readFile(encoded);
  check(decoded.valid(), "round-tripped NBT is valid");
  check(decoded.root.find("Answer") != nullptr, "Answer tag survives");
  if (decoded.root.find("Answer") != nullptr) {
    check(decoded.root.find("Answer")->asInt() == 42, "Answer is 42");
  }
  if (decoded.root.find("Greeting") != nullptr) {
    check(decoded.root.find("Greeting")->asString() == "hello", "Greeting survives");
  }
  if (decoded.root.find("Numbers") != nullptr) {
    check(decoded.root.find("Numbers")->size() == 3, "list keeps three ints");
  }
  check(decoded.compressionGzip, "gzip wrapping was detected");
}

void testNbtRejectsGarbage() {
  std::printf("testNbtRejectsGarbage\n");
  const std::vector<uint8_t> garbage = {0xff, 0x00, 0x12, 0x34, 0x56};
  const bitematica::nbt::Document document = bitematica::nbt::NbtReader::readFile(garbage);
  check(!document.valid(), "garbage is rejected, not partially decoded");
}

// --- format readers -----------------------------------------------------------------

void testLitematic() {
  std::printf("testLitematic\n");
  const bitematica::format::LoadResult result =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  check(result.success, std::string("litematic loads: ") + result.error);
  if (!result.success) return;
  const bitematica::core::Schematic &s = result.schematic;
  check(s.sourceFormat == "litematic", "source format recorded");
  check(s.sizeX() == 3 && s.sizeY() == 3 && s.sizeZ() == 3, "3x3x3 bounds");
  check(s.name == "Fixture Litematic", "metadata name read");
  check(s.author == "BiteMatica Tests", "metadata author read");
  // 26 stone + 1 oak_planks.
  const auto materials = bitematica::core::MaterialList::build(s);
  uint64_t stone = 0;
  uint64_t planks = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:stone") stone = m.count;
    if (m.name == "minecraft:oak_planks") planks = m.count;
  }
  check(stone == 26, "26 stone blocks counted, got " + std::to_string(stone));
  check(planks == 1, "1 oak_planks block counted, got " + std::to_string(planks));
}

void testSchem() {
  std::printf("testSchem\n");
  const auto result =
      bitematica::format::SchematicFormat::readPath(fixture("cube.schem"));
  check(result.success, std::string("schem loads: ") + result.error);
  if (!result.success) return;
  const bitematica::core::Schematic &s = result.schematic;
  check(s.sourceFormat == "schem", "source format recorded");
  check(s.sizeX() == 2 && s.sizeY() == 2 && s.sizeZ() == 2, "2x2x2 bounds");
  const auto materials = bitematica::core::MaterialList::build(s);
  uint64_t stone = 0;
  uint64_t planks = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:stone") stone = m.count;
    if (m.name == "minecraft:oak_planks") planks = m.count;
  }
  check(stone == 7, "7 stone blocks, got " + std::to_string(stone));
  check(planks == 1, "1 oak_planks block, got " + std::to_string(planks));
}

void testLegacySchematic() {
  std::printf("testLegacySchematic\n");
  const auto result =
      bitematica::format::SchematicFormat::readPath(fixture("column.schematic"));
  check(result.success, std::string("schematic loads: ") + result.error);
  if (!result.success) return;
  const bitematica::core::Schematic &s = result.schematic;
  check(s.sourceFormat == "schematic", "source format recorded");
  check(s.sizeX() == 2 && s.sizeY() == 4 && s.sizeZ() == 2, "2x4x2 bounds");
  // Legacy id 1 -> stone, id 5 -> oak_planks; the top layer is planks.
  const auto materials = bitematica::core::MaterialList::build(s);
  uint64_t stone = 0;
  uint64_t planks = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:stone") stone = m.count;
    if (m.name == "minecraft:oak_planks") planks = m.count;
  }
  check(stone == 12, "12 stone blocks, got " + std::to_string(stone));
  check(planks == 4, "4 oak_planks blocks, got " + std::to_string(planks));
}

void testStructure() {
  std::printf("testStructure\n");
  const auto result =
      bitematica::format::SchematicFormat::readPath(fixture("corner.nbt"));
  check(result.success, std::string("structure loads: ") + result.error);
  if (!result.success) return;
  const bitematica::core::Schematic &s = result.schematic;
  check(s.sourceFormat == "structure", "source format recorded");
  check(s.sizeX() == 2 && s.sizeY() == 2 && s.sizeZ() == 2, "2x2x2 bounds");
  const auto materials = bitematica::core::MaterialList::build(s);
  uint64_t red = 0;
  uint64_t stone = 0;
  for (const auto &m : materials) {
    if (m.name == "minecraft:red_wool") red = m.count;
    if (m.name == "minecraft:stone") stone = m.count;
  }
  check(red == 1, "1 red_wool, got " + std::to_string(red));
  check(stone == 7, "7 stone, got " + std::to_string(stone));
}

void testDetection() {
  std::printf("testDetection\n");
  using bitematica::format::FileKind;
  struct Case {
    const char *file;
    FileKind kind;
  };
  const Case cases[] = {
      {"cube.litematic", FileKind::Litematic},
      {"cube.schem", FileKind::SpongeSchem},
      {"column.schematic", FileKind::McEdit},
      {"corner.nbt", FileKind::Structure},
  };
  for (const Case &c : cases) {
    const auto result = bitematica::format::SchematicFormat::readPath(fixture(c.file));
    check(result.success, std::string("detect+load ") + c.file);
  }
  check(bitematica::format::SchematicFormat::looksLikeSchematic("x.litematic"),
        "litematic extension recognized");
  check(!bitematica::format::SchematicFormat::looksLikeSchematic("x.png"),
        "png extension rejected");
}

// --- toolbar + controller -----------------------------------------------------------

void testToolbarHitTest() {
  std::printf("testToolbarHitTest\n");
  bitematica::mod::Config config;
  config.toolbarX = 24;
  config.toolbarY = 120;
  const auto buttons = bitematica::render::HudOverlay::layoutToolbar(config);
  // Twelve original controls plus six world-space additions (verify, layers, move, rotate,
  // mirror, CSV export).
  check(buttons.size() == 23, "twenty-three toolbar buttons, got " +
                                  std::to_string(buttons.size()));

  const auto &first = buttons.front();
  const auto *hit = bitematica::render::HudOverlay::hitTest(
      buttons, first.x + 1.0f, first.y + 1.0f);
  check(hit != nullptr, "a point inside the first button hits");
  if (hit != nullptr) {
    check(hit->action == first.action, "hit resolves to the first action");
  }

  const auto *miss = bitematica::render::HudOverlay::hitTest(buttons, 0.0f, 0.0f);
  check(miss == nullptr, "a point outside every button misses");
}

void testControllerNavigation() {
  std::printf("testControllerNavigation\n");
  bitematica::mod::ControllerNavigator nav;
  const int count = 23;

  auto result = nav.handleKey(bitematica::mod::ControllerNavigator::DpadRight, true, count);
  check(result.handled && !result.activated, "d-pad right moves focus, does not activate");
  check(nav.focusIndex() == 1, "focus moved to index 1");

  result = nav.handleKey(bitematica::mod::ControllerNavigator::DpadDown, true, count);
  check(nav.focusIndex() == 7, "d-pad down moves a row (index 7)");

  result = nav.handleKey(bitematica::mod::ControllerNavigator::ButtonA, true, count);
  check(result.activated, "A activates the focused button");

  result = nav.handleKey(bitematica::mod::ControllerNavigator::ButtonL1, true, count);
  check(result.activated &&
            result.action == bitematica::render::ToolbarButton::Action::RotateLeft,
        "L1 is a direct rotate-left shortcut");

  // Key-up is ignored.
  result = nav.handleKey(bitematica::mod::ControllerNavigator::DpadLeft, false, count);
  check(!result.handled, "key-up does not move focus");

  // Focus order must match the toolbar's own order, index by index.
  const auto buttons = bitematica::render::HudOverlay::layoutToolbar({});
  bitematica::mod::ControllerNavigator probe;
  bool orderMatches = true;
  for (int i = 0; i < count; ++i) {
    // Move to index i by pressing right/down from zero using the grid.
    probe = bitematica::mod::ControllerNavigator();
    for (int c = 0; c < i % bitematica::mod::ControllerNavigator::kColumns; ++c) {
      probe.handleKey(bitematica::mod::ControllerNavigator::DpadRight, true, count);
    }
    for (int r = 0; r < i / bitematica::mod::ControllerNavigator::kColumns; ++r) {
      probe.handleKey(bitematica::mod::ControllerNavigator::DpadDown, true, count);
    }
    const auto activation =
        probe.handleKey(bitematica::mod::ControllerNavigator::ButtonA, true, count);
    if (activation.action != buttons[static_cast<size_t>(i)].action) orderMatches = false;
  }
  check(orderMatches, "controller grid order matches the toolbar order");
}

// --- HUD chrome ---------------------------------------------------------------------

void testChromeCommands() {
  std::printf("testChromeCommands\n");
  const auto result =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!result.success) return;

  bitematica::mod::Config config;
  config.showToolbar = true;
  config.showMaterialList = true;

  const auto buttons = bitematica::render::HudOverlay::layoutToolbar(config);
  const auto materials = bitematica::core::MaterialList::build(result.schematic);
  const auto commands = bitematica::render::HudOverlay::buildCommands(
      config, buttons, materials, "litematic 3x3x3", true);
  check(!commands.empty(), "chrome emits commands");

  bool sawToolbarText = false;
  bool sawMaterial = false;
  for (const auto &c : commands) {
    if (c.kind == bitematica::render::UiCommand::Kind::Text && c.text == "Materials")
      sawMaterial = true;
    if (c.kind == bitematica::render::UiCommand::Kind::Text && c.text == "M") sawToolbarText = true;
  }
  check(sawMaterial, "material panel header present");
  check(sawToolbarText, "toolbar labels present");

  // Opacity must actually change the alpha channel.
  const uint32_t opaque = 0xFF804020u;
  const uint32_t faded = bitematica::render::HudOverlay::applyOpacity(opaque, 50);
  check(((faded >> 24) & 0xff) < ((opaque >> 24) & 0xff), "opacity lowers alpha");
}

/// The schematic must never be drawn as HUD geometry. This is the regression guard for the
/// "software overlay" fallback: the chrome command vocabulary has no triangle primitive, so
/// there is no way to emit schematic geometry through it, and the material panel is the only
/// thing whose presence depends on a schematic being loaded.
void testChromeNeverDrawsSchematicGeometry() {
  std::printf("testChromeNeverDrawsSchematicGeometry\n");
  const auto result =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!result.success) return;

  bitematica::mod::Config config;
  config.showToolbar = true;
  config.showMaterialList = true;
  config.renderMode = bitematica::mod::RenderMode::Skeleton; // most tempting to fake with chrome

  const auto buttons = bitematica::render::HudOverlay::layoutToolbar(config);
  const auto materials = bitematica::core::MaterialList::build(result.schematic);

  // With no schematic loaded the panel is not drawn; with one loaded it is. Neither path emits a
  // triangle, and the command count is bounded by the toolbar grid plus a small material panel —
  // there is no per-block command explosion.
  const auto noSchematic = bitematica::render::HudOverlay::buildCommands(
      config, buttons, {}, "nothing loaded", false);
  const auto withSchematic = bitematica::render::HudOverlay::buildCommands(
      config, buttons, materials, "loaded", true);

  // Every emitted command is chrome (a rect or text). The vocabulary has no geometry primitive at
  // all — `UiCommand::Kind` is only Rect/Text — so schematic geometry cannot be drawn through the
  // HUD even by mistake.
  bool onlyChrome = true;
  for (const auto &c : withSchematic) {
    if (c.kind != bitematica::render::UiCommand::Kind::Rect &&
        c.kind != bitematica::render::UiCommand::Kind::Text) {
      onlyChrome = false;
    }
  }
  check(onlyChrome, "every HUD command is chrome (rect or text)");
  check(withSchematic.size() < 200, "chrome command count is bounded, not per-block");
  // The panel only appears when there is something to count.
  check(noSchematic.size() < withSchematic.size(),
        "the material panel is only emitted when a schematic is loaded");
}

void testConfigNormalize() {
  std::printf("testConfigNormalize\n");
  bitematica::mod::Config config;
  config.opacity = 0;
  config.maxRenderBlocks = 1;
  config.toolbarX = -50;
  config.normalize();
  check(config.opacity == 10, "opacity clamped to the minimum");
  check(config.maxRenderBlocks == 500, "render cap clamped to the minimum");
  check(config.toolbarX == 0, "negative anchor clamped to zero");
}

void testConfigRoundTrip() {
  std::printf("testConfigRoundTrip\n");
  bitematica::mod::Config original;
  original.opacity = 77;
  original.renderMode = bitematica::mod::RenderMode::Skeleton;
  original.showMaterialList = false;
  original.ghostPlacement = true;
  original.maxRenderBlocks = 12345;
  original.toolbarX = 40;
  original.toolbarY = 60;
  original.lastLoadedFile = "schematics/My \"Cube\".litematic";

  const std::string json = bitematica::mod::JsonConfig::serialize(original);
  bitematica::mod::Config parsed;
  std::string error;
  check(bitematica::mod::JsonConfig::parse(json, parsed, error),
        std::string("serialized config parses back: ") + error);
  check(parsed.opacity == 77, "opacity survives the round trip");
  check(parsed.renderMode == bitematica::mod::RenderMode::Skeleton, "render mode survives");
  check(parsed.showMaterialList == false, "bool false survives");
  check(parsed.ghostPlacement == true, "bool true survives");
  check(parsed.maxRenderBlocks == 12345, "int survives");
  check(parsed.lastLoadedFile == original.lastLoadedFile, "quoted string survives");

  // The Phase B float fields must survive too (the scanner must distinguish them from ints).
  bitematica::mod::Config floats;
  floats.showOutlines = false;
  floats.outlineWidth = 2.5f;
  floats.polygonOffsetFactor = -3.0f;
  floats.polygonOffsetUnits = -4.5f;
  bitematica::mod::Config floatsParsed;
  check(bitematica::mod::JsonConfig::parse(
            bitematica::mod::JsonConfig::serialize(floats), floatsParsed, error),
        std::string("float config parses back: ") + error);
  check(!floatsParsed.showOutlines, "showOutlines survives");
  check(std::fabs(floatsParsed.outlineWidth - 2.5f) < 1e-5f, "outlineWidth survives");
  check(std::fabs(floatsParsed.polygonOffsetFactor + 3.0f) < 1e-5f,
        "polygonOffsetFactor survives");
  check(std::fabs(floatsParsed.polygonOffsetUnits + 4.5f) < 1e-5f,
        "polygonOffsetUnits survives");

  // A malformed config must be reported, not silently accepted.
  bitematica::mod::Config ignored;
  check(!bitematica::mod::JsonConfig::parse("[1,2,3]", ignored, error),
        "an array is rejected as a config");
}

void testStructureExportRoundTrip() {
  std::printf("testStructureExportRoundTrip\n");
  const auto loaded =
      bitematica::format::SchematicFormat::readPath(fixture("cube.litematic"));
  if (!loaded.success) return;
  const std::string encoded = bitematica::format::StructureWriter::bytes(loaded.schematic);
  check(!encoded.empty(), "export produced bytes");

  // Read the exported structure back through the production reader.
  std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
  const auto reread = bitematica::format::SchematicFormat::read(bytes, "export.mcstructure");
  check(reread.success, std::string("exported structure reloads: ") + reread.error);
  if (!reread.success) return;
  check(reread.schematic.sizeX() == 3 && reread.schematic.sizeY() == 3,
        "exported bounds match");
  const auto before = bitematica::core::MaterialList::build(loaded.schematic);
  const auto after = bitematica::core::MaterialList::build(reread.schematic);
  uint64_t beforeStone = 0;
  uint64_t afterStone = 0;
  for (const auto &m : before) if (m.name == "minecraft:stone") beforeStone = m.count;
  for (const auto &m : after) if (m.name == "minecraft:stone") afterStone = m.count;
  check(beforeStone == afterStone, "stone count is identical after export+import");
}

void testLibraryScan() {
  std::printf("testLibraryScan\n");
  check(bitematica::mod::SchematicLibrary::isSchematicName("a.litematic"), "litematic accepted");
  check(bitematica::mod::SchematicLibrary::isSchematicName("A.SCHEM"), "case-insensitive ext");
  check(!bitematica::mod::SchematicLibrary::isSchematicName("a.txt"), "txt rejected");
  check(bitematica::mod::SchematicLibrary::step(0, -1, 3) == 2, "prev wraps to the end");
  check(bitematica::mod::SchematicLibrary::step(2, 1, 3) == 0, "next wraps to the start");
  check(bitematica::mod::SchematicLibrary::step(0, 1, 0) == 0, "empty list is safe");
}

} // namespace

int main() {
  std::printf("BiteMatica host tests\n");
  std::printf("=====================\n");
  testNbtRoundTrip();
  testNbtRejectsGarbage();
  testLitematic();
  testSchem();
  testLegacySchematic();
  testStructure();
  testDetection();
  testToolbarHitTest();
  testControllerNavigation();
  testChromeCommands();
  testChromeNeverDrawsSchematicGeometry();
  testConfigNormalize();
  testConfigRoundTrip();
  testStructureExportRoundTrip();
  testLibraryScan();

  std::printf("=====================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
