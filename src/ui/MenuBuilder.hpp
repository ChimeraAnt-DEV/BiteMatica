#pragma once

#include <string>
#include <vector>

#include "litematica/LayerRange.hpp"
#include "litematica/PlacementManager.hpp"
#include "litematica/ToolMode.hpp"
#include "mod/Config.hpp"
#include "ui/MenuModel.hpp"

namespace bitematica::ui {

/// Builds the full Litematica-style menu from the mod's live state.
///
/// Litematica's menus are its interface: the Main menu, Config (hundreds of settings across
/// generic/visuals/colors/hotkeys/info), the Material List, Schematic Placements, the Render Layers
/// sub-menu, the Area Editor, Loaded Schematics and the Task Manager. This assembles those eight
/// pages as plain data from the config and placement state, so the whole menu is described in one
/// place and rendered generically.
///
/// The builders are pure: they read the config and placements and return widgets, so the menu's
/// shape (which page holds what, with what ranges) is host-testable.
class MenuBuilder {
public:
  /// The state the menu reflects.
  struct Context {
    const mod::Config *config{};
    const litematica::PlacementManager *placements{};
    litematica::ToolMode toolMode{litematica::ToolMode::AreaSelection};
    litematica::LayerRange layers{};
    /// Counts for the material page: required/placed/missing, already computed.
    std::vector<std::tuple<std::string, long long, long long, long long>> materials;
    /// Loaded schematic names for the Loaded page.
    std::vector<std::string> loadedNames;
    /// Task queue lines for the Tasks page.
    std::vector<std::string> tasks;
    std::string statusLine;
    /// Whether world-space rendering is active (reported on the Main page).
    bool worldSpace{false};
    /// Whether the world is readable (verification available).
    bool worldReadable{false};
  };

  /// Builds every page.
  static std::vector<MenuPage> buildAll(const Context &context);

  /// Individual pages, exposed for tests.
  static MenuPage buildMain(const Context &context);
  static MenuPage buildConfig(const Context &context);
  static MenuPage buildMaterials(const Context &context);
  static MenuPage buildPlacements(const Context &context);
  static MenuPage buildRender(const Context &context);
  static MenuPage buildAreaEditor(const Context &context);
  static MenuPage buildLoaded(const Context &context);
  static MenuPage buildTasks(const Context &context);
};

} // namespace bitematica::ui
