// Host tests for the Litematica toolset: the nine tool modes, layer ranges, placement management,
// block targeting (ray-cast), Easy Place, paste-command generation, the menu model and the menu
// builder.
//
// All of it is pure C++ with no GL and no Android, which is the point: the ray-cast, the command
// text and the menu shape are exactly the parts that are painful to debug on a device and trivial
// to pin here.

#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Schematic.hpp"
#include "format/SchematicFormat.hpp"
#include "litematica/BlockTargeting.hpp"
#include "litematica/EasyPlace.hpp"
#include "litematica/LayerRange.hpp"
#include "litematica/PasteJob.hpp"
#include "litematica/PlacementManager.hpp"
#include "litematica/ToolMode.hpp"
#include "mod/Config.hpp"
#include "ui/MenuBuilder.hpp"
#include "ui/MenuModel.hpp"
#include "world/Placement.hpp"
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

/// A fake world backed by a set of solid blocks, for the ray-cast and Easy Place tests.
class SolidWorld : public bitematica::world::WorldSource {
public:
  void setSolid(const bitematica::core::BlockPos &pos, const std::string &name) {
    mBlocks[key(pos)] = name;
  }
  bool readBlock(const bitematica::core::BlockPos &position,
                 bitematica::core::BlockState &out) override {
    auto it = mBlocks.find(key(position));
    if (it == mBlocks.end()) {
      out = bitematica::core::BlockState{};
      return true;
    }
    out.name = it->second;
    out.states.clear();
    return true;
  }
  bool available() const override { return true; }

  /// A predicate over world coordinates, for the ray-cast.
  bool isSolid(int x, int y, int z) const {
    return mBlocks.count(key(bitematica::core::BlockPos{x, y, z})) > 0;
  }

private:
  static std::string key(const bitematica::core::BlockPos &p) {
    return std::to_string(p.x) + "," + std::to_string(p.y) + "," + std::to_string(p.z);
  }
  std::unordered_map<std::string, std::string> mBlocks;
};

// --- tool modes -----------------------------------------------------------------------

void testToolModeCycle() {
  std::printf("testToolModeCycle\n");
  using bitematica::litematica::ToolMode;
  check(bitematica::litematica::kToolModeCount == 9, "there are nine tool modes");

  // Cycling forward through all nine returns to the start.
  ToolMode mode = ToolMode::AreaSelection;
  for (int i = 0; i < 9; ++i) mode = bitematica::litematica::nextToolMode(mode);
  check(mode == ToolMode::AreaSelection, "nine forward steps wrap to the start");

  // And backwards.
  mode = ToolMode::AreaSelection;
  mode = bitematica::litematica::previousToolMode(mode);
  check(mode == ToolMode::PickBlock, "one backward step from the start lands on the last mode");

  // Every mode has a name and a hint.
  bool named = true;
  for (int i = 0; i < 9; ++i) {
    const auto m = static_cast<ToolMode>(i);
    if (std::string(bitematica::litematica::toolModeName(m)).empty()) named = false;
    if (std::string(bitematica::litematica::toolModeHint(m)).empty()) named = false;
  }
  check(named, "every mode has a name and a hint");
}

// --- layer range ----------------------------------------------------------------------

void testLayerRange() {
  std::printf("testLayerRange\n");
  using bitematica::litematica::LayerMode;
  using bitematica::litematica::LayerRange;

  LayerRange range;
  int lo = 0;
  int hi = 0;

  range.mode = LayerMode::All;
  range.resolve(8, lo, hi);
  check(lo == 0 && hi == 7, "All covers every layer");

  range.mode = LayerMode::Single;
  range.layer = 3;
  range.resolve(8, lo, hi);
  check(lo == 3 && hi == 3, "Single resolves to one layer");

  range.mode = LayerMode::AllBelow;
  range.layer = 4;
  range.resolve(8, lo, hi);
  check(lo == 0 && hi == 4, "All Below covers 0..layer");

  range.mode = LayerMode::AllAbove;
  range.layer = 4;
  range.resolve(8, lo, hi);
  check(lo == 4 && hi == 7, "All Above covers layer..top");

  range.mode = LayerMode::Range;
  range.layer = 2;
  range.layerMax = 5;
  range.resolve(8, lo, hi);
  check(lo == 2 && hi == 5, "Range covers layer..layerMax");

  // A layer index past the top must clamp, not render nothing.
  range.mode = LayerMode::Single;
  range.layer = 99;
  range.resolve(8, lo, hi);
  check(lo == 7 && hi == 7, "an out-of-range layer clamps to the top");

  // Stepping wraps within the build.
  range.mode = LayerMode::Single;
  range.layer = 0;
  range.step(8, -1);
  check(range.layer == 0, "stepping below zero clamps to zero");
  range.step(8, 100);
  check(range.layer == 7, "stepping past the top clamps to the top");
}

// --- placement manager ----------------------------------------------------------------

void testPlacementManager() {
  std::printf("testPlacementManager\n");
  auto schematic = std::make_shared<bitematica::core::Schematic>();
  schematic->bounds = {{0, 0, 0}, {1, 1, 1}};
  schematic->allocateBlocks();
  schematic->name = "test";

  bitematica::litematica::PlacementManager manager;
  check(manager.empty(), "a fresh manager is empty");
  const int first = manager.add(schematic, bitematica::world::Placement{});
  const int second = manager.add(schematic, bitematica::world::Placement{});
  check(first == 0 && second == 1, "adds return their index");
  check(manager.count() == 2, "two placements");
  check(manager.activeIndex() == 1, "the newest placement is active");

  manager.stepActive(-1);
  check(manager.activeIndex() == 0, "stepping back moves to the first");
  manager.stepActive(-1);
  check(manager.activeIndex() == 1, "stepping back from the first wraps to the last");

  manager.remove(0);
  check(manager.count() == 1, "remove drops one");
  manager.remove(0);
  check(manager.empty() && manager.activeIndex() == -1, "removing all clears the active index");
}

// --- block targeting ------------------------------------------------------------------

void testRayCast() {
  std::printf("testRayCast\n");
  SolidWorld world;
  // A wall at z = -5.
  for (int x = -3; x <= 3; ++x) {
    for (int y = -3; y <= 3; ++y) {
      world.setSolid({x, y, -5}, "minecraft:stone");
    }
  }

  // Looking straight down -Z from the origin must hit the wall.
  auto hit = bitematica::litematica::BlockTargeting::cast(
      {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, -1.0f}, 20.0f,
      [&](int x, int y, int z) { return world.isSolid(x, y, z); });
  check(hit.hit, "a ray down -Z hits the wall");
  check(hit.block.z == -5, "the hit is at z = -5, got " + std::to_string(hit.block.z));
  check(hit.normal.z == 1, "the entered face normal points back at the camera (+Z)");

  // Looking up into empty space must miss.
  auto miss = bitematica::litematica::BlockTargeting::cast(
      {0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, 20.0f,
      [&](int x, int y, int z) { return world.isSolid(x, y, z); });
  check(!miss.hit, "a ray into empty space misses");

  // The placement anchor is the block on the near side of the hit face.
  const auto anchor = bitematica::litematica::BlockTargeting::placementAnchor(hit);
  check(anchor.z == -4, "the placement anchor sits in front of the wall");
}

// --- Easy Place -----------------------------------------------------------------------

void testEasyPlace() {
  std::printf("testEasyPlace\n");
  // A 1x1x1 schematic wanting a stair at the origin.
  bitematica::core::Schematic schematic;
  schematic.bounds = {{0, 0, 0}, {0, 0, 0}};
  schematic.allocateBlocks();
  bitematica::core::BlockState stairs;
  stairs.name = "minecraft:oak_stairs";
  stairs.states = {{"facing", "north"}};
  schematic.palette.intern(bitematica::core::BlockState{}); // index 0 air
  schematic.setBlock(0, 0, 0, schematic.palette.intern(stairs));

  bitematica::world::Placement placement;
  placement.origin = {0, 0, 0};

  SolidWorld world;
  // Clicking the floor at (0,-1,0) on its top face should place onto (0,0,0).
  const auto decision = bitematica::litematica::EasyPlace::decide(
      {0, -1, 0}, {0, 1, 0}, schematic, placement, world, false);
  check(decision.kind == bitematica::litematica::EasyPlaceDecision::Kind::Place,
        "Easy Place decides to place");
  check(decision.target == bitematica::core::BlockPos{0, 0, 0}, "the target is the schematic cell");
  check(decision.state.baseName() == "minecraft:oak_stairs", "the state is the wanted block");

  // If the world already has the block, it reports Correct.
  SolidWorld filled;
  filled.setSolid({0, 0, 0}, "minecraft:oak_stairs");
  const auto already = bitematica::litematica::EasyPlace::decide(
      {0, -1, 0}, {0, 1, 0}, schematic, placement, filled, false);
  check(already.kind == bitematica::litematica::EasyPlaceDecision::Kind::Correct,
        "an already-placed block reports Correct");

  // Orienting a stair away from the player: standing to the west, it should face east.
  const auto oriented = bitematica::litematica::EasyPlace::orientTowards(stairs, {-1, 0, 0}, {0, 0, 0});
  bool facesEast = false;
  for (const auto &[k, v] : oriented.states) {
    if (k == "facing" && v == "east") facesEast = true;
  }
  check(facesEast, "a stair faces away from the player");
}

// --- paste commands -------------------------------------------------------------------

void testPasteCommands() {
  std::printf("testPasteCommands\n");
  bitematica::core::Schematic schematic;
  schematic.bounds = {{0, 0, 0}, {3, 0, 0}}; // a 4x1x1 row
  schematic.allocateBlocks();
  const uint32_t stone = schematic.palette.intern({"minecraft:stone", {}});
  for (int x = 0; x < 4; ++x) schematic.setBlock(x, 0, 0, stone);

  bitematica::world::Placement placement;
  placement.origin = {10, 64, 20};
  const auto commands = bitematica::litematica::PasteJob::build(schematic, placement);

  check(!commands.empty(), "paste produced commands");
  // Four same blocks in a row merge into one /fill.
  check(commands.size() == 1, "a run of same blocks merges into one fill, got " +
                                  std::to_string(commands.size()));
  check(commands[0] == "fill 10 64 20 13 64 20 minecraft:stone replace",
        "the fill command text: " + commands[0]);

  // With fill disabled, one command per block.
  bitematica::litematica::PasteOptions noFill;
  noFill.useFill = false;
  const auto perBlock = bitematica::litematica::PasteJob::build(schematic, placement, noFill);
  check(perBlock.size() == 4, "without fill there are four setblock commands");

  // Block-state formatting: numeric unquoted, strings quoted.
  bitematica::core::BlockState state;
  state.name = "minecraft:oak_stairs";
  state.states = {{"weirdo_direction", "3"}, {"half", "top"}};
  const std::string id = bitematica::litematica::PasteJob::blockIdFor(state);
  check(id.find("\"weirdo_direction\"=3") != std::string::npos,
        "numeric states are unquoted: " + id);
  check(id.find("\"half\"=\"top\"") != std::string::npos, "string states are quoted: " + id);
}

// --- menu -----------------------------------------------------------------------------

void testMenuModel() {
  std::printf("testMenuModel\n");
  using bitematica::ui::MenuBuilder;
  using bitematica::ui::MenuTab;

  bitematica::mod::Config config;
  bitematica::litematica::PlacementManager placements;
  auto schematic = std::make_shared<bitematica::core::Schematic>();
  schematic->bounds = {{0, 0, 0}, {2, 2, 2}};
  schematic->allocateBlocks();
  schematic->name = "hut";
  placements.add(schematic, bitematica::world::Placement{});

  MenuBuilder::Context context;
  context.config = &config;
  context.placements = &placements;
  context.materials = {{"minecraft:stone", 26, 25, 1}};
  context.loadedNames = {"hut.litematic"};
  context.statusLine = "ready";

  const auto pages = MenuBuilder::buildAll(context);
  check(pages.size() == static_cast<size_t>(bitematica::ui::kMenuTabCount),
        "every menu tab has a page, got " + std::to_string(pages.size()));

  bitematica::ui::MenuModel model;
  model.setPages(pages);
  check(model.validate(), "every widget has an id and a coherent range");
  check(model.page(MenuTab::Config) != nullptr, "the config page exists");
  check(model.page(MenuTab::Placements) != nullptr, "the placements page exists");

  // The material page reflects the numbers it was given.
  const auto *materials = model.page(MenuTab::Materials);
  bool foundMissing = false;
  for (const auto &widget : materials->widgets) {
    if (widget.id == "materials.missing" && widget.value == "1") foundMissing = true;
  }
  check(foundMissing, "the material page shows one missing block");

  // The placement page shows the loaded schematic's name.
  const auto *placementPage = model.page(MenuTab::Placements);
  bool foundName = false;
  for (const auto &widget : placementPage->widgets) {
    if (widget.value == "hut") foundName = true;
  }
  check(foundName, "the placements page shows the loaded schematic");
}

void testMenuBuilderRejectsBadWidgets() {
  std::printf("testMenuBuilderRejectsBadWidgets\n");
  // A slider whose min exceeds its max is invalid and `validate` must catch it, so the tests are a
  // real guard on the builder rather than a rubber stamp.
  bitematica::ui::MenuPage page;
  page.tab = bitematica::ui::MenuTab::Main;
  bitematica::ui::Widget bad;
  bad.kind = bitematica::ui::WidgetKind::Slider;
  bad.id = "bad";
  bad.minValue = 10;
  bad.maxValue = 1;
  page.widgets.push_back(bad);

  bitematica::ui::MenuModel model;
  model.setPages({page});
  check(!model.validate(), "a slider with min > max is rejected");
}

} // namespace

int main() {
  std::printf("BiteMatica Litematica toolset tests\n");
  std::printf("===================================\n");
  testToolModeCycle();
  testLayerRange();
  testPlacementManager();
  testRayCast();
  testEasyPlace();
  testPasteCommands();
  testMenuModel();
  testMenuBuilderRejectsBadWidgets();
  std::printf("===================================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
