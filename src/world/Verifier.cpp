#include "world/Verifier.hpp"

namespace bitematica::world {
namespace {

/// Compares two block states, distinguishing "wrong block" from "wrong state".
///
/// The block *type* is the primary check. When the types match, the states are compared too, but
/// only over the keys the two sides share: Bedrock's state names for the same visual block differ
/// from Java's (`facing` -> `weirdo_direction`, `half` -> `upside_down_bit`), and the translation
/// table has already normalised what it can. Comparing the shared keys catches the case that matters
/// — a stair facing the wrong way — without flagging every slab whose extra Bedrock-only bits differ.
///
/// The distinction is what gives the verifier its `WrongState` verdict, which the mismatch palette
/// colours differently from a `WrongBlock`.
enum class StateMatch { Same, WrongState, WrongBlock };

StateMatch compareState(const core::BlockState &wanted, const core::BlockState &found) {
  if (wanted.baseName() != found.baseName()) return StateMatch::WrongBlock;
  for (const auto &[key, value] : wanted.states) {
    for (const auto &[foundKey, foundValue] : found.states) {
      if (foundKey == key && foundValue != value) return StateMatch::WrongState;
    }
  }
  return StateMatch::Same;
}

} // namespace

MismatchPalette MismatchPalette::preset(int index) {
  switch (index) {
    case 1: return colourBlind();
    case 2: return highContrast();
    case 0:
    default: return litematica();
  }
}

uint32_t Verifier::colorFor(BlockStatus status) {
  const MismatchPalette p = MismatchPalette::litematica();
  switch (status) {
    case BlockStatus::Correct: return p.correct;
    case BlockStatus::Missing: return p.missing;
    case BlockStatus::WrongBlock: return p.wrongBlock;
    case BlockStatus::WrongState: return p.wrongState;
    case BlockStatus::Extra: return p.extra;
    case BlockStatus::Unknown: return p.unknown;
  }
  return p.unknown;
}

VerifyResult Verifier::verify(const core::Schematic &schematic, const Placement &placement,
                              WorldSource &source, int layerMin, int layerMax, bool reportExtras) {
  VerifyResult result;
  if (!source.available()) {
    result.available = false;
    result.error = "the world is not readable (no block source installed on this build)";
    return result;
  }
  if (schematic.empty()) {
    result.available = true;
    result.error = "nothing is loaded";
    return result;
  }

  result.available = true;
  result.statuses.assign(schematic.blocks.size(), BlockStatus::Unknown);

  const int sizeX = schematic.sizeX();
  const int sizeY = schematic.sizeY();
  const int sizeZ = schematic.sizeZ();

  for (int y = 0; y < sizeY; ++y) {
    if (y < layerMin || y > layerMax) continue;
    for (int z = 0; z < sizeZ; ++z) {
      for (int x = 0; x < sizeX; ++x) {
        const core::BlockState &wanted = schematic.stateAt(x, y, z);
        const bool wantsAir = wanted.isAir();
        if (wantsAir && !reportExtras) continue;

        const core::BlockPos world = placement.toWorld(schematic, x, y, z);
        core::BlockState found;
        BlockStatus status;
        if (!source.readBlock(world, found)) {
          // An unreadable block (unloaded chunk, no source) is Unknown — never Missing. Treating it
          // as missing would tell the player to build a wall that already exists.
          status = BlockStatus::Unknown;
        } else if (wantsAir) {
          status = found.isAir() ? BlockStatus::Correct : BlockStatus::Extra;
        } else if (found.isAir()) {
          status = BlockStatus::Missing;
        } else {
          switch (compareState(wanted, found)) {
            case StateMatch::Same: status = BlockStatus::Correct; break;
            case StateMatch::WrongState: status = BlockStatus::WrongState; break;
            case StateMatch::WrongBlock: status = BlockStatus::WrongBlock; break;
          }
        }

        const size_t index = static_cast<size_t>(schematic.indexOf(x, y, z));
        if (index < result.statuses.size()) result.statuses[index] = status;
        switch (status) {
          case BlockStatus::Correct: ++result.correct; break;
          case BlockStatus::Missing: ++result.missing; break;
          case BlockStatus::WrongBlock: ++result.wrongBlock; break;
          case BlockStatus::WrongState: ++result.wrongState; break;
          case BlockStatus::Extra: ++result.extra; break;
          case BlockStatus::Unknown: ++result.unknown; break;
        }
      }
    }
  }
  return result;
}

} // namespace bitematica::world
