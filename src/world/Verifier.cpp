#include "world/Verifier.hpp"

namespace bitematica::world {
namespace {

/// Compares two block states for "is this the block the schematic wants".
///
/// The comparison ignores block-state properties by default: Bedrock's state names for the same
/// visual block differ from Java's (see the translation table), so requiring an exact state match
/// would flag nearly every stair and slab as `Wrong`. Type-level matching is the honest default;
/// state-level matching is available where the translation is confident.
bool sameBlock(const core::BlockState &wanted, const core::BlockState &found) {
  return wanted.baseName() == found.baseName();
}

} // namespace

uint32_t Verifier::colorFor(BlockStatus status) {
  switch (status) {
    case BlockStatus::Correct: return 0x8000C060u;  // translucent green
    case BlockStatus::Missing: return 0xC0FF4040u;  // red, needs placing
    case BlockStatus::Wrong: return 0xC0FFA030u;    // amber, wrong block
    case BlockStatus::Extra: return 0xC04080FFu;    // blue, should be removed
    case BlockStatus::Unknown: return 0x40808080u;  // dim grey, unreadable
  }
  return 0x40808080u;
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
          status = BlockStatus::Unknown;
        } else if (wantsAir) {
          status = found.isAir() ? BlockStatus::Correct : BlockStatus::Extra;
        } else if (found.isAir()) {
          status = BlockStatus::Missing;
        } else if (sameBlock(wanted, found)) {
          status = BlockStatus::Correct;
        } else {
          status = BlockStatus::Wrong;
        }

        const size_t index = static_cast<size_t>(schematic.indexOf(x, y, z));
        if (index < result.statuses.size()) result.statuses[index] = status;
        switch (status) {
          case BlockStatus::Correct: ++result.correct; break;
          case BlockStatus::Missing: ++result.missing; break;
          case BlockStatus::Wrong: ++result.wrong; break;
          case BlockStatus::Extra: ++result.extra; break;
          case BlockStatus::Unknown: ++result.unknown; break;
        }
      }
    }
  }
  return result;
}

} // namespace bitematica::world
