#include "translate/BlockTranslation.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace bitematica::translate {
namespace {

std::string stripNamespace(const std::string &name) {
  const size_t colon = name.find(':');
  return colon == std::string::npos ? name : name.substr(colon + 1);
}

/// Java's `facing` and Bedrock's `weirdo_direction` for stairs use *opposite* numeric meanings:
/// Java's facing is the direction the stairs ascend toward (north=0), while Bedrock's
/// weirdo_direction is the direction they face (east=0, rotating the other way). Mapping the four
/// cardinals is the whole translation; getting this wrong is the classic "stairs all face
/// backwards" bug, which is why it is a named function rather than an inline table.
std::string javaFacingToBedrockDirection(const std::string &facing) {
  if (facing == "east") return "0";
  if (facing == "west") return "1";
  if (facing == "south") return "2";
  if (facing == "north") return "3";
  return "0";
}

/// Java ids that Bedrock spells differently.
const std::unordered_map<std::string, std::string> &renamedBlocks() {
  static const std::unordered_map<std::string, std::string> table = {
      {"grass_block", "grass"},
      {"oak_planks", "planks"},
      {"spruce_planks", "planks"},
      {"birch_planks", "planks"},
      {"jungle_planks", "planks"},
      {"acacia_planks", "planks"},
      {"dark_oak_planks", "planks"},
      {"oak_sapling", "sapling"},
      {"spruce_sapling", "sapling"},
      {"birch_sapling", "sapling"},
      {"redstone_wire", "redstone_wire"},
      {"redstone_torch", "redstone_torch"},
      {"wall_torch", "torch"},
      {"jack_o_lantern", "lit_pumpkin"},
      {"cobweb", "web"},
      {"terracotta", "hardened_clay"},
      {"white_terracotta", "white_terracotta"},
      {"dirt_path", "grass_path"},
      {"smooth_stone", "smooth_stone"},
      {"stone_slab", "stone_block_slab"},
      {"snow", "snow_layer"},
      {"cauldron", "cauldron"},
  };
  return table;
}

/// True for the block families that carry a `facing` state Bedrock renames.
bool isDirectionalFamily(const std::string &id) {
  static const char *kSuffixes[] = {
      "_stairs", "_door", "_trapdoor", "_fence_gate", "_wall_sign", "_sign", "_ladder"};
  for (const char *suffix : kSuffixes) {
    if (id.size() >= std::strlen(suffix) &&
        id.compare(id.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0) {
      return true;
    }
  }
  return false;
}

} // namespace

bool BlockTranslation::hasExplicitRule(const std::string &javaName) {
  const std::string id = stripNamespace(javaName);
  return renamedBlocks().count(id) > 0 || isDirectionalFamily(id);
}

core::BlockState BlockTranslation::toBedrock(const core::BlockState &java) {
  core::BlockState out;
  const std::string id = stripNamespace(java.name);

  // 1. Rename the block id where Bedrock uses a different spelling.
  auto it = renamedBlocks().find(id);
  const std::string bedrockId = it != renamedBlocks().end() ? it->second : id;
  out.name = "minecraft:" + bedrockId;

  // 2. Translate states.
  const bool directional = isDirectionalFamily(id);
  for (const auto &[key, value] : java.states) {
    if (directional && key == "facing") {
      // A stairs/door `facing` becomes Bedrock's `weirdo_direction` with the inverted meaning.
      out.states.emplace_back("weirdo_direction", javaFacingToBedrockDirection(value));
      continue;
    }
    if (directional && key == "half") {
      // Java `half=top` is Bedrock's `upside_down_bit=true`.
      out.states.emplace_back("upside_down_bit", value == "top" ? "1" : "0");
      continue;
    }
    if (key == "waterlogged") {
      // Bedrock waterlogging lives in the extra block layer, not a state; drop it and let the
      // renderer treat the block as dry. Documented limitation, not a silent guess.
      continue;
    }
    if (key == "powered" || key == "lit" || key == "open" || key == "in_wall" ||
        key == "snowy" || key == "distance") {
      // States with no clean Bedrock equivalent on the base block; dropped deliberately.
      continue;
    }
    // Everything else passes through unchanged, which is correct for the many blocks whose state
    // vocabulary the two editions share (axis, age, etc.).
    out.states.emplace_back(key, value);
  }

  return out;
}

std::string BlockTranslation::describe(const core::BlockState &java, const core::BlockState &bedrock) {
  if (java.name == bedrock.name && java.states == bedrock.states) {
    return java.name + " (unchanged)";
  }
  std::string out = java.name + " -> " + bedrock.name;
  if (!bedrock.states.empty()) {
    out += "[";
    for (size_t i = 0; i < bedrock.states.size(); ++i) {
      if (i) out += ",";
      out += bedrock.states[i].first + "=" + bedrock.states[i].second;
    }
    out += "]";
  }
  return out;
}

} // namespace bitematica::translate
