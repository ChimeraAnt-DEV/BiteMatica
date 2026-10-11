#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace bitematica::mod {

/// Finds the schematics a player can cycle through.
///
/// Scans one directory (the mod's own `schematics/` folder by default) for the
/// extensions BiteMatica reads, sorted by name so the toolbar's prev/next order is
/// stable between launches. The directory is created on demand, which is also how a
/// player adds a schematic: drop the file in and press reload.
class SchematicLibrary {
public:
  /// Returns the schematic files in `directory`, sorted, creating it when absent.
  static std::vector<std::string> scan(const std::string &directory) {
    namespace fs = std::filesystem;
    std::vector<std::string> files;
    std::error_code ec;
    fs::path root(directory);
    if (!fs::exists(root, ec)) {
      fs::create_directories(root, ec);
    }
    if (!fs::is_directory(root, ec)) return files;

    for (const fs::directory_entry &entry : fs::directory_iterator(root, ec)) {
      if (ec) break;
      if (!entry.is_regular_file()) continue;
      const std::string name = entry.path().filename().string();
      if (isSchematicName(name)) files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
  }

  static bool isSchematicName(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.ends_with(".litematic") || lower.ends_with(".schem") ||
           lower.ends_with(".schematic") || lower.ends_with(".nbt") ||
           lower.ends_with(".mcstructure");
  }

  /// Advances an index through a list with wraparound.
  static int step(int index, int delta, int count) {
    if (count <= 0) return 0;
    int next = (index + delta) % count;
    if (next < 0) next += count;
    return next;
  }
};

} // namespace bitematica::mod
