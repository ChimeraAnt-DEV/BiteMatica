#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/MaterialList.hpp"
#include "core/Schematic.hpp"
#include "mod/Config.hpp"
#include "render/MeshBuilder.hpp"
#include "render/ViewCamera.hpp"

namespace bitematica::render {

/// A neutral draw command, decoupled from the preloader's `pl::modmenu::DrawCommand`.
///
/// Keeping the UI model in its own vocabulary is what lets the whole overlay layout —
/// toolbar hit boxes, panel wrapping, mesh ordering — be unit-tested on a host build
/// with no preloader present. The mod glue translates these one-for-one at submit time.
struct UiCommand {
  enum class Kind { Rect, Triangle, Text };
  Kind kind{Kind::Rect};
  float x{};
  float y{};
  float w{};
  float h{};
  float x3{};
  float y3{};
  uint32_t color{};
  float size{};
  std::string text;

  static UiCommand makeRect(float x, float y, float w, float h, uint32_t color) {
    UiCommand c;
    c.kind = Kind::Rect;
    c.x = x; c.y = y; c.w = w; c.h = h; c.color = color;
    return c;
  }
  static UiCommand makeTriangle(const ScreenPoint &a, const ScreenPoint &b,
                                const ScreenPoint &c3, uint32_t color,
                                float size = 0.0f) {
    UiCommand c;
    c.kind = Kind::Triangle;
    c.x = a.x; c.y = a.y;
    c.w = b.x; c.h = b.y;
    c.x3 = c3.x; c.y3 = c3.y;
    c.color = color;
    c.size = size;
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

/// The schematic toolbar + material panel model and its draw-command output.
///
/// One class owns both the widgets' geometry and the commands that render them, so a
/// tap and the button it hits can never disagree: `layoutToolbar` is the single source
/// of truth for where each button is, and `hitTest` walks exactly that list.
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

  /// Builds every command for one frame: schematic mesh, toolbar, material panel.
  static std::vector<UiCommand> buildCommands(
      const core::Schematic &schematic, const mod::Config &config,
      const ViewCamera &camera, const std::vector<ToolbarButton> &buttons,
      const std::vector<core::MaterialEntry> &materials, const std::string &statusLine,
      bool hasSchematic);

  /// The mesh command list alone, for tests that do not want the chrome.
  static std::vector<UiCommand> buildMeshCommands(const core::Schematic &schematic,
                                                  const mod::Config &config,
                                                  const ViewCamera &camera);

  static uint32_t applyOpacity(uint32_t argb, int opacityPercent);
};

} // namespace bitematica::render
