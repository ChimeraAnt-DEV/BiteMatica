#pragma once

#include <string>

#include "mod/Config.hpp"

namespace bitematica::mod {

/// Reads and writes `Config` as a flat JSON object.
///
/// The config is a fixed set of primitives, so this does not need a general JSON
/// library and deliberately does not use one: the mod ships as a single `.so` with no
/// third-party runtime, and a hand-written reader keeps the accepted grammar narrow
/// (a flat object of bool/int/string) which is exactly the shape the mod writes.
/// Unknown keys are ignored on read so a newer config never bricks an older build, and
/// the writer always emits the full set so an older config gains new fields on next save.
class JsonConfig {
public:
  /// Parses JSON text into `config`. Missing keys keep their C++ defaults.
  /// @return false (with `error` set) only when the text is not a JSON object.
  static bool parse(const std::string &json, Config &config, std::string &error);

  /// Serializes a config to a stable, human-readable JSON object.
  static std::string serialize(const Config &config);
};

} // namespace bitematica::mod
