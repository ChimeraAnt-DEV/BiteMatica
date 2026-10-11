#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "world/WorldSource.hpp"

namespace bitematica::mod {

/// Reads blocks from the live Bedrock world by calling `BlockSource::getBlock`.
///
/// **This class does not know the vtable slot or the calling convention for `getBlock` on any
/// particular game build, and it does not guess them.** A guessed slot calls an unrelated function
/// and faults the game thread; a guessed ABI corrupts the stack. Both are worse than not verifying,
/// so the design is fail-closed at three levels:
///
///   1. **Version gate.** The slot is only used when the configured `gameVersionTag` matches the
///      running game version. An unverified build stays off.
///   2. **A confirmed reader.** Even with a matching version, the mod does not call the resolved
///      address itself. A `BlockReader` — a small function with a *known, human-confirmed* signature
///      — must be installed first (`setBlockReader`). Until then `available()` is false and the
///      verifier reports "the world is not readable". `docs/FINDING_GETBLOCK.md` explains how to
///      confirm the slot and write that reader for a new build.
///   3. **A sanity probe.** On the first call, the reader is exercised at positions whose values are
///      known (the block under the player's feet is not air; a position 400 blocks above is air in
///      the overworld). A reader that returns implausible values is disabled for the session with a
///      named reason, so a mis-set slot cannot silently colour the whole schematic wrong.
///
/// Everything except the native call itself is host-testable: the version gate, the probe logic and
/// the error budget are pure functions over the reader's output.
class GameWorldSource : public world::WorldSource {
public:
  /// A confirmed block reader. Returns true and fills `out` when a block was read.
  using BlockReader = std::function<bool(const core::BlockPos &, core::BlockState &)>;

  /// Installs the reader. The human supplies this once they have confirmed the slot and ABI for
  /// their build (see `docs/FINDING_GETBLOCK.md`). Pass nullptr to clear it.
  static void setBlockReader(BlockReader reader);

  /// True when a reader is installed.
  static bool hasBlockReader();

  /// Configures the source from the mod config.
  ///
  /// @param getBlockSlot the confirmed `BlockSource::getBlock` slot, or 0/-1 for "not verified"
  /// @param configuredVersionTag the game version the slot was verified against, e.g. "1.26.51"
  /// @param runningVersionTag the running game version, or "" when it could not be read
  void configure(int getBlockSlot, const std::string &configuredVersionTag,
                 const std::string &runningVersionTag);

  bool available() const override;
  bool readBlock(const core::BlockPos &position, core::BlockState &out) override;

  /// A short diagnostic for the load panel: which gate is blocking, or which path succeeded.
  const std::string &diagnostic() const { return mDiagnostic; }

  /// The number of consecutive read failures before the source is disabled for the session (F9).
  static constexpr int kErrorBudget = 8;

  // --- pure helpers, host-testable ---

  /// True when a configured version tag is usable for a running version.
  ///
  /// A blank configured tag means "not verified for any build", so the gate is closed. When a
  /// running tag is available, the two must match (a prefix match, so "1.26.51" matches a running
  /// "1.26.51.1") — a mismatch is closed. When the running tag could not be read (the SDK exposes
  /// the mod version, not the game version), a non-blank configured tag is treated as the user's
  /// explicit acknowledgement, and the gate opens **but the diagnostic says the version was not
  /// auto-verified** — the sanity probe is then the real safety net.
  ///
  /// Header-only so it is host-testable without the preloader.
  static bool versionGateOpen(const std::string &configuredTag, const std::string &runningTag) {
    if (configuredTag.empty()) return false;
    if (runningTag.empty()) return true;
    if (runningTag.compare(0, configuredTag.size(), configuredTag) == 0) return true;
    return configuredTag.compare(0, runningTag.size(), runningTag) == 0;
  }

  /// The result of the sanity probe.
  enum class ProbeResult { Passed, FeetIsAir, SkyIsNotAir, Unreadable };

  /// Runs the plausibility checks against a reader.
  ///
  /// @param feetPosition the block under the player's feet (must not be air in the overworld)
  /// @param skyPosition a position 400 blocks above the player (must be air)
  ///
  /// Header-only so it is host-testable without the preloader.
  static ProbeResult probe(const BlockReader &reader, const core::BlockPos &feetPosition,
                           const core::BlockPos &skyPosition) {
    if (!reader) return ProbeResult::Unreadable;
    core::BlockState feet;
    if (!reader(feetPosition, feet)) return ProbeResult::Unreadable;
    // In the overworld the block under the player's feet is never air (they stand on it).
    if (feet.isAir()) return ProbeResult::FeetIsAir;
    core::BlockState sky;
    if (!reader(skyPosition, sky)) return ProbeResult::Unreadable;
    // 400 blocks above the player is air in the overworld.
    if (!sky.isAir()) return ProbeResult::SkyIsNotAir;
    return ProbeResult::Passed;
  }

  /// Runs the probe against the installed reader, once. Called by the mod glue with the player's
  /// feet and a position 400 blocks above, before the first real read. A failed probe disables the
  /// source for the session with a named reason. Returns true when the probe passed.
  bool runProbe(const core::BlockPos &feetPosition, const core::BlockPos &skyPosition);

private:
  bool mConfigured{false};
  int mSlot{0};
  bool mVersionGateOpen{false};
  std::string mDiagnostic{"not configured"};
  /// Set once a probe fails; the source is off for the rest of the session.
  bool mSessionDisabled{false};
  bool mProbed{false};
  int mConsecutiveErrors{0};
};

} // namespace bitematica::mod
