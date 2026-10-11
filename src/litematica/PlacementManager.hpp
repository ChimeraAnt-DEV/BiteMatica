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
  /// The source file this placement was loaded from, so the placement survives a restart (the
  /// schematic itself is re-read from the file; only this id is persisted). Empty for an in-memory
  /// placement that was never saved to disk.
  std::string schematicId;
  /// A user-chosen display name (defaults to the schematic name).
  std::string displayName;
  /// Whether the placement is drawn and verified.
  bool visible{true};
  /// Whether the origin is pinned (Litematica's placement lock).
  bool locked{false};
  /// Per-sub-region visibility, one entry per region in the file.
  std::vector<bool> subRegionVisible{true};
  /// Per-placement render settings (D1): layer mode and range on the chosen axis, opacity, outlines.
  int layerMode{0};   ///< 0 all, 1 single, 2 range
  int layerAxis{1};   ///< 0 X, 1 Y, 2 Z
  int layerMin{0};
  int layerMax{255};
  int opacity{60};
  bool showOutlines{true};

  /// The schematic's display name, or a placeholder.
  std::string name() const {
    if (!displayName.empty()) return displayName;
    return schematic ? schematic->name : std::string("(empty)");
  }
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

  /// Records the current placement list on the undo stack (D11).
  ///
  /// Call this *before* a mutating edit (move/rotate/mirror/rename/delete) so the edit can be
  /// undone. The stack is a bounded ring of 32 snapshots; the oldest is dropped when it is full.
  /// The schematic pointers are shared, so a snapshot is cheap (the placement records are small).
  void pushUndo() {
    if (mUndo.size() == kUndoDepth) mUndo.erase(mUndo.begin());
    mUndo.push_back({mPlacements, mActive});
    // Any new edit invalidates the redo stack, as in every editor.
    mRedo.clear();
  }

  /// Undoes the last edit. Returns false when there is nothing to undo.
  bool undo() {
    if (mUndo.empty()) return false;
    mRedo.push_back({mPlacements, mActive});
    if (mRedo.size() > kUndoDepth) mRedo.erase(mRedo.begin());
    auto snapshot = std::move(mUndo.back());
    mUndo.pop_back();
    mPlacements = std::move(snapshot.placements);
    mActive = snapshot.active;
    return true;
  }

  /// Redoes the last undone edit. Returns false when there is nothing to redo.
  bool redo() {
    if (mRedo.empty()) return false;
    mUndo.push_back({mPlacements, mActive});
    auto snapshot = std::move(mRedo.back());
    mRedo.pop_back();
    mPlacements = std::move(snapshot.placements);
    mActive = snapshot.active;
    return true;
  }

  int undoDepth() const { return static_cast<int>(mUndo.size()); }
  int redoDepth() const { return static_cast<int>(mRedo.size()); }

private:
  static constexpr size_t kUndoDepth = 32;
  struct Snapshot {
    std::vector<PlacedSchematic> placements;
    int active{-1};
  };
  std::vector<PlacedSchematic> mPlacements;
  int mActive{-1};
  std::vector<Snapshot> mUndo;
  std::vector<Snapshot> mRedo;
};

} // namespace bitematica::litematica
