#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "core/BlockState.hpp"
#include "core/Schematic.hpp"

namespace bitematica::core {

/// One row of the material list: a block id and how many are needed.
struct MaterialEntry {
  std::string name;
  uint64_t count{};
};

/// Builds the sorted "how much of each block does this schematic need" list.
///
/// This is the Bedrock counterpart of Litematica's material list. It counts by the
/// *base* block id rather than the full state key, because a player cares about how
/// many stone they need, not how many are `axis=y` versus `axis=x`. Air is excluded.
class MaterialList {
public:
  static std::vector<MaterialEntry> build(const Schematic &schematic) {
    std::vector<MaterialEntry> counts;
    for (uint32_t paletteIndex = 0; paletteIndex < schematic.palette.size();
         ++paletteIndex) {
      const BlockState &state = schematic.palette.at(paletteIndex);
      if (state.isAir()) continue;
      uint64_t total = 0;
      for (uint32_t block : schematic.blocks) {
        if (block == paletteIndex) ++total;
      }
      if (total > 0) counts.push_back({state.baseName(), total});
    }
    std::sort(counts.begin(), counts.end(),
              [](const MaterialEntry &a, const MaterialEntry &b) {
                if (a.count != b.count) return a.count > b.count;
                return a.name < b.name;
              });
    return counts;
  }

  /// Total non-air blocks in the schematic.
  static uint64_t totalBlocks(const Schematic &schematic) {
    uint64_t total = 0;
    for (const MaterialEntry &entry : build(schematic)) total += entry.count;
    return total;
  }
};

} // namespace bitematica::core
