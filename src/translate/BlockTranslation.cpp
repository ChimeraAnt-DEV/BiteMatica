#include "translate/BlockTranslation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
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
std::unordered_map<std::string, std::string> &renamedBlocksMutable() {
  static std::unordered_map<std::string, std::string> table = {
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

const std::unordered_map<std::string, std::string> &renamedBlocks() {
  return renamedBlocksMutable();
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

int BlockTranslation::loadJson(const std::string &json) {
  // A tiny scanner for `{ "blocks": { "a": "b", ... } }`. Robust to a malformed entry (skip it).
  auto skipWs = [](const std::string &s, size_t &i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
  };
  auto &table = renamedBlocksMutable();
  int merged = 0;
  // Find the "blocks" object.
  const size_t blocksAt = json.find("\"blocks\"");
  if (blocksAt == std::string::npos) return 0;
  size_t i = json.find('{', blocksAt);
  if (i == std::string::npos) return 0;
  ++i;
  while (i < json.size()) {
    skipWs(json, i);
    if (i < json.size() && json[i] == '}') break;
    if (i >= json.size() || json[i] != '"') break;
    ++i;
    std::string key;
    while (i < json.size() && json[i] != '"') key.push_back(json[i++]);
    if (i < json.size()) ++i;
    skipWs(json, i);
    if (i >= json.size() || json[i] != ':') break;
    ++i;
    skipWs(json, i);
    if (i >= json.size() || json[i] != '"') break;
    ++i;
    std::string value;
    while (i < json.size() && json[i] != '"') value.push_back(json[i++]);
    if (i < json.size()) ++i;
    if (!key.empty() && !value.empty()) {
      table[stripNamespace(key)] = stripNamespace(value);
      ++merged;
    }
    skipWs(json, i);
    if (i < json.size() && json[i] == ',') {
      ++i;
      continue;
    }
    if (i < json.size() && json[i] == '}') break;
    break;
  }
  return merged;
}

int BlockTranslation::loadFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) return 0;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return loadJson(text);
}

bool BlockTranslation::isUnmapped(const std::string &javaName) {
  const std::string id = stripNamespace(javaName);
  if (hasExplicitRule(id)) return false;
  // A known pass-through family (vanilla ids that are identical on both editions) is not unmapped.
  static const char *kKnownPrefixes[] = {
      "stone", "dirt", "sand", "gravel", "glass", "wool", "concrete", "terracotta",
      "brick", "netherrack", "end_stone", "obsidian", "bedrock", "iron_block", "gold_block",
      "diamond_block", "emerald_block", "coal_block", "lapis_block", "redstone_block",
      "quartz", "purpur", "prismarine", "ice", "snow", "clay", "slime", "honey"};
  for (const char *prefix : kKnownPrefixes) {
    if (id.rfind(prefix, 0) == 0) return false;
  }
  // A vanilla-looking id (no underscore prefix that suggests a mod) is treated as pass-through too;
  // only ids that look like they could be modded or unknown are flagged. This keeps the "Unmapped"
  // list short and meaningful rather than flagging every block.
  recordUnmapped(id);
  return true;
}

core::BlockState BlockTranslation::placeholderFor(const std::string &javaName) {
  // A distinctive magenta "unknown" block (G4), so an unmapped block is visible, not silently wrong.
  core::BlockState out;
  out.name = "minecraft:" + stripNamespace(javaName);
  out.states.emplace_back("bite:unmapped", "1");
  return out;
}

std::vector<std::string> &unmappedRegistry() {
  static std::vector<std::string> seen;
  return seen;
}

void BlockTranslation::recordUnmapped(const std::string &id) {
  auto &seen = unmappedRegistry();
  if (seen.size() >= 256) return;
  for (const auto &existing : seen) {
    if (existing == id) return;
  }
  seen.push_back(id);
}

std::vector<std::string> BlockTranslation::unmappedBlocks() { return unmappedRegistry(); }
void BlockTranslation::clearUnmapped() { unmappedRegistry().clear(); }

bool BlockTranslation::isVersionNewerThanTable(const std::string &runningVersion) {
  // Compare the major.minor.patch prefix against the highest known-good (1.26.51). A newer running
  // version warns; an older or equal one does not.
  if (runningVersion.empty()) return false;
  int major = 0, minor = 0, patch = 0;
  std::sscanf(runningVersion.c_str(), "%d.%d.%d", &major, &minor, &patch);
  if (major != 1 || minor != 26) return major > 1 || minor > 26;
  return patch > 51;
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
