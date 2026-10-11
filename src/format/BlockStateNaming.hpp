#pragma once

#include <string>
#include <vector>

#include "core/BlockState.hpp"

namespace bitematica::format {

/// Parses a Java-style block state string into a portable BlockState.
///
/// Accepts both spellings the Java schematics use:
///   `minecraft:oak_stairs[facing=north,half=bottom]`
///   `minecraft:oak_stairs`                     (no states)
/// and, tolerantly, a bare id with no namespace (`stone` -> `minecraft:stone`). The
/// Bedrock side keys on the same namespaced id, so no id table is needed here.
inline core::BlockState parseJavaState(const std::string &text) {
  core::BlockState state;
  std::string body = text;

  const size_t open = body.find('[');
  if (open != std::string::npos && body.back() == ']') {
    const std::string statePart = body.substr(open + 1, body.size() - open - 2);
    body = body.substr(0, open);
    size_t start = 0;
    while (start <= statePart.size()) {
      const size_t comma = statePart.find(',', start);
      const std::string pair = statePart.substr(
          start, comma == std::string::npos ? std::string::npos : comma - start);
      const size_t eq = pair.find('=');
      if (eq != std::string::npos) {
        state.states.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
      }
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }

  if (body.find(':') == std::string::npos && !body.empty()) {
    body = "minecraft:" + body;
  }
  state.name = body.empty() ? "minecraft:air" : body;
  return state;
}

/// Parses the Bedrock `.nbt` structure palette entry, whose states arrive as a
/// separate compound rather than inline in the name.
inline core::BlockState parseBedrockState(
    const std::string &name,
    const std::vector<std::pair<std::string, std::string>> &states) {
  core::BlockState state;
  state.name = name.empty() ? "minecraft:air" : name;
  if (state.name.find(':') == std::string::npos && !state.name.empty()) {
    state.name = "minecraft:" + state.name;
  }
  state.states = states;
  return state;
}

} // namespace bitematica::format
