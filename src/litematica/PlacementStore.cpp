#include "litematica/PlacementStore.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace bitematica::litematica {
namespace {

std::string escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

/// A very small recursive-descent reader for the placement file's shape. It understands a JSON
/// object, arrays, strings, numbers and booleans — enough for `placements.json` and nothing more.
/// A malformed document fails with a named error rather than a partial read.
struct Reader {
  const std::string &text;
  size_t pos{0};

  void skipWs() {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t')) {
      ++pos;
    }
  }
  bool consume(char c) {
    skipWs();
    if (pos < text.size() && text[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  }
  bool readString(std::string &out) {
    skipWs();
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < text.size() && text[pos] != '"') {
      if (text[pos] == '\\' && pos + 1 < text.size()) {
        ++pos;
        switch (text[pos]) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          default: out.push_back(text[pos]); break;
        }
      } else {
        out.push_back(text[pos]);
      }
      ++pos;
    }
    if (pos >= text.size()) return false;
    ++pos; // closing quote
    return true;
  }
  bool readNumber(double &out) {
    skipWs();
    const size_t start = pos;
    while (pos < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '-' ||
            text[pos] == '+' || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E')) {
      ++pos;
    }
    if (pos == start) return false;
    out = std::strtod(text.substr(start, pos - start).c_str(), nullptr);
    return true;
  }
  bool readBool(bool &out) {
    skipWs();
    if (text.compare(pos, 4, "true") == 0) {
      pos += 4;
      out = true;
      return true;
    }
    if (text.compare(pos, 5, "false") == 0) {
      pos += 5;
      out = false;
      return true;
    }
    return false;
  }
  bool skipValue() {
    skipWs();
    if (pos >= text.size()) return false;
    if (text[pos] == '"') {
      std::string ignored;
      return readString(ignored);
    }
    if (text[pos] == '{' || text[pos] == '[') {
      const char open = text[pos];
      const char close = open == '{' ? '}' : ']';
      int depth = 0;
      while (pos < text.size()) {
        if (text[pos] == '"') {
          std::string ignored;
          if (!readString(ignored)) return false;
          continue;
        }
        if (text[pos] == open) ++depth;
        if (text[pos] == close) {
          --depth;
          ++pos;
          if (depth == 0) return true;
          continue;
        }
        ++pos;
      }
      return false;
    }
    double number = 0;
    if (readNumber(number)) return true;
    bool flag = false;
    return readBool(flag);
  }
};

} // namespace

std::string PlacementStore::serialize(const std::vector<PlacedSchematic> &placements) {
  std::string out = "{\n";
  out += "  \"version\": " + std::to_string(kVersion) + ",\n";
  out += "  \"placements\": [\n";
  for (size_t i = 0; i < placements.size(); ++i) {
    const PlacedSchematic &p = placements[i];
    out += "    {\n";
    out += "      \"schematicId\": \"" + escape(p.schematicId) + "\",\n";
    out += "      \"name\": \"" + escape(p.name()) + "\",\n";
    out += "      \"x\": " + std::to_string(p.placement.origin.x) + ",\n";
    out += "      \"y\": " + std::to_string(p.placement.origin.y) + ",\n";
    out += "      \"z\": " + std::to_string(p.placement.origin.z) + ",\n";
    out += "      \"rotation\": " + std::to_string(static_cast<int>(p.placement.rotation)) + ",\n";
    out += "      \"mirror\": " + std::to_string(static_cast<int>(p.placement.mirror)) + ",\n";
    out += "      \"yOffset\": " + std::to_string(p.placement.yOffset) + ",\n";
    out += "      \"visible\": " + std::string(p.visible ? "true" : "false") + ",\n";
    out += "      \"locked\": " + std::string(p.locked ? "true" : "false") + ",\n";
    out += "      \"layerMode\": " + std::to_string(p.layerMode) + ",\n";
    out += "      \"layerAxis\": " + std::to_string(p.layerAxis) + ",\n";
    out += "      \"layerMin\": " + std::to_string(p.layerMin) + ",\n";
    out += "      \"layerMax\": " + std::to_string(p.layerMax) + ",\n";
    out += "      \"opacity\": " + std::to_string(p.opacity) + ",\n";
    out += "      \"showOutlines\": " + std::string(p.showOutlines ? "true" : "false") + "\n";
    out += "    }";
    out += (i + 1 < placements.size()) ? ",\n" : "\n";
  }
  out += "  ]\n";
  out += "}\n";
  return out;
}

bool PlacementStore::parse(const std::string &json, std::vector<PlacedSchematic> &out,
                           std::string &error) {
  out.clear();
  Reader reader{json};
  if (!reader.consume('{')) {
    error = "placements file is not a JSON object";
    return false;
  }
  int version = kVersion;
  bool sawVersion = false;
  while (true) {
    reader.skipWs();
    if (reader.consume('}')) break;
    std::string key;
    if (!reader.readString(key)) {
      error = "expected a key in placements file";
      return false;
    }
    if (!reader.consume(':')) {
      error = "expected ':' after key " + key;
      return false;
    }
    if (key == "version") {
      double value = 0;
      if (!reader.readNumber(value)) {
        error = "version is not a number";
        return false;
      }
      version = static_cast<int>(value);
      sawVersion = true;
    } else if (key == "placements") {
      if (!reader.consume('[')) {
        error = "placements is not an array";
        return false;
      }
      reader.skipWs();
      if (reader.consume(']')) {
        // empty list
      } else {
        while (true) {
          if (!reader.consume('{')) {
            error = "a placement is not an object";
            return false;
          }
          PlacedSchematic p;
          while (true) {
            reader.skipWs();
            if (reader.consume('}')) break;
            std::string field;
            if (!reader.readString(field) || !reader.consume(':')) {
              error = "malformed placement field";
              return false;
            }
            if (field == "schematicId") {
              reader.readString(p.schematicId);
            } else if (field == "name") {
              reader.readString(p.displayName);
            } else if (field == "x") {
              double v = 0; reader.readNumber(v); p.placement.origin.x = static_cast<int>(v);
            } else if (field == "y") {
              double v = 0; reader.readNumber(v); p.placement.origin.y = static_cast<int>(v);
            } else if (field == "z") {
              double v = 0; reader.readNumber(v); p.placement.origin.z = static_cast<int>(v);
            } else if (field == "rotation") {
              double v = 0; reader.readNumber(v);
              p.placement.rotation = static_cast<world::Rotation>(static_cast<int>(v) & 3);
            } else if (field == "mirror") {
              double v = 0; reader.readNumber(v);
              p.placement.mirror = static_cast<world::Mirror>(static_cast<int>(v) % 3);
            } else if (field == "yOffset") {
              double v = 0; reader.readNumber(v); p.placement.yOffset = static_cast<int>(v);
            } else if (field == "visible") {
              reader.readBool(p.visible);
            } else if (field == "locked") {
              reader.readBool(p.locked);
            } else if (field == "layerMode") {
              double v = 0; reader.readNumber(v); p.layerMode = static_cast<int>(v);
            } else if (field == "layerAxis") {
              double v = 0; reader.readNumber(v); p.layerAxis = static_cast<int>(v);
            } else if (field == "layerMin") {
              double v = 0; reader.readNumber(v); p.layerMin = static_cast<int>(v);
            } else if (field == "layerMax") {
              double v = 0; reader.readNumber(v); p.layerMax = static_cast<int>(v);
            } else if (field == "opacity") {
              double v = 0; reader.readNumber(v); p.opacity = static_cast<int>(v);
            } else if (field == "showOutlines") {
              reader.readBool(p.showOutlines);
            } else {
              if (!reader.skipValue()) {
                error = "could not skip unknown field " + field;
                return false;
              }
            }
            reader.skipWs();
            if (reader.consume(',')) continue;
            if (reader.consume('}')) break;
            error = "expected ',' or '}' in a placement";
            return false;
          }
          out.push_back(std::move(p));
          reader.skipWs();
          if (reader.consume(',')) continue;
          if (reader.consume(']')) break;
          error = "expected ',' or ']' after a placement";
          return false;
        }
      }
    } else {
      if (!reader.skipValue()) {
        error = "could not skip unknown top-level field " + key;
        return false;
      }
    }
    reader.skipWs();
    if (reader.consume(',')) continue;
    if (reader.consume('}')) break;
    error = "expected ',' or '}' at the top level";
    return false;
  }

  // A file from a future version is refused rather than half-read. An older version is accepted:
  // any field it lacks already took its default above.
  if (sawVersion && version > kVersion) {
    error = "placements file version " + std::to_string(version) +
            " is newer than this build supports (" + std::to_string(kVersion) + ")";
    return false;
  }
  return true;
}

bool PlacementStore::writeFile(const std::string &path,
                               const std::vector<PlacedSchematic> &placements,
                               std::string &error) {
  const std::string text = serialize(placements);
  const std::string tempPath = path + ".tmp";
  {
    std::ofstream out(tempPath, std::ios::trunc);
    if (!out) {
      error = "could not open " + tempPath + " for writing";
      return false;
    }
    out << text;
    if (!out) {
      error = "could not write " + tempPath;
      return false;
    }
  }
  // Atomic replace: rename the temp file over the real one. A crash before this leaves the old file
  // intact; a crash after leaves the new one. Either way the file is never truncated.
  if (std::rename(tempPath.c_str(), path.c_str()) != 0) {
    std::remove(tempPath.c_str());
    error = "could not replace " + path;
    return false;
  }
  return true;
}

bool PlacementStore::readFile(const std::string &path, std::vector<PlacedSchematic> &out,
                              std::string &error) {
  std::ifstream in(path);
  if (!in) {
    error = "no placements file at " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return parse(buffer.str(), out, error);
}

} // namespace bitematica::litematica
