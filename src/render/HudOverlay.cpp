#include "render/HudOverlay.hpp"

#include <algorithm>
#include <cstdio>

namespace bitematica::render {
namespace {

constexpr uint32_t kToolbarBg = 0xD014181CU;
constexpr uint32_t kButtonBg = 0xE62A3038U;
constexpr uint32_t kButtonActive = 0xFF4AE0A0U;
constexpr uint32_t kButtonBorder = 0x80FFFFFFU;
constexpr uint32_t kTextColor = 0xFFF2F4F7U;
constexpr uint32_t kTextMuted = 0xB0C4C9D4U;
constexpr uint32_t kPanelBg = 0xC0111418U;
constexpr uint32_t kAccent = 0xFF7CF0C0U;

using Action = ToolbarButton::Action;

struct ButtonSpec {
  Action action;
  const char *label;
};

/// The toolbar buttons, in grid order. Two rows of six keeps the whole bar inside a
/// landscape phone viewport even at the default 40 px button size.
const ButtonSpec kButtons[] = {
    {Action::PreviousFile, "<"}, {Action::NextFile, ">"},
    {Action::CycleMode, "M"},    {Action::ToggleMaterials, "L"},
    {Action::RotateLeft, "Q"},   {Action::RotateRight, "E"},
    {Action::ZoomIn, "+"},       {Action::ZoomOut, "-"},
    {Action::NudgeUp, "^"},      {Action::NudgeDown, "v"},
    {Action::ToggleGhost, "G"},  {Action::ExportStructure, "S"},
    // World-space controls.
    {Action::Verify, "V"},        {Action::CycleLayers, "Y"},
    {Action::MoveToPlayer, "P"},  {Action::RotatePlacement, "R"},
    {Action::CycleMirror, "X"},   {Action::ExportCsv, "C"},
    {Action::SaveLitematic, "W"},
    // Litematica toolset.
    {Action::CycleToolMode, "T"}, {Action::ApplyTool, "A"},
    {Action::OpenMenu, "N"},      {Action::CycleMenuTab, "Tab"},
};

constexpr int kColumns = 6;

std::string formatCount(long long value) {
  // Group thousands so a large material count stays readable at a glance.
  std::string digits = std::to_string(value);
  std::string out;
  int counter = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (counter > 0 && counter % 3 == 0) out.push_back(',');
    out.push_back(*it);
    ++counter;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

} // namespace

std::vector<ToolbarButton> HudOverlay::layoutToolbar(const mod::Config &config) {
  std::vector<ToolbarButton> buttons;
  const int count = static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]));
  for (int i = 0; i < count; ++i) {
    const int row = i / kColumns;
    const int column = i % kColumns;
    ToolbarButton button;
    button.action = kButtons[i].action;
    button.label = kButtons[i].label;
    button.x = static_cast<float>(config.toolbarX) + column * (kButtonSize + kButtonGap);
    button.y = static_cast<float>(config.toolbarY) + row * (kButtonSize + kButtonGap);
    button.w = kButtonSize;
    button.h = kButtonSize;
    switch (button.action) {
      case Action::ToggleMaterials:
        button.active = config.showMaterialList;
        break;
      case Action::ToggleGhost:
        button.active = config.ghostPlacement;
        break;
      case Action::CycleMode:
        button.active = config.renderMode != mod::RenderMode::Hidden;
        break;
      case Action::Verify:
        button.active = config.verifyAgainstWorld;
        break;
      case Action::CycleLayers:
        button.active = config.layerMode != mod::LayerMode::All;
        break;
      default:
        break;
    }
    buttons.push_back(std::move(button));
  }
  return buttons;
}

const ToolbarButton *HudOverlay::hitTest(const std::vector<ToolbarButton> &buttons,
                                         float x, float y) {
  for (const ToolbarButton &button : buttons) {
    if (x >= button.x && x <= button.x + button.w && y >= button.y &&
        y <= button.y + button.h) {
      return &button;
    }
  }
  return nullptr;
}

uint32_t HudOverlay::applyOpacity(uint32_t argb, int opacityPercent) {
  const int alpha = static_cast<int>((argb >> 24) & 0xff);
  const int scaled = std::clamp(alpha * opacityPercent / 100, 0, 255);
  return (argb & 0x00FFFFFFu) | (static_cast<uint32_t>(scaled) << 24);
}

std::vector<UiCommand> HudOverlay::buildCommands(
    const mod::Config &config, const std::vector<ToolbarButton> &buttons,
    const std::vector<core::MaterialEntry> &materials, const std::string &statusLine,
    bool showMaterials) {
  // Chrome only: toolbar, status line, material panel. Schematic geometry is deliberately absent —
  // it is world-space GL geometry, drawn by SchematicRenderer, never HUD draw commands.
  std::vector<UiCommand> commands;

  if (config.showToolbar) {
    // Panel behind the grid.
    float minX = buttons.empty() ? 0.0f : buttons.front().x;
    float minY = buttons.empty() ? 0.0f : buttons.front().y;
    float maxX = minX;
    float maxY = minY;
    for (const ToolbarButton &button : buttons) {
      minX = std::min(minX, button.x);
      minY = std::min(minY, button.y);
      maxX = std::max(maxX, button.x + button.w);
      maxY = std::max(maxY, button.y + button.h);
    }
    commands.push_back(UiCommand::makeRect(minX - kPadding, minY - kPadding,
                                       (maxX - minX) + 2 * kPadding,
                                       (maxY - minY) + 2 * kPadding, kToolbarBg));

    for (const ToolbarButton &button : buttons) {
      const uint32_t bg = button.active ? kButtonActive : kButtonBg;
      commands.push_back(UiCommand::makeRect(button.x, button.y, button.w, button.h, bg));
      commands.push_back(UiCommand::makeRect(button.x, button.y, button.w, 2.0f, kButtonBorder));
      // Label, centered by eye: half a character height up and across.
      const float labelX = button.x + button.w * 0.5f - 6.0f;
      const float labelY = button.y + button.h * 0.5f - 7.0f;
      commands.push_back(UiCommand::makeText(labelX, labelY, button.label,
                                         button.active ? 0xFF04120CU : kTextColor, 16.0f));
    }

    if (!statusLine.empty()) {
      commands.push_back(UiCommand::makeText(minX, maxY + kPadding + 4.0f, statusLine,
                                         kTextMuted, 12.0f));
    }
  }

  if (config.showMaterialList && showMaterials) {
    const int shown = std::min<int>(static_cast<int>(materials.size()), 16);
    const float panelWidth = 220.0f;
    const float rowHeight = 16.0f;
    const float panelHeight = (shown + 1) * rowHeight + 2 * kPadding;
    commands.push_back(UiCommand::makeRect(static_cast<float>(config.materialX),
                                       static_cast<float>(config.materialY), panelWidth,
                                       panelHeight, kPanelBg));

    float y = static_cast<float>(config.materialY) + kPadding;
    commands.push_back(UiCommand::makeText(static_cast<float>(config.materialX) + kPadding, y,
                                       "Materials", kAccent, 14.0f));
    y += rowHeight;
    for (int i = 0; i < shown; ++i) {
      const core::MaterialEntry &entry = materials[static_cast<size_t>(i)];
      std::string label = entry.name;
      const size_t colon = label.find(':');
      if (colon != std::string::npos) label = label.substr(colon + 1);
      std::string line = label;
      line += "  x" + formatCount(static_cast<long long>(entry.count));
      commands.push_back(UiCommand::makeText(static_cast<float>(config.materialX) + kPadding,
                                         y, line, kTextColor, 12.0f));
      y += rowHeight;
    }
    if (static_cast<int>(materials.size()) > shown) {
      commands.push_back(UiCommand::makeText(
          static_cast<float>(config.materialX) + kPadding, y,
          "+" + std::to_string(materials.size() - static_cast<size_t>(shown)) + " more",
          kTextMuted, 12.0f));
    }
  }

  return commands;
}

} // namespace bitematica::render
