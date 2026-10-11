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

std::string LiveMaterials::toText(const std::vector<LiveMaterialRow> &rows) {
  std::string out = "BiteMatica material list\n";
  out += "block                  required  placed  missing\n";
  for (const LiveMaterialRow &row : rows) {
    std::string name = row.name;
    if (name.size() < 20) name.append(20 - name.size(), ' ');
    const std::string required = std::to_string(row.required);
    const std::string placed = std::to_string(row.placed);
    const std::string missing = std::to_string(row.missing);
    out += name + "  " + std::string(8 - std::min<size_t>(8, required.size()), ' ') + required;
    out += "  " + std::string(6 - std::min<size_t>(6, placed.size()), ' ') + placed;
    out += "  " + std::string(7 - std::min<size_t>(7, missing.size()), ' ') + missing + "\n";
  }
  return out;
}

StackBreakdown LiveMaterials::stacksFor(long long count, int stackSize) {
  StackBreakdown out;
  if (stackSize <= 0) stackSize = 64;
  if (count <= 0) return out;
  out.stacks = count / stackSize;
  out.remainder = count % stackSize;
  // A shulker box holds 27 stacks.
  const long long totalStacks = out.stacks + (out.remainder > 0 ? 1 : 0);
  out.shulkers = (totalStacks + 26) / 27;
  return out;
}

long long LiveMaterials::itemCount(const BlockState &state, long long blockCount) {
  if (blockCount <= 0) return 0;
  const std::string &name = state.name;
  // A door or bed occupies two blocks but is one crafted item, so a two-block door needs one item.
  const bool isDoor = name.size() >= 5 && name.compare(name.size() - 5, 5, "_door") == 0;
  const bool isBed = name.size() >= 4 && name.compare(name.size() - 4, 4, "_bed") == 0;
  if (isDoor || isBed) return (blockCount + 1) / 2;
  // A double slab (`top_slot_bit` on a slab that spans both halves) is two items; a normal slab is
  // one. Detect via the `double` state some formats carry.
  for (const auto &[k, v] : state.states) {
    if (k == "double" && (v == "1" || v == "true")) return blockCount * 2;
  }
  // Snow layers, candles, sea pickles, turtle eggs: the `count`/`height`/`layers` state is how many
  // items one block holds.
  for (const auto &[k, v] : state.states) {
    if (k == "count" || k == "candles" || k == "cluster_count" || k == "turtle_egg_count") {
      int n = 0;
      for (char c : v) {
        if (c >= '0' && c <= '9') n = n * 10 + (c - '0');
      }
      if (n > 1) return blockCount * n;
    }
    if (k == "height" && name.find("snow") != std::string::npos) {
      int n = 0;
      for (char c : v) {
        if (c >= '0' && c <= '9') n = n * 10 + (c - '0');
      }
      if (n > 0) return blockCount * n;
    }
  }
  return blockCount;
}

std::vector<LiveMaterialRow> LiveMaterials::applyFilter(const std::vector<LiveMaterialRow> &rows,
                                                        const Filter &filter) {
  std::vector<LiveMaterialRow> out;
  for (const LiveMaterialRow &row : rows) {
    if (filter.hideCompleted && row.complete()) continue;
    if (filter.onlyMissing && row.remaining() <= 0) continue;
    if (!filter.search.empty() &&
        row.name.find(filter.search) == std::string::npos) {
      continue;
    }
    out.push_back(row);
  }
  return out;
}

} // namespace bitematica::core
