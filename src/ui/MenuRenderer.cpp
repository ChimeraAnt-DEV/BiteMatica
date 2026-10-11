#include "ui/MenuRenderer.hpp"

#include <algorithm>

namespace bitematica::ui {
namespace {

// A small, consistent palette so the menu reads as one surface.
constexpr uint32_t kPanelBg = 0xE0101418u;
constexpr uint32_t kTabBg = 0xFF1C222Au;
constexpr uint32_t kTabActive = 0xFF4AE0A0u;
constexpr uint32_t kRowBg = 0xFF232A33u;
constexpr uint32_t kHeaderText = 0xFF7CF0C0u;
constexpr uint32_t kText = 0xFFF2F4F7u;
constexpr uint32_t kMuted = 0xB0C4C9D4u;
constexpr uint32_t kOn = 0xFF3DDC84u;
constexpr uint32_t kOff = 0xFF6B7280u;
constexpr uint32_t kButton = 0xFF2F6FEDu;
constexpr uint32_t kClose = 0xFFB03030u;
constexpr uint32_t kIcon = 0xFF4AE0A0u;
constexpr uint32_t kIconOpen = 0xFFB03030u;

/// A short label for the current value of a widget, shown on the right of its row.
std::string valueLabel(const Widget &widget) {
  switch (widget.kind) {
    case WidgetKind::Toggle:
      return widget.value == "true" ? "ON" : "OFF";
    case WidgetKind::Choice: {
      // The value is the index into `choices`.
      int index = 0;
      for (char c : widget.value) {
        if (c >= '0' && c <= '9') index = index * 10 + (c - '0');
      }
      if (index >= 0 && index < static_cast<int>(widget.choices.size())) {
        return widget.choices[static_cast<size_t>(index)];
      }
      return widget.value;
    }
    case WidgetKind::Slider:
      return widget.value;
    case WidgetKind::Button:
      return "";
    default:
      return widget.value;
  }
}

} // namespace

int MenuRenderer::rowCount(const MenuPage &page) {
  return static_cast<int>(page.widgets.size());
}

MenuLayout MenuRenderer::layout(const MenuPage &page, int tabIndex, int tabCount, int scrollRow,
                                const MenuMetrics &metrics) {
  MenuLayout layout;

  // Panel.
  const float panelX = metrics.panelX;
  const float panelY = metrics.panelY;
  const float panelW = metrics.panelWidth;
  const float panelH = metrics.tabHeight + metrics.padding +
                       metrics.visibleRows * metrics.rowHeight + metrics.padding;
  layout.panelX = panelX;
  layout.panelY = panelY;
  layout.panelW = panelW;
  layout.panelH = panelH;
  layout.commands.push_back(
      render::UiCommand::makeRect(panelX, panelY, panelW, panelH, kPanelBg));

  // Tab strip: evenly divided across the panel width, minus room for the close button.
  const float stripW = panelW - metrics.closeSize - metrics.padding;
  const float tabW = tabCount > 0 ? stripW / static_cast<float>(tabCount) : stripW;
  for (int i = 0; i < tabCount; ++i) {
    const float x = panelX + static_cast<float>(i) * tabW;
    const bool active = i == tabIndex;
    layout.commands.push_back(
        render::UiCommand::makeRect(x + 2.0f, panelY + 2.0f, tabW - 4.0f,
                                    metrics.tabHeight - 4.0f, active ? kTabActive : kTabBg));
    layout.commands.push_back(render::UiCommand::makeText(
        x + 8.0f, panelY + metrics.tabHeight * 0.5f - 8.0f, menuTabName(static_cast<MenuTab>(i)),
        active ? 0xFF04120Cu : kText, metrics.fontSize * 0.85f));

    MenuHit hit;
    hit.kind = MenuHit::Kind::Tab;
    hit.tab = i;
    hit.x = x;
    hit.y = panelY;
    hit.w = tabW;
    hit.h = metrics.tabHeight;
    layout.hits.push_back(hit);
  }

  // Close button, top-right.
  const float closeX = panelX + panelW - metrics.closeSize;
  layout.commands.push_back(render::UiCommand::makeRect(
      closeX + 2.0f, panelY + 2.0f, metrics.closeSize - 4.0f, metrics.closeSize - 4.0f, kClose));
  layout.commands.push_back(render::UiCommand::makeText(
      closeX + metrics.closeSize * 0.35f, panelY + metrics.closeSize * 0.28f, "X", kText,
      metrics.fontSize));
  MenuHit closeHit;
  closeHit.kind = MenuHit::Kind::Close;
  closeHit.x = closeX;
  closeHit.y = panelY;
  closeHit.w = metrics.closeSize;
  closeHit.h = metrics.closeSize;
  layout.hits.push_back(closeHit);

  // Rows. `scrollRow` skips widgets off the top so a long page fits the visible window.
  const float rowsTop = panelY + metrics.tabHeight + metrics.padding;
  const int first = std::max(0, scrollRow);
  const int last = std::min(static_cast<int>(page.widgets.size()), first + metrics.visibleRows);
  for (int i = first; i < last; ++i) {
    const Widget &widget = page.widgets[static_cast<size_t>(i)];
    const float y = rowsTop + static_cast<float>(i - first) * metrics.rowHeight;
    const float rowX = panelX + metrics.padding;
    const float rowW = panelW - 2.0f * metrics.padding;

    if (widget.kind == WidgetKind::Header) {
      layout.commands.push_back(render::UiCommand::makeText(
          rowX, y + metrics.rowHeight * 0.5f - 9.0f, widget.label, kHeaderText,
          metrics.headerFontSize));
      continue;
    }

    if (widget.kind == WidgetKind::Text) {
      layout.commands.push_back(render::UiCommand::makeText(
          rowX, y + metrics.rowHeight * 0.5f - 8.0f, widget.label, kMuted, metrics.fontSize));
      layout.commands.push_back(render::UiCommand::makeText(
          rowX + rowW * 0.55f, y + metrics.rowHeight * 0.5f - 8.0f, widget.value, kText,
          metrics.fontSize));
      continue;
    }

    // Interactive rows get a background, a label and a value/action on the right, and a hitbox.
    const uint32_t bg = widget.kind == WidgetKind::Button ? kButton : kRowBg;
    layout.commands.push_back(render::UiCommand::makeRect(rowX, y + 2.0f, rowW,
                                                          metrics.rowHeight - 4.0f, bg));
    layout.commands.push_back(render::UiCommand::makeText(
        rowX + 10.0f, y + metrics.rowHeight * 0.5f - 8.0f, widget.label, kText,
        metrics.fontSize));

    if (widget.kind == WidgetKind::Toggle) {
      const bool on = widget.value == "true";
      layout.commands.push_back(render::UiCommand::makeRect(
          rowX + rowW - 70.0f, y + 8.0f, 60.0f, metrics.rowHeight - 16.0f, on ? kOn : kOff));
      layout.commands.push_back(render::UiCommand::makeText(
          rowX + rowW - 46.0f, y + metrics.rowHeight * 0.5f - 8.0f, on ? "ON" : "OFF", kText,
          metrics.fontSize * 0.85f));
    } else {
      const std::string value = valueLabel(widget);
      if (!value.empty()) {
        layout.commands.push_back(render::UiCommand::makeText(
            rowX + rowW * 0.55f, y + metrics.rowHeight * 0.5f - 8.0f, value, kText,
            metrics.fontSize));
      }
    }

    MenuHit hit;
    hit.kind = MenuHit::Kind::Widget;
    hit.tab = tabIndex;
    hit.widgetIndex = i;
    hit.x = rowX;
    hit.y = y;
    hit.w = rowW;
    hit.h = metrics.rowHeight;
    layout.hits.push_back(hit);
  }

  // A scroll indicator when the page continues below.
  if (last < static_cast<int>(page.widgets.size())) {
    layout.commands.push_back(render::UiCommand::makeText(
        panelX + panelW - 90.0f, panelY + panelH - 20.0f,
        "+" + std::to_string(page.widgets.size() - static_cast<size_t>(last)) + " more", kMuted,
        metrics.fontSize * 0.8f));
  }
  return layout;
}

MenuRenderer::IconLayout MenuRenderer::layoutIcon(float x, float y, float size, bool menuOpen) {
  IconLayout icon;

  // A rounded-looking tile with a grid glyph, so it reads as "menu" at a glance.
  const uint32_t bg = menuOpen ? kIconOpen : kIcon;
  icon.commands.push_back(render::UiCommand::makeRect(x, y, size, size, 0xD0101418u));
  icon.commands.push_back(render::UiCommand::makeRect(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f,
                                                      bg));

  // A 2x2 grid glyph in the middle: four small squares, the universal menu mark.
  const float pad = size * 0.28f;
  const float cell = (size - 2.0f * pad) * 0.5f - 1.0f;
  for (int row = 0; row < 2; ++row) {
    for (int col = 0; col < 2; ++col) {
      icon.commands.push_back(render::UiCommand::makeRect(
          x + pad + static_cast<float>(col) * (cell + 2.0f),
          y + pad + static_cast<float>(row) * (cell + 2.0f), cell, cell, 0xFF04120Cu));
    }
  }

  icon.hit.kind = MenuHit::Kind::Widget;
  icon.hit.widgetIndex = -1;
  icon.hit.x = x;
  icon.hit.y = y;
  icon.hit.w = size;
  icon.hit.h = size;
  return icon;
}

} // namespace bitematica::ui
