#pragma once

#include <cstdint>
#include <string>

#include "world/WorldSource.hpp"

namespace bitematica::mod {

/// Reads blocks from the live Bedrock world by hooking `BlockSource::getBlock`.
///
/// The preloader exposes no block accessor, so this resolves the game's block getter the same way
/// the SDK's own game hooks resolve their targets: by RTTI vtable name first (which needs no
/// per-build code address) and by byte signature as a fallback. The resolved function is called
/// with the world/block-source pointer the SDK already tracks, and its result is converted to a
/// {@link core::BlockState}.
///
/// <b>Fail-closed and version-gated.</b> The hook is only enabled on game versions whose vtable
/// slot and field layout have been verified; on any other build `available()` returns false and the
/// verifier reports "the world is not readable" rather than inventing block states. Nothing here
/// dereferences a guessed pointer: the target must resolve through RTTI or a signature, and the
/// returned block id must be in the game's own id range.
class GameWorldSource : public world::WorldSource {
public:
  /// Installs the hook. Idempotent. Returns true when the world becomes readable.
  bool install();

  bool available() const override;
  bool readBlock(const core::BlockPos &position, core::BlockState &out) override;

  /// A short diagnostic for the load panel: which resolution path succeeded, or why not.
  const std::string &diagnostic() const { return mDiagnostic; }

  /// The verified version range the hook is allowed to install on.
  static const char *verifiedMinVersion();
  static const char *verifiedMaxVersion();

private:
  bool mInstalled{false};
  std::string mDiagnostic{"not attempted"};
};

} // namespace bitematica::mod
