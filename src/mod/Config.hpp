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

/// The layer filter (item 10).
enum class LayerMode : int {
  All = 0,       // every layer
  Single = 1,    // one layer
  Range = 2,     // a contiguous range
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
  /// Triangle budget for the section mesher; meshing stops and reports truncation past this.
  int maxTriangles = 1500000;
  /// Distance beyond which sections are not drawn (0 disables distance culling).
  int renderDistance = 256;

  /// Layer filter (item 10): all layers, a single layer, or a range.
  LayerMode layerMode = LayerMode::All;
  int layerSingle = 0;
  int layerMin = 0;
  int layerMax = 255;

  /// Verification (item 9): colour missing/wrong/extra blocks instead of the schematic's own colour.
  bool verifyAgainstWorld = false;
  /// Whether to also scan air cells for stray world blocks (the "extra" status).
  bool verifyExtras = false;
  /// The `BlockSource::getBlock` primary-vtable slot for the running build, or -1 to leave
  /// verification off. There is no reliable way to discover this at runtime and a wrong slot faults
  /// the game thread, so it is a per-build constant a user sets once they have confirmed it. The
  /// default keeps the safe path: verification reports "world not readable" rather than guessing.
  int verifyBlockSourceSlot = -1;

  /// Toolbar anchor in HUD pixels (top-left of the toolbar's panel).
  int toolbarX = 24;
  int toolbarY = 120;
  /// Material list anchor in HUD pixels.
  int materialX = 24;
  int materialY = 24;

  /// Render distance beyond which the overlay is not drawn, in blocks.
  int maxRenderDistance = 0; // 0 = unlimited

  std::string lastLoadedFile;

  /// The inclusive local Y range the mesher and verifier should use for the current layer mode.
  void resolvedLayers(int sizeY, int &outMin, int &outMax) const {
    switch (layerMode) {
      case LayerMode::Single:
        outMin = layerSingle;
        outMax = layerSingle;
        break;
      case LayerMode::Range:
        outMin = layerMin;
        outMax = layerMax;
        break;
      case LayerMode::All:
      default:
        outMin = 0;
        outMax = sizeY > 0 ? sizeY - 1 : 0;
        break;
    }
    if (outMin < 0) outMin = 0;
    if (sizeY > 0 && outMax > sizeY - 1) outMax = sizeY - 1;
    if (outMax < outMin) outMax = outMin;
  }

  /// Clamp all values that a hand-edited config could push out of range.
  void normalize() {
    if (opacity < 10) opacity = 10;
    if (opacity > 100) opacity = 100;
    if (maxRenderBlocks < 500) maxRenderBlocks = 500;
    if (maxRenderBlocks > 250000) maxRenderBlocks = 250000;
    if (maxTriangles < 1000) maxTriangles = 1000;
    if (maxTriangles > 4000000) maxTriangles = 4000000;
    if (renderDistance < 0) renderDistance = 0;
    if (renderDistance > 1024) renderDistance = 1024;
    if (layerSingle < 0) layerSingle = 0;
    if (layerMin < 0) layerMin = 0;
    if (layerMax < layerMin) layerMax = layerMin;
    // A slot of 0 or below means "unverified"; anything above the vtable's plausible size is
    // nonsense and is treated the same way.
    if (verifyBlockSourceSlot < 0 || verifyBlockSourceSlot > 512) verifyBlockSourceSlot = -1;
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
