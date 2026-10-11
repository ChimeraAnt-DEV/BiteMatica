#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/Schematic.hpp"
#include "world/Placement.hpp"

namespace bitematica::litematica {

/// One placed schematic: the schematic, where it sits, and its per-placement options.
///
/// Litematica can hold several placements at once, each with its own rotation, mirroring, lock and
/// sub-region visibility. This is that record. The `subRegions` flag mirrors Litematica's
/// per-sub-region toggle; a single-region schematic simply has one entry, always visible.
struct PlacedSchematic {
  std::shared_ptr<const core::Schematic> schematic;
  world::Placement placement;
  /// Whether the placement is drawn and verified.
  bool visible{true};
  /// Whether the origin is pinned (Litematica's placement lock).
  bool locked{false};
  /// Per-sub-region visibility, one entry per region in the file.
  std::vector<bool> subRegionVisible{true};

  /// The schematic's display name, or a placeholder.
  std::string name() const { return schematic ? schematic->name : std::string("(empty)"); }
  int sizeX() const { return schematic ? schematic->sizeX() : 0; }
  int sizeY() const { return schematic ? schematic->sizeY() : 0; }
  int sizeZ() const { return schematic ? schematic->sizeZ() : 0; }
};

/// Holds every loaded schematic placement and the active one.
///
/// The "loaded schematics" and "schematic placements" menus in Litematica both operate on this
/// list, so it is the single source of truth: the renderer draws the visible ones, the verifier
/// checks the active one, and the material list sums them.
class PlacementManager {
public:
  /// Adds a placement and makes it active. Returns its index.
  int add(std::shared_ptr<const core::Schematic> schematic, const world::Placement &placement) {
    PlacedSchematic entry;
    entry.schematic = std::move(schematic);
    entry.placement = placement;
    entry.subRegionVisible.assign(1, true);
    mPlacements.push_back(std::move(entry));
    mActive = static_cast<int>(mPlacements.size()) - 1;
    return mActive;
  }

  int count() const { return static_cast<int>(mPlacements.size()); }
  bool empty() const { return mPlacements.empty(); }
  int activeIndex() const { return mActive; }

  void setActive(int index) {
    if (index >= 0 && index < count()) mActive = index;
  }

  PlacedSchematic *active() {
    return (mActive >= 0 && mActive < count()) ? &mPlacements[static_cast<size_t>(mActive)]
                                               : nullptr;
  }
  const PlacedSchematic *active() const {
    return (mActive >= 0 && mActive < count()) ? &mPlacements[static_cast<size_t>(mActive)]
                                               : nullptr;
  }

  PlacedSchematic *at(int index) {
    return (index >= 0 && index < count()) ? &mPlacements[static_cast<size_t>(index)] : nullptr;
  }
  const PlacedSchematic *at(int index) const {
    return (index >= 0 && index < count()) ? &mPlacements[static_cast<size_t>(index)] : nullptr;
  }

  const std::vector<PlacedSchematic> &all() const { return mPlacements; }

  /// Removes a placement. The active index is kept in range.
  void remove(int index) {
    if (index < 0 || index >= count()) return;
    mPlacements.erase(mPlacements.begin() + index);
    if (mPlacements.empty()) {
      mActive = -1;
    } else if (mActive >= count()) {
      mActive = count() - 1;
    }
  }

  /// Removes every placement.
  void clear() {
    mPlacements.clear();
    mActive = -1;
  }

  /// The next / previous visible placement index, wrapping. Used by the placement menu.
  int stepActive(int delta) {
    if (mPlacements.empty()) return -1;
    mActive = ((mActive + delta) % count() + count()) % count();
    return mActive;
  }

private:
  std::vector<PlacedSchematic> mPlacements;
  int mActive{-1};
};

} // namespace bitematica::litematica
