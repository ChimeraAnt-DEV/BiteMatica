#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Schematic.hpp"
#include "world/Placement.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::world {

/// The verification state of one schematic block against the live world.
enum class BlockStatus : uint8_t {
  Correct = 0,     ///< the world block matches what the schematic wants (type and, when checked, state)
  Missing = 1,     ///< the world has air where the schematic wants a block
  WrongBlock = 2,  ///< the world has a different block type there
  WrongState = 3,  ///< the right block type, but the wrong state (facing/half/axis/...)
  Extra = 4,       ///< the world has a block where the schematic wants air
  Unknown = 5,     ///< the world could not be read (no source / unloaded chunk / out of range)
};

/// The result of verifying a placement against the world.
struct VerifyResult {
  bool available{false};
  std::string error;
  long long correct{};
  long long missing{};
  long long wrongBlock{};
  long long wrongState{};
  long long extra{};
  long long unknown{};
  /// Per schematic-local block status, indexed like `Schematic::blocks`. Empty when unavailable.
  std::vector<BlockStatus> statuses;

  long long total() const { return correct + missing + wrongBlock + wrongState + extra + unknown; }
  /// Everything wrong or missing, for the "N to go" counter.
  long long wrong() const { return wrongBlock + wrongState; }
  /// Percentage of checked blocks that are correct, 0 when nothing was checked. `Unknown` (an
  /// unreadable chunk) is deliberately excluded from the denominator: an unloaded chunk is not a
  /// failure to build, and must never be reported as missing.
  int percentComplete() const {
    const long long checked = correct + missing + wrongBlock + wrongState + extra;
    if (checked <= 0) return 0;
    return static_cast<int>((correct * 100) / checked);
  }
};

/// The colour of each verdict, so the palette is data not a magic number scattered in the renderer.
///
/// The default is Litematica-like: blue = missing, red = wrong block, orange = wrong state, purple =
/// extra, green = correct. Colour-blind presets are provided (C6) and the whole palette is
/// configurable.
struct MismatchPalette {
  uint32_t correct{0x8000C060u};
  uint32_t missing{0xC04080FFu};
  uint32_t wrongBlock{0xC0FF4040u};
  uint32_t wrongState{0xC0FFA030u};
  uint32_t extra{0xC08000FFu};
  uint32_t unknown{0x40808080u};

  /// The Litematica-like default.
  static MismatchPalette litematica() { return MismatchPalette{}; }

  /// A deuteranopia/protanopia-friendly preset: blue/orange/yellow/teal/magenta are distinguishable
  /// without relying on red-vs-green.
  static MismatchPalette colourBlind() {
    MismatchPalette p;
    p.correct = 0x8000B0D0u;   // teal
    p.missing = 0xC03070FFu;   // strong blue
    p.wrongBlock = 0xC0FF8000u; // orange
    p.wrongState = 0xC0FFD000u; // yellow
    p.extra = 0xC0C040C0u;     // magenta
    p.unknown = 0x40808080u;
    return p;
  }

  /// High-contrast: opaque, saturated, maximum separation.
  static MismatchPalette highContrast() {
    MismatchPalette p;
    p.correct = 0xFF00FF00u;
    p.missing = 0xFF0000FFu;
    p.wrongBlock = 0xFFFF0000u;
    p.wrongState = 0xFFFFFF00u;
    p.extra = 0xFFFF00FFu;
    p.unknown = 0xFF808080u;
    return p;
  }

  /// Selects a preset by its config index (0 default, 1 colour-blind, 2 high-contrast).
  static MismatchPalette preset(int index);
};

/// Compares a placement's schematic against the live world.
///
/// This is the feature that makes the mod useful: it turns "here is where the blocks go" into "here
/// is what you still have to place". The comparison is intentionally strict about state — a block
/// of the right type with the wrong state is `Wrong`, not `Correct` — and it reports `Extra` for
/// world blocks sitting where the schematic wants air, so a build that was started and abandoned is
/// visible rather than silently accepted.
class Verifier {
public:
  /// Verifies every non-air (and, optionally, air) block of a placement.
  ///
  /// @param schematic the schematic
  /// @param placement where it sits
  /// @param source the world to compare against
  /// @param layerMin,layerMax inclusive local Y range to check (layer modes)
  /// @param reportExtras also check air cells for stray world blocks
  static VerifyResult verify(const core::Schematic &schematic, const Placement &placement,
                             WorldSource &source, int layerMin, int layerMax,
                             bool reportExtras = false);

  /// The display colour for a status, as 0xAARRGGBB.
  static uint32_t colorFor(BlockStatus status);
};

} // namespace bitematica::world
