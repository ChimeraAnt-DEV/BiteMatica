#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/MaterialList.hpp"
#include "core/Schematic.hpp"
#include "mod/Config.hpp"

namespace bitematica::render {

/// A neutral draw command, decoupled from the preloader's `pl::modmenu::DrawCommand`.
///
/// This vocabulary is **UI chrome only** — the toolbar, the material panel and the status
/// line. Schematic geometry is never drawn through it: that would be a screen-space
/// "software overlay", which is not world-space rendering and is not this mod. World-space
/// geometry goes through GL in `gles/SchematicRenderer`. Keeping the chrome model in its
/// own vocabulary is what lets the layout — toolbar hit boxes, panel wrapping — be
/// unit-tested on a host build with no preloader present.
struct UiCommand {
  enum class Kind { Rect, Text };
  Kind kind{Kind::Rect};
  float x{};
  float y{};
  float w{};
  float h{};
  uint32_t color{};
  float size{};
  std::string text;

  static UiCommand makeRect(float x, float y, float w, float h, uint32_t color) {
    UiCommand c;
    c.kind = Kind::Rect;
    c.x = x; c.y = y; c.w = w; c.h = h; c.color = color;
    return c;
  }
  static UiCommand makeText(float x, float y, const std::string &value, uint32_t color,
                            float size = 14.0f) {
    UiCommand c;
    c.kind = Kind::Text;
    c.x = x; c.y = y; c.color = color; c.text = value; c.size = size;
    return c;
  }
};

/// A single toolbar control and its screen hit box.
struct ToolbarButton {
  enum class Action {
    PreviousFile,
    NextFile,
    CycleMode,
    RotateLeft,
    RotateRight,
    ZoomIn,
    ZoomOut,
    NudgeUp,
    NudgeDown,
    ToggleGhost,
    ExportStructure,
    ToggleMaterials,
    Reload,
    // World-space additions.
    Verify,        // compare against the live world
    CycleLayers,   // all / single / range
    MoveToPlayer,  // snap the placement origin to the player
    RotatePlacement, // rotate the placement 90 degrees
    CycleMirror,   // mirror the placement
    ExportCsv,     // export the live material list as CSV
    SaveLitematic, // save the loaded schematic (or a selection) as .litematic
    // Litematica toolset.
    CycleToolMode,  // cycle the nine stick-tool modes
    ApplyTool,      // act with the current tool mode
    OpenMenu,       // open the in-game menu
    CycleMenuTab,   // switch menu tab
  };
  Action action{Action::CycleMode};
  std::string label;
  float x{};
  float y{};
  float w{};
  float h{};
  bool active{};
  bool highlighted{};
};

/// The toolbar + material panel model and its draw-command output.
///
/// One class owns both the widgets' geometry and the commands that render them, so a
/// tap and the button it hits can never disagree: `layoutToolbar` is the single source
/// of truth for where each button is, and `hitTest` walks exactly that list.
///
/// This class draws **chrome only**. There is no schematic-mesh path here by design: a
/// schematic drawn as 2D HUD geometry is a screen-space projection, not the world-space
/// ghost this mod exists to provide.
class HudOverlay {
public:
  static constexpr float kButtonSize = 40.0f;
  static constexpr float kButtonGap = 6.0f;
  static constexpr float kPadding = 8.0f;

  /// Lays out the toolbar from the config anchor, in a 2-row grid.
  static std::vector<ToolbarButton> layoutToolbar(const mod::Config &config);

  /// Returns the action under a screen point, or nullptr.
  static const ToolbarButton *hitTest(const std::vector<ToolbarButton> &buttons,
                                      float x, float y);

  /// Builds every chrome command for one frame: toolbar, material panel, status line.
  ///
  /// @param materials the rows to show in the material panel
  /// @param showMaterials whether the material panel is drawn at all (the panel is only
  ///        meaningful once a schematic is loaded and its counts are known)
  static std::vector<UiCommand> buildCommands(const mod::Config &config,
                                              const std::vector<ToolbarButton> &buttons,
                                              const std::vector<core::MaterialEntry> &materials,
                                              const std::string &statusLine, bool showMaterials);

  static uint32_t applyOpacity(uint32_t argb, int opacityPercent);
};

} // namespace bitematica::render
