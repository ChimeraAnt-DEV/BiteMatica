#pragma once

#include <string>
#include <vector>

#include "render/HudOverlay.hpp"
#include "ui/MenuModel.hpp"

namespace bitematica::ui {

/// A tappable region of the menu, produced by the layout pass.
///
/// The renderer emits these alongside the draw commands so a tap can be matched to a widget without
/// re-deriving the layout. Keeping them in one pass is what stops the drawn widget and the tappable
/// widget from drifting apart — the same contract the toolbar uses.
struct MenuHit {
  enum class Kind { Tab, Widget, Close };
  Kind kind{Kind::Widget};
  /// The tab index for a tab hit.
  int tab{};
  /// The widget index within the page for a widget hit.
  int widgetIndex{-1};
  float x{};
  float y{};
  float w{};
  float h{};
};

/// The result of laying a menu page out: draw commands plus the hitboxes for them.
struct MenuLayout {
  std::vector<render::UiCommand> commands;
  std::vector<MenuHit> hits;
  /// The full menu panel bounds, so a tap outside can be treated as "close".
  float panelX{};
  float panelY{};
  float panelW{};
  float panelH{};
};

/// Sizes and spacing for the menu, in HUD pixels. Scaled to be reachable with a thumb on a phone.
struct MenuMetrics {
  float panelX{40.0f};
  float panelY{40.0f};
  float panelWidth{760.0f};
  float rowHeight{44.0f};
  float padding{12.0f};
  float tabHeight{48.0f};
  float closeSize{44.0f};
  float fontSize{18.0f};
  float headerFontSize{20.0f};
  /// How many rows fit before the page scrolls. The caller scrolls by `scrollRow`.
  int visibleRows{14};
};

/// Lays a menu page out into draw commands and hitboxes.
///
/// The menu is drawn through the same HUD command channel as the toolbar, so it needs no GL of its
/// own and works identically whether the schematic is drawn in world space or as a software
/// overlay. Layout is pure arithmetic over the widget list, so it is host-testable: a widget that
/// would be drawn off the panel edge, or a hitbox that does not match its row, is caught in a test
/// rather than on a device.
class MenuRenderer {
public:
  /// Lays out one page.
  ///
  /// @param page the page to draw
  /// @param tabIndex the index of this page among all tabs, for highlighting
  /// @param tabCount how many tabs there are
  /// @param scrollRow the first widget row to show (for scrolling a long page)
  /// @param metrics sizing
  static MenuLayout layout(const MenuPage &page, int tabIndex, int tabCount, int scrollRow,
                           const MenuMetrics &metrics = MenuMetrics{});

  /// The always-on floating button that opens the menu, as a draw command plus its hitbox.
  ///
  /// This is the answer to "the menu needs a keyboard": the icon is drawn on screen and tapped, so
  /// no key is needed. It is deliberately separate from the toolbar so it is reachable even when the
  /// toolbar is hidden.
  struct IconLayout {
    std::vector<render::UiCommand> commands;
    MenuHit hit;
  };

  /// Lays out the floating menu icon at its anchor.
  static IconLayout layoutIcon(float x, float y, float size, bool menuOpen);

  /// The number of widget rows a page has (headers and text included), for scroll clamping.
  static int rowCount(const MenuPage &page);
};

} // namespace bitematica::ui
