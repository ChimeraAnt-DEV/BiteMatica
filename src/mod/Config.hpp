#pragma once

#include <cstdint>
#include <string>

namespace bitematica::mod {

/// How the schematic is drawn.
enum class RenderMode : int {
  Overlay = 0,   // translucent blocks over the world
  Skeleton = 1,  // only the bounding box wireframe
  Hidden = 2,    // nothing drawn
};

/// User settings for BiteMatica, persisted as `config/config.json`.
///
/// Field order is the JSON order (the config layer reflects aggregate members), so it
/// is grouped as: master switches, view, colors, then the material-panel placement.
/// Defaults are chosen so that a fresh install draws a readable overlay with the
/// toolbar already on screen and does not need the Mod Menu opened first.
struct Config {
  int version = 1;

  /// Master switch mirrored by the Mod Menu module toggle.
  bool enabled = true;

  RenderMode renderMode = RenderMode::Overlay;
  /// Overlay opacity in percent (10..100).
  int opacity = 60;
  /// Draw the material list panel.
  bool showMaterialList = true;
  /// Draw the toolbar (mode, info, placement nudge buttons).
  bool showToolbar = true;
  /// Ghost placement: outline where the schematic would sit at the player's crosshair.
  bool ghostPlacement = false;
  /// Chunk-level of detail: skip interior blocks (only exposed faces are drawn anyway).
  bool onlyExposed = true;
  /// Maximum blocks meshed per frame; guards very large schematics.
  int maxRenderBlocks = 60000;

  /// Toolbar anchor in HUD pixels (top-left of the toolbar's panel).
  int toolbarX = 24;
  int toolbarY = 120;
  /// Material list anchor in HUD pixels.
  int materialX = 24;
  int materialY = 24;

  /// Render distance beyond which the overlay is not drawn, in blocks.
  int maxRenderDistance = 0; // 0 = unlimited

  std::string lastLoadedFile;

  /// Clamp all values that a hand-edited config could push out of range.
  void normalize() {
    if (opacity < 10) opacity = 10;
    if (opacity > 100) opacity = 100;
    if (maxRenderBlocks < 500) maxRenderBlocks = 500;
    if (maxRenderBlocks > 250000) maxRenderBlocks = 250000;
    if (toolbarX < 0) toolbarX = 0;
    if (toolbarY < 0) toolbarY = 0;
    if (materialX < 0) materialX = 0;
    if (materialY < 0) materialY = 0;
    if (static_cast<int>(renderMode) < 0 || static_cast<int>(renderMode) > 2) {
      renderMode = RenderMode::Overlay;
    }
  }
};

} // namespace bitematica::mod
