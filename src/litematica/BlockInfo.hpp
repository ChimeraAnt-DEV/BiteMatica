#pragma once

#include <string>

#include "core/BlockState.hpp"
#include "litematica/BlockTargeting.hpp"
#include "world/Placement.hpp"
#include "world/Verifier.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::litematica {

/// What the schematic wants at a targeted block, versus what the world has (E5 / F11).
struct BlockInfo {
  core::BlockPos position{};
  /// True when the position falls inside a placement's box.
  bool inPlacement{false};
  /// The placement index the position belongs to, or -1.
  int placementIndex{-1};
  /// What the schematic wants there (air when the placement has nothing there).
  core::BlockState wanted{};
  bool hasWanted{false};
  /// What the world has there, when the world is readable.
  core::BlockState found{};
  bool hasFound{false};
  /// The verdict, computed the same way the verifier computes it.
  world::BlockStatus verdict{world::BlockStatus::Unknown};
  /// True when the schematic block sits in a sub-region (multi-region litematics), and which.
  int regionIndex{-1};

  /// A one-line description for the info panel.
  std::string describe() const;
};

/// Builds the block info for a world position against the active placement (E5).
///
/// The world side is only read when the source is available; otherwise `hasFound` is false and the
/// verdict is `Unknown` — the panel then shows "world not readable" rather than inventing a state.
/// This is the same fail-closed rule the verifier follows.
inline BlockInfo describeTarget(const core::BlockPos &position, const core::Schematic &schematic,
                                const world::Placement &placement, int placementIndex,
                                world::WorldSource *source) {
  BlockInfo info;
  info.position = position;
  info.placementIndex = placementIndex;

  core::LocalPos local{};
  if (placement.toLocal(schematic, position, local)) {
    info.inPlacement = true;
    info.wanted = schematic.stateAt(local.x, local.y, local.z);
    info.hasWanted = true;
  }

  if (source != nullptr && source->available()) {
    core::BlockState found;
    if (source->readBlock(position, found)) {
      info.found = found;
      info.hasFound = true;
    }
  }

  // The verdict mirrors the verifier's logic so the panel and the coloured ghost never disagree.
  if (!info.hasFound) {
    info.verdict = world::BlockStatus::Unknown;
  } else if (!info.inPlacement) {
    info.verdict = info.found.isAir() ? world::BlockStatus::Correct : world::BlockStatus::Extra;
  } else if (info.wanted.isAir()) {
    info.verdict = info.found.isAir() ? world::BlockStatus::Correct : world::BlockStatus::Extra;
  } else if (info.found.isAir()) {
    info.verdict = world::BlockStatus::Missing;
  } else if (info.found.baseName() != info.wanted.baseName()) {
    info.verdict = world::BlockStatus::WrongBlock;
  } else {
    info.verdict = world::BlockStatus::Correct;
  }
  return info;
}

/// The status name for a verdict, for the panel.
inline const char *verdictName(world::BlockStatus status) {
  switch (status) {
    case world::BlockStatus::Correct: return "correct";
    case world::BlockStatus::Missing: return "missing";
    case world::BlockStatus::WrongBlock: return "wrong block";
    case world::BlockStatus::WrongState: return "wrong state";
    case world::BlockStatus::Extra: return "extra";
    case world::BlockStatus::Unknown: return "unknown (world not readable)";
  }
  return "unknown";
}

inline std::string BlockInfo::describe() const {
  std::string out = "at " + std::to_string(position.x) + "," + std::to_string(position.y) + "," +
                    std::to_string(position.z) + "  ";
  if (inPlacement && hasWanted) {
    out += "wants " + wanted.name;
    out += "  ";
  } else {
    out += "not in a placement  ";
  }
  if (hasFound) {
    out += "world " + found.name;
  } else {
    out += "world unreadable";
  }
  out += "  [";
  out += verdictName(verdict);
  out += "]";
  return out;
}

} // namespace bitematica::litematica
