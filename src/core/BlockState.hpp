#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bitematica::core {

/// A single block type plus its block states, independent of any save format.
///
/// Bedrock stores blocks as a numeric runtime id (or a name + states map) in its
/// worlds; a schematic keeps the portable description so a paste can re-resolve
/// the id on whichever build is running. `name` uses the Java-style namespaced id
/// (`minecraft:stone`) because that is what Bedrock's own name->id table also
/// accepts, and `states` holds the flattened block-state pairs.
struct BlockState {
  std::string name{"minecraft:air"};
  std::vector<std::pair<std::string, std::string>> states;

  bool isAir() const { return name.empty() || name == "minecraft:air" || name == "air"; }

  /// A stable string key used for palette de-duplication and material grouping.
  std::string key() const {
    std::string out = name;
    for (const auto &[k, v] : states) {
      out += ';';
      out += k;
      out += '=';
      out += v;
    }
    return out;
  }

  /// The bare block id without any states, used for material grouping.
  std::string baseName() const { return name; }
};

/// De-duplicates block states into palette indices.
class BlockPalette {
public:
  /// Returns the palette index for a state, inserting it when new.
  uint32_t intern(const BlockState &state) {
    const std::string id = state.key();
    auto it = mIndex.find(id);
    if (it != mIndex.end()) return it->second;
    const uint32_t next = static_cast<uint32_t>(mEntries.size());
    mEntries.push_back(state);
    mIndex.emplace(id, next);
    return next;
  }

  uint32_t size() const { return static_cast<uint32_t>(mEntries.size()); }
  const BlockState &at(uint32_t index) const { return mEntries.at(index); }
  const std::vector<BlockState> &entries() const { return mEntries; }

  /// Finds an existing palette index, or -1 when the state is not present.
  int find(const BlockState &state) const {
    auto it = mIndex.find(state.key());
    return it == mIndex.end() ? -1 : static_cast<int>(it->second);
  }

private:
  std::vector<BlockState> mEntries;
  std::unordered_map<std::string, uint32_t> mIndex;
};

} // namespace bitematica::core
