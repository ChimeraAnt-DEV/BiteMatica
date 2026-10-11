#pragma once

#include <string>
#include <vector>

#include "core/Schematic.hpp"
#include "world/Placement.hpp"

namespace bitematica::litematica {

/// Options that mirror Litematica's paste behaviour.
struct PasteOptions {
  /// Replace existing blocks, or only fill air.
  bool replaceExisting{true};
  /// Skip air in the schematic (don't clear the target).
  bool skipAir{true};
  /// Group adjacent same-block runs into `/fill` instead of one `/setblock` each.
  bool useFill{true};
  /// Maximum blocks a single `/fill` may cover (the command has a size limit).
  int maxFillVolume{32768};
  /// Cap the number of commands produced, so a huge paste cannot spam thousands of lines.
  int maxCommands{20000};
};

/// Builds the commands that place a schematic into the world.
///
/// Litematica's Paste tool writes blocks directly. On Bedrock the equivalent that does not fight
/// the game is the command layer: `/setblock` and `/fill` are what a creative player would type, so
/// the mod generates them and (with cheats on) sends them, exactly as Litematica uses client-side
/// commands when configured to. Nothing here places a block itself.
///
/// The builder is pure: it turns a schematic + placement into a command list, and is host-tested by
/// asserting the command text for a known build.
class PasteJob {
public:
  /// The command list for placing `schematic` at `placement`.
  ///
  /// The placement's rotation and mirror are baked into the world coordinates, so the emitted
  /// commands need no rotation argument. Returns the commands in a stable order (Y, then Z, then X)
  /// so the build goes bottom-up.
  static std::vector<std::string> build(const core::Schematic &schematic,
                                        const world::Placement &placement,
                                        const PasteOptions &options = PasteOptions{});

  /// A single `/setblock` command for one block.
  static std::string setBlockCommand(const core::BlockPos &position,
                                     const core::BlockState &state);

  /// A single `/fill` command for a box of one block type.
  static std::string fillCommand(const core::BlockPos &from, const core::BlockPos &to,
                                 const core::BlockState &state);

  /// The Bedrock block id for a state, including the `["state"=value]` suffix Bedrock accepts in
  /// commands. Java-style states are translated by the caller before this is reached.
  static std::string blockIdFor(const core::BlockState &state);
};

} // namespace bitematica::litematica
