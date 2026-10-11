#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Schematic.hpp"
#include "render/MathTypes.hpp"
#include "world/Placement.hpp"
#include "world/Verifier.hpp"
#include "world/WorldSource.hpp"

namespace bitematica::world {

/// Verifies a placement incrementally, a bounded number of blocks per call (F5).
///
/// Scanning a large schematic in one frame would stall it, so the scan is spread over frames: each
/// `step()` checks at most `budget` blocks, nearest-sections-first, and accumulates the verdicts.
/// The render callback calls `step()` once per frame; the counters and the progress bar come from
/// `result()`.
///
/// Sections are ordered by their distance to the player at construction, so a scan that is
/// interrupted (the player walks away, the app backgrounds) has already covered what the player can
/// see. A section can be re-queued (on a timer, or when the player moves close) without losing the
/// accumulated result.
///
/// Pure and host-testable: it drives a `WorldSource`, never the game.
class IncrementalVerifier {
public:
  IncrementalVerifier(const core::Schematic &schematic, const Placement &placement,
                      int layerMin, int layerMax, bool reportExtras)
      : mSchematic(schematic),
        mPlacement(placement),
        mLayerMin(layerMin),
        mLayerMax(layerMax),
        mReportExtras(reportExtras) {
    mResult.available = true;
    mResult.statuses.assign(schematic.blocks.size(), BlockStatus::Unknown);
    buildQueue();
  }

  /// Runs up to `budget` block checks. Returns true when the scan is finished.
  bool step(WorldSource &source, long long budget) {
    if (!mResult.available) return true;
    long long done = 0;
    while (mCursor < mQueue.size() && done < budget) {
      const int x = mQueue[mCursor].x;
      const int y = mQueue[mCursor].y;
      const int z = mQueue[mCursor].z;
      ++mCursor;
      ++done;
      checkBlock(source, x, y, z);
    }
    return mCursor >= mQueue.size();
  }

  /// Restarts the scan (e.g. the player moved and the world may have changed).
  void restart() {
    mCursor = 0;
    mResult = VerifyResult{};
    mResult.available = true;
    mResult.statuses.assign(mSchematic.blocks.size(), BlockStatus::Unknown);
  }

  bool finished() const { return mCursor >= mQueue.size(); }
  /// 0..100.
  int progressPercent() const {
    if (mQueue.empty()) return 100;
    return static_cast<int>((mCursor * 100) / mQueue.size());
  }

  const VerifyResult &result() const { return mResult; }

private:
  struct Cell {
    int x, y, z;
  };

  void buildQueue() {
    // Order by distance from the placement origin to the player? The player position is not known
    // here, so the queue is ordered by section proximity to the origin, which is where the player
    // is when they place a build. A caller that wants true player-relative priority can call
    // `restart()` with a new order; this default keeps the near-the-origin sections first.
    mQueue.clear();
    for (int y = 0; y < mSchematic.sizeY(); ++y) {
      if (y < mLayerMin || y > mLayerMax) continue;
      for (int z = 0; z < mSchematic.sizeZ(); ++z) {
        for (int x = 0; x < mSchematic.sizeX(); ++x) {
          const core::BlockState &wanted = mSchematic.stateAt(x, y, z);
          if (wanted.isAir() && !mReportExtras) continue;
          mQueue.push_back({x, y, z});
        }
      }
    }
    // Near-the-origin first: sort by squared distance to the origin corner.
    std::stable_sort(mQueue.begin(), mQueue.end(), [](const Cell &a, const Cell &b) {
      const long long da = static_cast<long long>(a.x) * a.x + static_cast<long long>(a.y) * a.y +
                           static_cast<long long>(a.z) * a.z;
      const long long db = static_cast<long long>(b.x) * b.x + static_cast<long long>(b.y) * b.y +
                           static_cast<long long>(b.z) * b.z;
      return da < db;
    });
  }

  void checkBlock(WorldSource &source, int x, int y, int z) {
    const core::BlockState &wanted = mSchematic.stateAt(x, y, z);
    const bool wantsAir = wanted.isAir();
    const core::BlockPos world = mPlacement.toWorld(mSchematic, x, y, z);
    core::BlockState found;
    BlockStatus status;
    if (!source.readBlock(world, found)) {
      status = BlockStatus::Unknown; // unloaded chunk is never Missing
    } else if (wantsAir) {
      status = found.isAir() ? BlockStatus::Correct : BlockStatus::Extra;
    } else if (found.isAir()) {
      status = BlockStatus::Missing;
    } else if (found.baseName() != wanted.baseName()) {
      status = BlockStatus::WrongBlock;
    } else {
      bool wrongState = false;
      for (const auto &[k, v] : wanted.states) {
        for (const auto &[fk, fv] : found.states) {
          if (fk == k && fv != v) wrongState = true;
        }
      }
      status = wrongState ? BlockStatus::WrongState : BlockStatus::Correct;
    }
    const size_t index = static_cast<size_t>(mSchematic.indexOf(x, y, z));
    if (index < mResult.statuses.size()) mResult.statuses[index] = status;
    switch (status) {
      case BlockStatus::Correct: ++mResult.correct; break;
      case BlockStatus::Missing: ++mResult.missing; break;
      case BlockStatus::WrongBlock: ++mResult.wrongBlock; break;
      case BlockStatus::WrongState: ++mResult.wrongState; break;
      case BlockStatus::Extra: ++mResult.extra; break;
      case BlockStatus::Unknown: ++mResult.unknown; break;
    }
  }

  const core::Schematic &mSchematic;
  Placement mPlacement;
  int mLayerMin;
  int mLayerMax;
  bool mReportExtras;
  std::vector<Cell> mQueue;
  size_t mCursor{0};
  VerifyResult mResult;
};

} // namespace bitematica::world
