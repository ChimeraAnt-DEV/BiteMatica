#pragma once

#include <string>

#include "core/BlockPos.hpp"
#include "core/Schematic.hpp"
#include "world/Placement.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::litematica {

/// What Easy Place should do at the block the player is looking at.
///
/// Litematica's Easy Place is the survival build assistant: look at a ghost block, hold right-click,
/// and it places the correct block from your inventory, choosing the placement orientation so
/// stairs, slabs and the like face the right way. This computes the *decision* — which block is
/// needed, whether it is already correct, and what to place against — as a pure function of the
/// schematic, the placement and the world. Actually issuing the placement is the caller's job, so
/// this stays host-testable and the mod never silently places anything.
struct EasyPlaceDecision {
  enum class Kind {
    None,      ///< nothing to do here
    Place,     ///< place `state` at `target`
    Correct,   ///< the world already matches
    Wrong,     ///< a different block is in the way; must be broken first
  };

  Kind kind{Kind::None};
  core::BlockPos target{};
  core::BlockState state{};
  /// The face to click, as a normal, chosen so the placement lands on `target`.
  core::BlockPos clickFace{};
  /// The block the player should click on (target + face), for the game's own placement logic.
  core::BlockPos support{};
  std::string reason;
};

/// Computes Easy Place decisions.
class EasyPlace {
public:
  /// Decides what to do for the block under the crosshair.
  ///
  /// @param hitBlock the world block the crosshair is on
  /// @param hitFace the face that was entered
  /// @param schematic the loaded schematic
  /// @param placement where it sits
  /// @param world the live world, for the "already correct?" check
  /// @param allowBreaking whether a wrong block may be reported as breakable
  static EasyPlaceDecision decide(const core::BlockPos &hitBlock, const core::BlockPos &hitFace,
                                  const core::Schematic &schematic,
                                  const world::Placement &placement,
                                  world::WorldSource &world, bool allowBreaking);

  /// Chooses the placement face for a block so it ends up at `target`, given where the player is
  /// standing. This is the "stairs face the right way" logic: the face is picked from the direction
  /// the player approaches from, and the state's `facing` is set to match.
  static core::BlockPos choosePlacementFace(const core::BlockPos &target,
                                            const core::BlockPos &playerBlock);

  /// Applies a `facing`/`weirdo_direction` state to a block so it points away from the player.
  static core::BlockState orientTowards(const core::BlockState &state,
                                        const core::BlockPos &from, const core::BlockPos &to);
};

} // namespace bitematica::litematica
