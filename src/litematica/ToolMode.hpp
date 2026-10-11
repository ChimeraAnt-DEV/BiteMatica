#pragma once

#include <string>

namespace bitematica::litematica {

/// The nine stick-tool modes, mirroring Litematica's tool set.
///
/// In Litematica these are cycled with Ctrl + scroll while holding the tool item. On Android the
/// same set is driven by the toolbar and the controller's shoulder buttons. The enum order is the
/// cycle order and is part of the contract (a host test pins the wraparound).
enum class ToolMode : int {
  AreaSelection = 0,   ///< set corner 1 / corner 2, expand along the view ray
  SchematicPlacement,  ///< place a schematic's origin at the targeted block
  Fill,                ///< fill / replace inside a selection (creative)
  Delete,              ///< clear a selection (creative)
  Move,                ///< move blocks from A to B
  Paste,               ///< paste the schematic into the world
  Grid,                ///< create a repeating grid / array
  Rebuild,             ///< verify world against the schematic, highlight mismatches
  PickBlock,           ///< pull the targeted schematic block into the hotbar
  Count,
};

/// The nine modes in cycle order.
inline constexpr int kToolModeCount = static_cast<int>(ToolMode::Count);

/// The next mode in the cycle.
inline ToolMode nextToolMode(ToolMode mode) {
  return static_cast<ToolMode>((static_cast<int>(mode) + 1) % kToolModeCount);
}

/// The previous mode in the cycle.
inline ToolMode previousToolMode(ToolMode mode) {
  return static_cast<ToolMode>((static_cast<int>(mode) + kToolModeCount - 1) % kToolModeCount);
}

/// The display name of a mode.
inline const char *toolModeName(ToolMode mode) {
  switch (mode) {
    case ToolMode::AreaSelection: return "Area Selection";
    case ToolMode::SchematicPlacement: return "Schematic Placement";
    case ToolMode::Fill: return "Fill / Replace";
    case ToolMode::Delete: return "Delete";
    case ToolMode::Move: return "Move";
    case ToolMode::Paste: return "Paste to World";
    case ToolMode::Grid: return "Grid";
    case ToolMode::Rebuild: return "Rebuild / Verify";
    case ToolMode::PickBlock: return "Pick Block";
    case ToolMode::Count: break;
  }
  return "Unknown";
}

/// A one-line hint for the status bar, so the active mode explains itself.
inline const char *toolModeHint(ToolMode mode) {
  switch (mode) {
    case ToolMode::AreaSelection: return "tap: corner 1 / corner 2";
    case ToolMode::SchematicPlacement: return "tap a block to move the origin there";
    case ToolMode::Fill: return "fill the selection with the held block";
    case ToolMode::Delete: return "clear the selection";
    case ToolMode::Move: return "move the selection to the target";
    case ToolMode::Paste: return "paste the schematic into the world";
    case ToolMode::Grid: return "repeat the selection on a grid";
    case ToolMode::Rebuild: return "verify the world against the schematic";
    case ToolMode::PickBlock: return "pick the targeted block";
    case ToolMode::Count: break;
  }
  return "";
}

} // namespace bitematica::litematica
