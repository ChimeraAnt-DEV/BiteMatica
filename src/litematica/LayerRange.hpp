#pragma once

#include <algorithm>

namespace bitematica::litematica {

/// Which layers of a schematic are drawn, mirroring Litematica's render-layer sub-menu.
enum class LayerMode : int {
  All = 0,      ///< the whole schematic
  Single = 1,   ///< one horizontal layer
  AllBelow = 2, ///< everything at or below a Y level
  AllAbove = 3, ///< everything at or above a Y level
  Range = 4,    ///< a contiguous Y range
  Count,
};

/// The layer filter state and the range it resolves to.
///
/// Litematica's layer stepping is the feature that makes a multi-storey build tractable: you show
/// one layer, build it, step up, repeat. The resolution is a pure function of the mode and the
/// schematic height, so it is host-testable — and the clamping matters, because a layer index past
/// the top must not silently render nothing.
struct LayerRange {
  LayerMode mode{LayerMode::All};
  /// The active layer for Single, the bound for AllBelow/AllAbove, the low end for Range.
  int layer{};
  /// The high end for Range.
  int layerMax{};

  /// Resolves to an inclusive [min, max] Y range for a schematic of `height` layers.
  void resolve(int height, int &outMin, int &outMax) const {
    const int top = height > 0 ? height - 1 : 0;
    const int clamped = std::clamp(layer, 0, top);
    switch (mode) {
      case LayerMode::Single:
        outMin = clamped;
        outMax = clamped;
        break;
      case LayerMode::AllBelow:
        outMin = 0;
        outMax = clamped;
        break;
      case LayerMode::AllAbove:
        outMin = clamped;
        outMax = top;
        break;
      case LayerMode::Range:
        outMin = clamped;
        outMax = std::clamp(layerMax, clamped, top);
        break;
      case LayerMode::All:
      default:
        outMin = 0;
        outMax = top;
        break;
    }
  }

  /// Steps the active layer by `delta`, clamped to the schematic. Used by the step hotkeys.
  void step(int height, int delta) {
    const int top = height > 0 ? height - 1 : 0;
    layer = std::clamp(layer + delta, 0, top);
    if (mode == LayerMode::Range) layerMax = std::max(layerMax, layer);
  }

  /// Cycles the layer mode.
  void cycleMode() {
    mode = static_cast<LayerMode>((static_cast<int>(mode) + 1) % static_cast<int>(LayerMode::Count));
  }

  static const char *modeName(LayerMode m) {
    switch (m) {
      case LayerMode::All: return "All";
      case LayerMode::Single: return "Single Layer";
      case LayerMode::AllBelow: return "All Below";
      case LayerMode::AllAbove: return "All Above";
      case LayerMode::Range: return "Range";
      case LayerMode::Count: break;
    }
    return "All";
  }
};

} // namespace bitematica::litematica
