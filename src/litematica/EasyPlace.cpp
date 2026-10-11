#include "litematica/EasyPlace.hpp"

#include <cstdlib>

namespace bitematica::litematica {
namespace {

/// True when the state's block carries a horizontal facing that can be oriented.
bool hasHorizontalFacing(const core::BlockState &state) {
  for (const auto &[key, value] : state.states) {
    (void)value;
    if (key == "facing" || key == "weirdo_direction" || key == "direction") return true;
  }
  return false;
}

/// The Bedrock `weirdo_direction` for a Java `facing` string (inverted, per the translation table).
std::string javaFacingToBedrock(const std::string &facing) {
  if (facing == "east") return "0";
  if (facing == "west") return "1";
  if (facing == "south") return "2";
  if (facing == "north") return "3";
  return "0";
}

} // namespace

core::BlockPos EasyPlace::choosePlacementFace(const core::BlockPos &target,
                                              const core::BlockPos &playerBlock) {
  // Click the neighbour that is closest to the player, so the block is placed onto the near face.
  const int dx = playerBlock.x - target.x;
  const int dy = playerBlock.y - target.y;
  const int dz = playerBlock.z - target.z;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);
  const int adz = std::abs(dz);
  if (adx >= ady && adx >= adz) return core::BlockPos{dx >= 0 ? 1 : -1, 0, 0};
  if (adz >= ady) return core::BlockPos{0, 0, dz >= 0 ? 1 : -1};
  return core::BlockPos{0, dy >= 0 ? 1 : -1, 0};
}

core::BlockState EasyPlace::orientTowards(const core::BlockState &state, const core::BlockPos &from,
                                          const core::BlockPos &to) {
  if (!hasHorizontalFacing(state)) return state;
  // The block should face away from the player: pick the cardinal from `from` to `to`.
  const int dx = to.x - from.x;
  const int dz = to.z - from.z;
  std::string facing;
  if (std::abs(dx) >= std::abs(dz)) {
    facing = dx >= 0 ? "east" : "west";
  } else {
    facing = dz >= 0 ? "south" : "north";
  }

  core::BlockState out = state;
  for (auto &[key, value] : out.states) {
    if (key == "facing") {
      value = facing;
    } else if (key == "weirdo_direction") {
      value = javaFacingToBedrock(facing);
    } else if (key == "direction") {
      value = javaFacingToBedrock(facing);
    }
  }
  return out;
}

EasyPlaceDecision EasyPlace::decide(const core::BlockPos &hitBlock, const core::BlockPos &hitFace,
                                    const core::Schematic &schematic,
                                    const world::Placement &placement, world::WorldSource &world,
                                    bool allowBreaking) {
  EasyPlaceDecision decision;

  // The block Easy Place would place onto is the neighbour on the clicked face.
  const core::BlockPos target{hitBlock.x + hitFace.x, hitBlock.y + hitFace.y,
                              hitBlock.z + hitFace.z};
  decision.target = target;
  decision.clickFace = core::BlockPos{-hitFace.x, -hitFace.y, -hitFace.z};
  decision.support = hitBlock;

  core::LocalPos local{};
  if (!placement.toLocal(schematic, target, local)) {
    decision.kind = EasyPlaceDecision::Kind::None;
    decision.reason = "no schematic block here";
    return decision;
  }
  const core::BlockState &wanted = schematic.stateAt(local.x, local.y, local.z);
  if (wanted.isAir()) {
    decision.kind = EasyPlaceDecision::Kind::None;
    decision.reason = "the schematic wants air here";
    return decision;
  }

  core::BlockState found;
  if (world.available() && world.readBlock(target, found)) {
    if (!found.isAir() && found.baseName() == wanted.baseName()) {
      decision.kind = EasyPlaceDecision::Kind::Correct;
      decision.state = wanted;
      decision.reason = "already placed";
      return decision;
    }
    if (!found.isAir()) {
      decision.kind = allowBreaking ? EasyPlaceDecision::Kind::Wrong : EasyPlaceDecision::Kind::None;
      decision.state = wanted;
      decision.reason = allowBreaking ? "a different block is in the way"
                                      : "a different block is in the way (breaking disabled)";
      return decision;
    }
  }

  // Orient the block so it faces away from where the player would stand.
  const core::BlockPos playerBlock{target.x + decision.clickFace.x, target.y + decision.clickFace.y,
                                   target.z + decision.clickFace.z};
  decision.state = orientTowards(wanted, playerBlock, target);
  decision.kind = EasyPlaceDecision::Kind::Place;
  decision.reason = "place " + wanted.baseName();
  return decision;
}

} // namespace bitematica::litematica
