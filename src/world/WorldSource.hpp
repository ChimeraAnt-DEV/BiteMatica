#pragma once

#include <functional>
#include <string>

#include "core/BlockState.hpp"
#include "core/BlockPos.hpp"

namespace bitematica::world {

/// Reads a block from the live world.
///
/// This is the seam the verification pass needs. The preloader does not expose a block accessor, so
/// the mod installs a provider that resolves `BlockSource::getBlock` by signature scan (the same
/// approach the SDK's own game hooks use) and returns the block state at a world position. With no
/// provider installed, verification reports "unavailable" rather than guessing that every block is
/// missing.
///
/// Kept as an abstract interface so the whole verification pass is host-testable with a fake world,
/// which is where the interesting cases (missing / wrong / extra) actually live.
class WorldSource {
public:
  virtual ~WorldSource() = default;

  /// Reads the block at a world position.
  /// @return true when a live block was read; false when the world is not readable (out of range,
  ///         no provider, or the provider is not installed on this build).
  virtual bool readBlock(const core::BlockPos &position, core::BlockState &out) = 0;

  /// True when this source can currently read the world.
  virtual bool available() const = 0;
};

/// The process-wide world source. Pass null to clear it.
void setWorldSource(WorldSource *source);

/// The installed source, or nullptr.
WorldSource *worldSource();

} // namespace bitematica::world
