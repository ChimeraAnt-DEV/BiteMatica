#include "mod/ControllerNavigator.hpp"

#include <algorithm>

namespace bitematica::mod {

const char *ControllerNavigator::actionName(render::ToolbarButton::Action action) {
  using Action = render::ToolbarButton::Action;
  switch (action) {
    case Action::PreviousFile: return "Previous file";
    case Action::NextFile: return "Next file";
    case Action::CycleMode: return "Render mode";
    case Action::RotateLeft: return "Rotate left";
    case Action::RotateRight: return "Rotate right";
    case Action::ZoomIn: return "Zoom in";
    case Action::ZoomOut: return "Zoom out";
    case Action::NudgeUp: return "Move up";
    case Action::NudgeDown: return "Move down";
    case Action::ToggleGhost: return "Ghost placement";
    case Action::ExportStructure: return "Export .mcstructure";
    case Action::ToggleMaterials: return "Material list";
    case Action::Reload: return "Reload";
    case Action::Verify: return "Verify against world";
    case Action::CycleLayers: return "Layer mode";
    case Action::MoveToPlayer: return "Move to player";
    case Action::RotatePlacement: return "Rotate placement";
    case Action::CycleMirror: return "Mirror placement";
    case Action::ExportCsv: return "Export material CSV";
    case Action::SaveLitematic: return "Save .litematic";
    case Action::CycleToolMode: return "Cycle tool mode";
    case Action::ApplyTool: return "Use tool";
    case Action::OpenMenu: return "Open menu";
    case Action::CycleMenuTab: return "Next menu tab";
    case Action::PanicToggle: return "Hide/show rendering";
  }
  return "Action";
}

ControllerNavigator::Result ControllerNavigator::handleKey(int keyCode, bool isKeyDown,
                                                           int count) {
  Result result;
  if (!isKeyDown || count <= 0) return result;

  const int index = resolvedFocus(count);
  const int row = index / kColumns;
  const int column = index % kColumns;

  switch (keyCode) {
    case DpadUp:
    case DpadDown:
    case DpadLeft:
    case DpadRight: {
      result.handled = true;
      result.activated = false;
      mEngaged = true;
      int newRow = row;
      int newColumn = column;
      if (keyCode == DpadUp) newRow = std::max(0, row - 1);
      if (keyCode == DpadDown) newRow = std::min(kRowCount - 1, row + 1);
      if (keyCode == DpadLeft) newColumn = std::max(0, column - 1);
      if (keyCode == DpadRight) newColumn = std::min(kColumns - 1, column + 1);
      int candidate = newRow * kColumns + newColumn;
      if (candidate >= count) candidate = count - 1;
      mFocusIndex = candidate;
      return result;
    }
    case ButtonL1:
    case ButtonR1:
      // Shoulder buttons are a shortcut straight to the rotate action, so a
      // controller user can turn the build without stepping through the grid.
      result.handled = true;
      result.activated = true;
      mEngaged = true;
      result.action = keyCode == ButtonL1 ? render::ToolbarButton::Action::RotateLeft
                                          : render::ToolbarButton::Action::RotateRight;
      result.label = actionName(result.action);
      return result;
    case ButtonA:
    case DpadCenter:
      result.handled = true;
      result.activated = true;
      mEngaged = true;
      result.action = actionForIndex(index);
      result.label = actionName(result.action);
      return result;
    case ButtonB:
      result.handled = true;
      mEngaged = false;
      return result;
    default:
      return result;
  }
}

render::ToolbarButton::Action ControllerNavigator::actionForIndex(int index) {
  using Action = render::ToolbarButton::Action;
  // Must match HudOverlay's grid order exactly.
  static const Action order[] = {
      Action::PreviousFile, Action::NextFile,    Action::CycleMode,
      Action::ToggleMaterials, Action::RotateLeft, Action::RotateRight,
      Action::ZoomIn,       Action::ZoomOut,     Action::NudgeUp,
      Action::NudgeDown,    Action::ToggleGhost, Action::ExportStructure,
      Action::Verify,       Action::CycleLayers, Action::MoveToPlayer,
      Action::RotatePlacement, Action::CycleMirror, Action::ExportCsv,
      Action::SaveLitematic, Action::CycleToolMode, Action::ApplyTool,
      Action::OpenMenu, Action::CycleMenuTab, Action::PanicToggle,
  };
  const int count = static_cast<int>(sizeof(order) / sizeof(order[0]));
  if (index < 0) index = 0;
  if (index >= count) index = count - 1;
  return order[index];
}

} // namespace bitematica::mod
