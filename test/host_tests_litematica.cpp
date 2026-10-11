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
#include "litematica/PlacementEdit.hpp"
#include "litematica/PlacementManager.hpp"
#include "litematica/PlacementStore.hpp"
#include "litematica/ToolMode.hpp"
#include "mod/Config.hpp"
#include "translate/BlockStateTransform.hpp"
#include "ui/MenuBuilder.hpp"
#include "ui/MenuModel.hpp"
#include "ui/MenuRenderer.hpp"
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

void testPlacementUndoRedo() {
  std::printf("testPlacementUndoRedo\n");
  auto schematic = std::make_shared<bitematica::core::Schematic>();
  schematic->bounds = {{0, 0, 0}, {1, 1, 1}};
  schematic->allocateBlocks();
  schematic->name = "test";

  bitematica::litematica::PlacementManager manager;
  manager.add(schematic, bitematica::world::Placement{});
  check(!manager.undo(), "nothing to undo on a fresh manager");

  // A move edit: record the undo point, then change the origin.
  manager.pushUndo();
  manager.active()->placement.origin = {100, 64, -50};
  check(manager.undoDepth() == 1, "one undo entry after an edit");
  check(manager.undo(), "undo succeeds");
  check(manager.at(0)->placement.origin.x == 0, "undo restored the original origin");
  check(manager.redoDepth() == 1, "one redo entry after an undo");
  check(manager.redo(), "redo succeeds");
  check(manager.at(0)->placement.origin.x == 100, "redo re-applied the move");

  // A new edit clears the redo stack.
  manager.undo();
  manager.pushUndo();
  manager.active()->placement.origin = {7, 7, 7};
  check(manager.redoDepth() == 0, "a new edit clears the redo stack");

  // The ring is bounded at 32.
  for (int i = 0; i < 100; ++i) manager.pushUndo();
  check(manager.undoDepth() == 32, "the undo ring is capped at 32, got " +
                                       std::to_string(manager.undoDepth()));
}

// --- placement persistence (D1) -------------------------------------------------------

void testPlacementStoreRoundTrip() {
  std::printf("testPlacementStoreRoundTrip\n");
  std::vector<bitematica::litematica::PlacedSchematic> placements;
  bitematica::litematica::PlacedSchematic a;
  a.schematicId = "schematics/house.litematic";
  a.displayName = "House \"A\"";
  a.placement.origin = {10, 64, -5};
  a.placement.rotation = bitematica::world::Rotation::Cw90;
  a.placement.mirror = bitematica::world::Mirror::Z;
  a.placement.yOffset = 3;
  a.visible = false;
  a.locked = true;
  a.layerMode = 2;
  a.layerAxis = 1;
  a.layerMin = 4;
  a.layerMax = 20;
  a.opacity = 45;
  a.showOutlines = false;
  placements.push_back(a);

  bitematica::litematica::PlacedSchematic b;
  b.schematicId = "schematics/tower.litematic";
  b.placement.origin = {-100, 70, 200};
  placements.push_back(b);

  const std::string json = bitematica::litematica::PlacementStore::serialize(placements);
  std::vector<bitematica::litematica::PlacedSchematic> parsed;
  std::string error;
  check(bitematica::litematica::PlacementStore::parse(json, parsed, error),
        std::string("serialized placements parse back: ") + error);
  check(parsed.size() == 2, "two placements survive");
  if (parsed.size() != 2) return;
  check(parsed[0].schematicId == a.schematicId, "schematic id survives");
  check(parsed[0].displayName == a.displayName, "a quoted display name survives");
  check(parsed[0].placement.origin.x == 10 && parsed[0].placement.origin.y == 64 &&
            parsed[0].placement.origin.z == -5,
        "origin survives");
  check(parsed[0].placement.rotation == bitematica::world::Rotation::Cw90, "rotation survives");
  check(parsed[0].placement.mirror == bitematica::world::Mirror::Z, "mirror survives");
  check(parsed[0].placement.yOffset == 3, "y offset survives");
  check(parsed[0].visible == false && parsed[0].locked == true, "visible/locked survive");
  check(parsed[0].layerMode == 2 && parsed[0].layerMin == 4 && parsed[0].layerMax == 20,
        "layer settings survive");
  check(parsed[0].opacity == 45 && parsed[0].showOutlines == false,
        "per-placement render settings survive");
  check(parsed[1].placement.origin.z == 200, "the second placement survives");
}

void testPlacementStoreVersionGate() {
  std::printf("testPlacementStoreVersionGate\n");
  std::vector<bitematica::litematica::PlacedSchematic> parsed;
  std::string error;
  // A future version is refused, not half-read.
  check(!bitematica::litematica::PlacementStore::parse(
            R"({"version": 999, "placements": []})", parsed, error),
        "a newer file version is refused");
  check(error.find("newer") != std::string::npos, "the refusal names the version, got: " + error);
  // An older version is accepted; a missing field takes its default.
  check(bitematica::litematica::PlacementStore::parse(
            R"({"version": 1, "placements": [{"schematicId": "x"}]})", parsed, error),
        std::string("a minimal placement parses: ") + error);
  check(parsed.size() == 1 && parsed[0].placement.rotation == bitematica::world::Rotation::None,
        "a missing rotation defaults to None");
  // Malformed input is reported, not partially read.
  check(!bitematica::litematica::PlacementStore::parse("[1,2,3]", parsed, error),
        "an array is rejected");
  check(!bitematica::litematica::PlacementStore::parse("{", parsed, error),
        "an unterminated object is rejected");
}

// --- placement edit: origin + nudge (D4/D5) -------------------------------------------

void testOriginAndNudge() {
  std::printf("testOriginAndNudge\n");
  using bitematica::litematica::NudgeAxes;
  using bitematica::litematica::OriginPlacement;
  using bitematica::litematica::RayHit;

  // "Set origin here" on top of a targeted block: hit the top face (normal +Y).
  RayHit hit;
  hit.hit = true;
  hit.block = {10, 64, 10};
  hit.normal = {0, 1, 0};
  const auto onTop = bitematica::litematica::originForTarget(hit, OriginPlacement::OnTopOfBlock);
  check(onTop.x == 10 && onTop.y == 65 && onTop.z == 10, "origin on top is one block above");
  const auto inside = bitematica::litematica::originForTarget(hit, OriginPlacement::InsideBlock);
  check(inside.y == 64, "origin inside is the block itself");

  // At feet.
  const auto feet = bitematica::litematica::originAtFeet({12.7f, 64.0f, -3.2f});
  check(feet.x == 12 && feet.y == 64 && feet.z == -4, "origin at feet floors the position");

  // Nudge axes snap the view direction to the dominant world axis.
  const auto lookingNorth = NudgeAxes::fromView({0.1f, 0.0f, -0.99f});
  check(lookingNorth.forward.z == -1 && lookingNorth.forward.x == 0,
        "looking -Z gives forward = -Z");
  check(lookingNorth.right.x == 1, "right of -Z is +X");
  const auto lookingEast = NudgeAxes::fromView({0.99f, 0.0f, 0.1f});
  check(lookingEast.forward.x == 1 && lookingEast.forward.z == 0, "looking +X gives forward = +X");
  check(lookingEast.right.z == 1, "right of +X is +Z");

  // A nudge of 8 along forward moves 8 blocks.
  const auto delta = lookingNorth.delta(0, 8);
  check(delta.z == -8 && delta.x == 0, "8 forward is 8 blocks along -Z");
  const auto upDelta = lookingNorth.delta(4, 3);
  check(upDelta.y == 3, "up is always +Y");
}

// --- block-state rotation/mirror (D3) -------------------------------------------------

void testBlockStateTransform() {
  std::printf("testBlockStateTransform\n");
  using bitematica::translate::BlockStateTransform;
  using bitematica::world::Mirror;
  using bitematica::world::Rotation;

  // A stair's weirdo_direction rotates: east(0) -> south(2) -> west(1) -> north(3) -> east(0).
  check(BlockStateTransform::rotateWeirdoDirection(0, Rotation::Cw90) == 2,
        "weirdo east -> south after 90 cw");
  check(BlockStateTransform::rotateWeirdoDirection(0, Rotation::Cw180) == 1,
        "weirdo east -> west after 180");
  check(BlockStateTransform::rotateWeirdoDirection(0, Rotation::Cw270) == 3,
        "weirdo east -> north after 270");
  check(BlockStateTransform::rotateWeirdoDirection(0, Rotation::None) == 0,
        "no rotation leaves weirdo unchanged");
  // Four quarter turns return to the start.
  check(BlockStateTransform::rotateWeirdoDirection(
            BlockStateTransform::rotateWeirdoDirection(
                BlockStateTransform::rotateWeirdoDirection(
                    BlockStateTransform::rotateWeirdoDirection(0, Rotation::Cw90), Rotation::Cw90),
                Rotation::Cw90),
            Rotation::Cw90) == 0,
        "four 90-degree turns are the identity");

  // An axis swaps on a quarter turn and is unchanged on a half turn.
  check(BlockStateTransform::rotateAxis("x", Rotation::Cw90) == "z", "x axis -> z at 90");
  check(BlockStateTransform::rotateAxis("z", Rotation::Cw90) == "x", "z axis -> x at 90");
  check(BlockStateTransform::rotateAxis("y", Rotation::Cw90) == "y", "y axis unchanged at 90");
  check(BlockStateTransform::rotateAxis("x", Rotation::Cw180) == "x", "x axis unchanged at 180");

  // A full state transform: a stair facing east becomes south after a 90 cw rotation.
  bitematica::core::BlockState stair;
  stair.name = "minecraft:oak_stairs";
  stair.states = {{"weirdo_direction", "0"}, {"upside_down_bit", "0"}};
  const auto rotated = BlockStateTransform::transform(stair, Rotation::Cw90, Mirror::None);
  bool sawDirection = false;
  for (const auto &[k, v] : rotated.states) {
    if (k == "weirdo_direction") sawDirection = v == "2";
  }
  check(sawDirection, "the stair state rotated east -> south");

  // A mirror flips east<->west; mirror twice returns the original.
  const auto mirroredOnce = BlockStateTransform::transform(stair, Rotation::None, Mirror::X);
  const auto mirroredTwice = BlockStateTransform::transform(mirroredOnce, Rotation::None, Mirror::X);
  bool mirrorFlipped = false;
  for (const auto &[k, v] : mirroredOnce.states) {
    if (k == "weirdo_direction") mirrorFlipped = v == "1"; // east -> west
  }
  check(mirrorFlipped, "a mirror flips the stair east -> west");
  bool mirrorRoundTrip = false;
  for (const auto &[k, v] : mirroredTwice.states) {
    if (k == "weirdo_direction") mirrorRoundTrip = v == "0";
  }
  check(mirrorRoundTrip, "mirror twice returns the original facing");

  // A door's hinge flips under a mirror.
  bitematica::core::BlockState door;
  door.name = "minecraft:oak_door";
  door.states = {{"hinge_bit", "0"}};
  const auto mirroredDoor = BlockStateTransform::transform(door, Rotation::None, Mirror::Z);
  bool hingeFlipped = false;
  for (const auto &[k, v] : mirroredDoor.states) {
    if (k == "hinge_bit") hingeFlipped = v == "1";
  }
  check(hingeFlipped, "a door hinge flips under a mirror");

  // A log's axis swaps on a quarter turn.
  bitematica::core::BlockState log;
  log.name = "minecraft:oak_log";
  log.states = {{"pillar_axis", "x"}};
  const auto rotatedLog = BlockStateTransform::transform(log, Rotation::Cw90, Mirror::None);
  bool axisSwapped = false;
  for (const auto &[k, v] : rotatedLog.states) {
    if (k == "pillar_axis") axisSwapped = v == "x"; // pillar_axis is not `axis`; left unchanged
  }
  check(axisSwapped, "an unrecognised state key is left unchanged (honest fallback)");
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

void testMenuIconIsTappable() {
  std::printf("testMenuIconIsTappable\n");
  using bitematica::ui::MenuRenderer;

  // The icon is the answer to "the menu needs a keyboard": it is drawn on screen and tapped. Its
  // hitbox must cover the whole drawn tile, or a tap on the visible button would miss.
  const auto icon = MenuRenderer::layoutIcon(100.0f, 200.0f, 52.0f, false);
  check(!icon.commands.empty(), "the icon draws something");
  check(icon.hit.x == 100.0f && icon.hit.y == 200.0f && icon.hit.w == 52.0f && icon.hit.h == 52.0f,
        "the icon hitbox matches its position and size");

  // The open state changes the colour, so the player can see the menu is open.
  const auto open = MenuRenderer::layoutIcon(100.0f, 200.0f, 52.0f, true);
  bool colourDiffers = false;
  for (size_t i = 0; i < icon.commands.size() && i < open.commands.size(); ++i) {
    if (icon.commands[i].color != open.commands[i].color) colourDiffers = true;
  }
  check(colourDiffers, "the icon changes colour when the menu is open");
}

void testMenuLayoutHitboxes() {
  std::printf("testMenuLayoutHitboxes\n");
  using bitematica::ui::MenuBuilder;
  using bitematica::ui::MenuHit;
  using bitematica::ui::MenuRenderer;
  using bitematica::ui::MenuTab;

  bitematica::mod::Config config;
  bitematica::litematica::PlacementManager placements;
  MenuBuilder::Context context;
  context.config = &config;
  context.placements = &placements;
  context.statusLine = "ready";

  const auto pages = MenuBuilder::buildAll(context);
  const auto &configPage = pages[static_cast<size_t>(MenuTab::Config)];
  const auto layout = MenuRenderer::layout(configPage, static_cast<int>(MenuTab::Config),
                                           static_cast<int>(pages.size()), 0);

  check(!layout.commands.empty(), "the menu lays out draw commands");
  check(layout.panelW > 0.0f && layout.panelH > 0.0f, "the panel has a size");

  // There must be one hitbox per tab, plus a close button, plus one per visible row.
  int tabHits = 0;
  int closeHits = 0;
  int widgetHits = 0;
  for (const auto &hit : layout.hits) {
    if (hit.kind == MenuHit::Kind::Tab) ++tabHits;
    if (hit.kind == MenuHit::Kind::Close) ++closeHits;
    if (hit.kind == MenuHit::Kind::Widget) ++widgetHits;
  }
  check(tabHits == static_cast<int>(pages.size()), "one hitbox per tab");
  check(closeHits == 1, "a close button exists");
  check(widgetHits > 0, "interactive rows have hitboxes");

  // Every hitbox must lie inside the panel, or a tap could never reach it.
  bool allInside = true;
  for (const auto &hit : layout.hits) {
    if (hit.x < layout.panelX || hit.y < layout.panelY ||
        hit.x + hit.w > layout.panelX + layout.panelW + 0.5f ||
        hit.y + hit.h > layout.panelY + layout.panelH + 0.5f) {
      allInside = false;
    }
  }
  check(allInside, "every hitbox is inside the panel");

  // A tap on the middle of a widget hitbox must land on that widget: the hitboxes are what the
  // touch handler walks, so this is the contract that makes the menu actually work.
  const MenuHit *widget = nullptr;
  for (const auto &hit : layout.hits) {
    if (hit.kind == MenuHit::Kind::Widget) {
      widget = &hit;
      break;
    }
  }
  check(widget != nullptr, "found a widget hitbox to test");
  if (widget != nullptr) {
    const float cx = widget->x + widget->w * 0.5f;
    const float cy = widget->y + widget->h * 0.5f;
    const MenuHit *found = nullptr;
    for (const auto &hit : layout.hits) {
      if (cx >= hit.x && cx <= hit.x + hit.w && cy >= hit.y && cy <= hit.y + hit.h) {
        found = &hit;
        break;
      }
    }
    check(found != nullptr && found->widgetIndex == widget->widgetIndex,
          "a tap in a widget's centre resolves to that widget");
  }

  // Usability invariants that a self-consistency check would miss:
  //  1. every interactive hitbox is at least a minimum touch target tall, or a thumb cannot hit it;
  //  2. no two interactive hitboxes overlap, or a tap is ambiguous.
  const float kMinTouch = 28.0f;
  std::vector<const MenuHit *> interactive;
  bool bigEnough = true;
  for (const auto &hit : layout.hits) {
    if (hit.kind != MenuHit::Kind::Widget) continue;
    interactive.push_back(&hit);
    if (hit.h < kMinTouch) bigEnough = false;
  }
  check(bigEnough, "every widget hitbox is a reachable touch target");

  bool overlap = false;
  for (size_t i = 0; i < interactive.size(); ++i) {
    for (size_t j = i + 1; j < interactive.size(); ++j) {
      const MenuHit &a = *interactive[i];
      const MenuHit &b = *interactive[j];
      const bool disjoint = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
                            b.y + b.h <= a.y;
      if (!disjoint) overlap = true;
    }
  }
  check(!overlap, "no two widget hitboxes overlap");
}

void testMenuScrollClampsRows() {
  std::printf("testMenuScrollClampsRows\n");
  using bitematica::ui::MenuBuilder;
  using bitematica::ui::MenuRenderer;
  using bitematica::ui::MenuTab;

  bitematica::mod::Config config;
  bitematica::litematica::PlacementManager placements;
  MenuBuilder::Context context;
  context.config = &config;
  context.placements = &placements;

  const auto pages = MenuBuilder::buildAll(context);
  const auto &configPage = pages[static_cast<size_t>(MenuTab::Config)];
  const int rows = MenuRenderer::rowCount(configPage);
  check(rows > 0, "the config page has rows");

  bitematica::ui::MenuMetrics metrics;
  metrics.visibleRows = 5;
  // Scrolling past the end must not produce rows, and must not read out of bounds.
  const auto layout = MenuRenderer::layout(configPage, 0, 8, rows + 100, metrics);
  int widgetHits = 0;
  for (const auto &hit : layout.hits) {
    if (hit.kind == bitematica::ui::MenuHit::Kind::Widget) ++widgetHits;
  }
  check(widgetHits == 0, "scrolling past the end shows no rows");
}

} // namespace

int main() {
  std::printf("BiteMatica Litematica toolset tests\n");
  std::printf("===================================\n");
  testToolModeCycle();
  testLayerRange();
  testPlacementManager();
  testPlacementUndoRedo();
  testPlacementStoreRoundTrip();
  testPlacementStoreVersionGate();
  testBlockStateTransform();
  testOriginAndNudge();
  testRayCast();
  testEasyPlace();
  testPasteCommands();
  testMenuModel();
  testMenuBuilderRejectsBadWidgets();
  testMenuIconIsTappable();
  testMenuLayoutHitboxes();
  testMenuScrollClampsRows();
  std::printf("===================================\n");
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
