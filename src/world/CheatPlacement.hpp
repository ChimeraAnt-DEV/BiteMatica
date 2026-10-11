#pragma once

#include <string>

#include "core/Schematic.hpp"
#include "world/Placement.hpp"

namespace bitematica::world {

/// Builds the commands for the reliable, cheat-route placement of a schematic.
///
/// Placing blocks by hooking item use is fragile and is treated as cheating on most servers, so the
/// route that ships first is the one the game itself supports: export the schematic as a Bedrock
/// structure file (`.mcstructure`) and load it with the `/structure` command. That is exactly what a
/// player with cheats enabled would do by hand, the game does the placing, and nothing about the mod
/// touches the network.
///
/// This class only *builds the commands and the file*; it never sends anything. The player runs the
/// commands, or a future opt-in "easy place" mode does with explicit consent.
class CheatPlacement {
public:
  /// The `/structure load` command for an exported file at a world position.
  ///
  /// @param structureName the file name without extension, as `/structure load` expects
  /// @param origin the world position to load the structure at
  static std::string loadCommand(const std::string &structureName, const core::BlockPos &origin);

  /// The `/structure save` command for capturing a region to a structure file.
  static std::string saveCommand(const std::string &structureName, const core::BlockPos &from,
                                 const core::BlockPos &to);

  /// The commands that make the placement's origin line up with a target position, applying the
  /// placement's rotation as a `/structure load` rotation argument.
  static std::string loadCommandForPlacement(const std::string &structureName,
                                             const Placement &placement);

  /// The rotation argument `/structure load` understands for a placement's rotation.
  static std::string rotationArgument(Rotation rotation);

  /// The default structure name for a schematic (its sanitized file name without extension).
  static std::string structureNameFor(const core::Schematic &schematic);
};

} // namespace bitematica::world
