#include "core/LiveMaterials.hpp"

#include <algorithm>
#include <unordered_map>

namespace bitematica::core {

std::vector<LiveMaterialRow> LiveMaterials::build(const Schematic &schematic,
                                                  const world::VerifyResult &verification) {
  std::unordered_map<std::string, LiveMaterialRow> rows;

  const bool haveVerification =
      verification.available && verification.statuses.size() == schematic.blocks.size();

  for (uint32_t paletteIndex = 0; paletteIndex < schematic.palette.size(); ++paletteIndex) {
    const BlockState &state = schematic.palette.at(paletteIndex);
    if (state.isAir()) continue;
    LiveMaterialRow &row = rows[state.baseName()];
    row.name = state.baseName();
    for (size_t i = 0; i < schematic.blocks.size(); ++i) {
      if (schematic.blocks[i] != paletteIndex) continue;
      ++row.required;
      if (haveVerification) {
        const world::BlockStatus status = verification.statuses[i];
        if (status == world::BlockStatus::Correct) {
          ++row.placed;
        } else if (status == world::BlockStatus::Missing ||
                   status == world::BlockStatus::WrongBlock ||
                   status == world::BlockStatus::WrongState) {
          // A wrong block or state still has to be broken and replaced, so it counts as outstanding.
          // `Unknown` (an unreadable chunk) is deliberately neither placed nor missing — it is not
          // the player's fault and must not inflate the shopping list.
          ++row.missing;
        }
      }
    }
  }

  // Without verification, "missing" is unknown; report everything as outstanding so the list is
  // still a useful shopping list rather than showing zero-to-go.
  if (!haveVerification) {
    for (auto &[name, row] : rows) {
      (void)name;
      row.missing = row.required;
    }
  }

  std::vector<LiveMaterialRow> out;
  out.reserve(rows.size());
  for (auto &[name, row] : rows) {
    (void)name;
    out.push_back(row);
  }
  std::sort(out.begin(), out.end(), [](const LiveMaterialRow &a, const LiveMaterialRow &b) {
    if (a.missing != b.missing) return a.missing > b.missing;
    if (a.required != b.required) return a.required > b.required;
    return a.name < b.name;
  });
  return out;
}

std::string LiveMaterials::toCsv(const std::vector<LiveMaterialRow> &rows) {
  std::string out = "block,required,placed,missing\n";
  for (const LiveMaterialRow &row : rows) {
    out += row.name;
    out += ',';
    out += std::to_string(row.required);
    out += ',';
    out += std::to_string(row.placed);
    out += ',';
    out += std::to_string(row.missing);
    out += '\n';
  }
  return out;
}

} // namespace bitematica::core
