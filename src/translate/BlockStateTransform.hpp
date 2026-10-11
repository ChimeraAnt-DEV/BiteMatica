#pragma once

#include <string>
#include <vector>

#include "core/BlockState.hpp"
#include "world/Placement.hpp"

namespace bitematica::translate {

/// Rotates and mirrors a block *state* to match a placement transform (D3).
///
/// Rotating a placement moves the blocks, but it must also turn each block's own state: a stair
/// facing north must face east after a 90-degree clockwise rotation, a log's axis swaps from X to Z,
/// a door's hinge flips on a mirror. Without this, a rotated schematic is a rotated *shape* full of
/// blocks that point the wrong way — the exact thing Litematica's rotation is supposed to get right.
///
/// The mapping is done on the Bedrock state names (`weirdo_direction`, `upside_down_bit`, `axis`,
/// `hinge_bit`, `direction`, ...), because the schematic has already been translated to Bedrock by
/// `BlockTranslation`. States the table does not understand are left unchanged, which is honest: a
/// state we cannot rotate is better left alone than guessed at.
class BlockStateTransform {
public:
  /// Applies the placement's rotation and mirror to a Bedrock block state.
  static core::BlockState transform(const core::BlockState &state, world::Rotation rotation,
                                    world::Mirror mirror);

  /// The Bedrock `weirdo_direction` (0=east,1=west,2=south,3=north) after a rotation.
  /// Exposed for testing the direction math on its own.
  static int rotateWeirdoDirection(int direction, world::Rotation rotation);

  /// The Bedrock `direction` (0=east,1=south,2=west,3=north) after a rotation.
  static int rotateDirection(int direction, world::Rotation rotation);

  /// The `axis` state (`x`/`y`/`z`) after a rotation.
  static std::string rotateAxis(const std::string &axis, world::Rotation rotation);
};

} // namespace bitematica::translate
