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
};

} // namespace bitematica::core
