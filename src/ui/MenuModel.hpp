#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bitematica::ui {

/// The Litematica menu screens, as tabs in the in-game menu.
enum class MenuTab : int {
  Main = 0,       ///< tool mode, quick toggles, status
  Config,         ///< generic / visuals / colors / hotkeys / info switches
  Materials,      ///< live material list
  Placements,     ///< rotation, mirroring, sub-regions, locking
  Render,         ///< render layers and render mode
  AreaEditor,     ///< selection corners, save/load
  Loaded,         ///< loaded schematics, unload/reload
  Tasks,          ///< background job queue
  Count,
};

inline constexpr int kMenuTabCount = static_cast<int>(MenuTab::Count);

inline const char *menuTabName(MenuTab tab) {
  switch (tab) {
    case MenuTab::Main: return "Main";
    case MenuTab::Config: return "Config";
    case MenuTab::Materials: return "Materials";
    case MenuTab::Placements: return "Placements";
    case MenuTab::Render: return "Render";
    case MenuTab::AreaEditor: return "Area Editor";
    case MenuTab::Loaded: return "Loaded";
    case MenuTab::Tasks: return "Tasks";
    case MenuTab::Count: break;
  }
  return "";
}

/// A widget kind, mapped to a draw + input behaviour by the renderer.
enum class WidgetKind : int {
  Header,     ///< a section title
  Toggle,     ///< on/off
  Slider,     ///< integer value with a range
  Choice,     ///< one of N labels
  Button,     ///< fires an action
  Text,       ///< read-only line
  List,       ///< a scrollable list of rows
};

/// One interactive row in a menu.
///
/// The menu is data, not code: the same structure is drawn, laid out and hit-tested by the HUD, so
/// a new setting is one entry here rather than a new draw path. This is what makes the config menu
/// (Litematica has hundreds of settings) tractable.
struct Widget {
  WidgetKind kind{WidgetKind::Text};
  std::string id;
  std::string label;
  std::string value;      ///< current value, as text
  std::string description;
  int minValue{};
  int maxValue{};
  std::vector<std::string> choices;
  bool enabled{true};
  /// Invoked with the widget id and the new value text. Set by the mod, not the model.
  std::function<void(const std::string &, const std::string &)> onChange;
  std::function<void()> onActivate;
};

/// One menu tab: a titled, ordered list of widgets.
struct MenuPage {
  MenuTab tab{MenuTab::Main};
  std::string title;
  std::vector<Widget> widgets;
};

/// Builds and holds the menu pages.
///
/// Kept separate from the mod so the layout (which widgets a page holds, in what order, with what
/// ranges) is host-testable: a slider whose min exceeds its max, or a choice with no options, is a
/// bug that should be caught without a device.
class MenuModel {
public:
  /// Replaces the pages.
  void setPages(std::vector<MenuPage> pages) { mPages = std::move(pages); }

  const std::vector<MenuPage> &pages() const { return mPages; }
  int pageCount() const { return static_cast<int>(mPages.size()); }

  MenuPage *page(MenuTab tab) {
    for (MenuPage &page : mPages) {
      if (page.tab == tab) return &page;
    }
    return nullptr;
  }

  /// The index of a tab in the page list, or -1.
  int indexOf(MenuTab tab) const {
    for (int i = 0; i < static_cast<int>(mPages.size()); ++i) {
      if (mPages[static_cast<size_t>(i)].tab == tab) return i;
    }
    return -1;
  }

  /// Finds a widget by id across every page.
  Widget *find(const std::string &id) {
    for (MenuPage &page : mPages) {
      for (Widget &widget : page.widgets) {
        if (widget.id == id) return &widget;
      }
    }
    return nullptr;
  }

  /// The number of selectable (non-header, non-text) widgets on a page.
  static int selectableCount(const MenuPage &page) {
    int count = 0;
    for (const Widget &widget : page.widgets) {
      if (widget.kind != WidgetKind::Header && widget.kind != WidgetKind::Text) ++count;
    }
    return count;
  }

  /// True when every widget's range and choices are coherent. Used by the tests.
  bool validate() const {
    for (const MenuPage &page : mPages) {
      for (const Widget &widget : page.widgets) {
        if (widget.id.empty()) return false;
        if (widget.kind == WidgetKind::Slider && widget.minValue > widget.maxValue) return false;
        if (widget.kind == WidgetKind::Choice && widget.choices.empty()) return false;
      }
    }
    return true;
  }

private:
  std::vector<MenuPage> mPages;
};

} // namespace bitematica::ui
