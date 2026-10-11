#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/MaterialList.hpp"
#include "core/Schematic.hpp"
#include "world/Placement.hpp"
#include "world/Verifier.hpp"

namespace bitematica::core {

/// One row of the live material list.
struct LiveMaterialRow {
  std::string name;
  long long required{};
  long long placed{};
  long long missing{};
  /// True when the row is fully placed.
  bool complete() const { return missing <= 0; }
  /// Blocks still outstanding, never negative.
  long long remaining() const { return missing < 0 ? 0 : missing; }
};

/// Builds a material list that updates as the build progresses.
///
/// The static {@link MaterialList} says how many of each block a schematic needs. This adds the
/// verification result: for every block family, how many are already correct in the world and how
/// many are still missing. That is the difference between "you need 340 stone" and "you have placed
/// 120 of 340 stone, 220 to go", which is what a player actually builds against.

/// A stack breakdown (I4): full stacks plus a remainder, and the shulker-box equivalent.
struct StackBreakdown {
  long long stacks{};
  long long remainder{};
  /// How many shulker boxes (27 stacks each) would hold the whole amount.
  long long shulkers{};
};

class LiveMaterials {
public:
  /// Combines the required counts with a verification result.
  ///
  /// `placed` counts blocks whose status is `Correct`; `missing` is the remainder (Missing + Wrong),
  /// since a wrong block still has to be broken and replaced.
  static std::vector<LiveMaterialRow> build(const Schematic &schematic,
                                            const world::VerifyResult &verification);

  /// Serializes the rows as CSV (header + one row per material), for the export action.
  static std::string toCsv(const std::vector<LiveMaterialRow> &rows);

  /// Serializes the rows as a plain-text shopping list (I6).
  static std::string toText(const std::vector<LiveMaterialRow> &rows);

  /// Splits a count into stacks + remainder (I4). `stackSize` is 64 for most items; a non-stackable
  /// item passes 1.
  static StackBreakdown stacksFor(long long count, int stackSize = 64);

  /// The number of *items* a row needs (I7): most blocks count once, but a door or bed is one item
  /// per half, a double slab counts 2, snow layers/candles/sea pickles/turtle eggs count by their
  /// `count`-style state. `blockCount` is the number of blocks; `state` gives the per-block item
  /// multiplier.
  static long long itemCount(const BlockState &state, long long blockCount);

  /// Filters (I5): hide completed rows, keep only rows with something missing, and a name search.
  struct Filter {
    bool hideCompleted{false};
    bool onlyMissing{false};
    std::string search;
  };
  static std::vector<LiveMaterialRow> applyFilter(const std::vector<LiveMaterialRow> &rows,
                                                  const Filter &filter);
};

} // namespace bitematica::core
