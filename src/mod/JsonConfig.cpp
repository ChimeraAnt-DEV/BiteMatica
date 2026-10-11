#include "mod/JsonConfig.hpp"

#include <cctype>
#include <cstdlib>

namespace bitematica::mod {
namespace {

/// A tiny scanner over a flat JSON object. It understands only what the config uses:
/// `{ "key": true|false|number|"string", ... }`. A nested object or array is a parse
/// error rather than being skipped, so a malformed config is reported, not half-read.
class Scanner {
public:
  explicit Scanner(const std::string &text) : mText(text) {}

  bool parseObject(Config &config, std::string &error) {
    skipWhitespace();
    if (!consume('{')) return fail(error, "config is not a JSON object");
    skipWhitespace();
    if (consume('}')) return true;
    while (true) {
      skipWhitespace();
      std::string key;
      if (!readString(key)) return fail(error, "expected a key");
      skipWhitespace();
      if (!consume(':')) return fail(error, "expected ':' after a key");
      skipWhitespace();
      if (!readValue(key, config)) return fail(error, "bad value for key " + key);
      skipWhitespace();
      if (consume(',')) continue;
      if (consume('}')) return true;
      return fail(error, "expected ',' or '}'");
    }
  }

private:
  bool fail(std::string &error, const std::string &message) {
    error = message;
    return false;
  }

  void skipWhitespace() {
    while (mPos < mText.size() &&
           std::isspace(static_cast<unsigned char>(mText[mPos]))) {
      ++mPos;
    }
  }

  bool consume(char c) {
    if (mPos < mText.size() && mText[mPos] == c) {
      ++mPos;
      return true;
    }
    return false;
  }

  bool readString(std::string &out) {
    if (!consume('"')) return false;
    out.clear();
    while (mPos < mText.size()) {
      const char c = mText[mPos++];
      if (c == '"') return true;
      if (c == '\\' && mPos < mText.size()) {
        const char esc = mText[mPos++];
        switch (esc) {
          case 'n': out.push_back('\n'); break;
          case 't': out.push_back('\t'); break;
          case 'r': out.push_back('\r'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          default: out.push_back(esc); break;
        }
      } else {
        out.push_back(c);
      }
    }
    return true; // unterminated string reads to end; the object check still catches it
  }

  bool readValue(const std::string &key, Config &config) {
    if (mPos < mText.size() && mText[mPos] == '"') {
      std::string value;
      if (!readString(value)) return false;
      if (key == "lastLoadedFile") config.lastLoadedFile = value;
      return true;
    }
    // Number or boolean literal.
    std::string token;
    while (mPos < mText.size()) {
      const char c = mText[mPos];
      if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c)) ||
          c == '{' || c == '[') {
        break;
      }
      token.push_back(c);
      ++mPos;
    }
    if (token.empty()) return false;
    if (token == "true" || token == "false") {
      const bool value = token == "true";
      applyToggle(key, value, config);
      return true;
    }
    const long number = std::strtol(token.c_str(), nullptr, 10);
    applyNumber(key, static_cast<int>(number), config);
    return true;
  }

  static void applyToggle(const std::string &key, bool value, Config &config) {
    if (key == "enabled") config.enabled = value;
    else if (key == "showMaterialList") config.showMaterialList = value;
    else if (key == "showToolbar") config.showToolbar = value;
    else if (key == "ghostPlacement") config.ghostPlacement = value;
    else if (key == "onlyExposed") config.onlyExposed = value;
    else if (key == "verifyAgainstWorld") config.verifyAgainstWorld = value;
    else if (key == "verifyExtras") config.verifyExtras = value;
  }

  static void applyNumber(const std::string &key, int value, Config &config) {
    if (key == "version") config.version = value;
    else if (key == "renderMode") config.renderMode = static_cast<RenderMode>(value);
    else if (key == "opacity") config.opacity = value;
    else if (key == "maxRenderBlocks") config.maxRenderBlocks = value;
    else if (key == "maxTriangles") config.maxTriangles = value;
    else if (key == "renderDistance") config.renderDistance = value;
    else if (key == "layerMode") config.layerMode = static_cast<LayerMode>(value);
    else if (key == "layerSingle") config.layerSingle = value;
    else if (key == "layerMin") config.layerMin = value;
    else if (key == "layerMax") config.layerMax = value;
    else if (key == "toolbarX") config.toolbarX = value;
    else if (key == "toolbarY") config.toolbarY = value;
    else if (key == "materialX") config.materialX = value;
    else if (key == "materialY") config.materialY = value;
    else if (key == "maxRenderDistance") config.maxRenderDistance = value;
  }

  const std::string &mText;
  size_t mPos{};
};

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

} // namespace

bool JsonConfig::parse(const std::string &json, Config &config, std::string &error) {
  if (json.empty()) {
    error = "config file is empty";
    return false;
  }
  Scanner scanner(json);
  if (!scanner.parseObject(config, error)) return false;
  config.normalize();
  return true;
}

std::string JsonConfig::serialize(const Config &config) {
  const char *mode = "0";
  switch (config.renderMode) {
    case RenderMode::Overlay: mode = "0"; break;
    case RenderMode::Skeleton: mode = "1"; break;
    case RenderMode::Hidden: mode = "2"; break;
  }
  auto boolean = [](bool value) { return std::string(value ? "true" : "false"); };

  std::string out = "{\n";
  out += "  \"version\": " + std::to_string(config.version) + ",\n";
  out += "  \"enabled\": " + boolean(config.enabled) + ",\n";
  out += "  \"renderMode\": " + std::string(mode) + ",\n";
  out += "  \"opacity\": " + std::to_string(config.opacity) + ",\n";
  out += "  \"showMaterialList\": " + boolean(config.showMaterialList) + ",\n";
  out += "  \"showToolbar\": " + boolean(config.showToolbar) + ",\n";
  out += "  \"ghostPlacement\": " + boolean(config.ghostPlacement) + ",\n";
  out += "  \"onlyExposed\": " + boolean(config.onlyExposed) + ",\n";
  out += "  \"maxRenderBlocks\": " + std::to_string(config.maxRenderBlocks) + ",\n";
  out += "  \"maxTriangles\": " + std::to_string(config.maxTriangles) + ",\n";
  out += "  \"renderDistance\": " + std::to_string(config.renderDistance) + ",\n";
  out += "  \"layerMode\": " + std::to_string(static_cast<int>(config.layerMode)) + ",\n";
  out += "  \"layerSingle\": " + std::to_string(config.layerSingle) + ",\n";
  out += "  \"layerMin\": " + std::to_string(config.layerMin) + ",\n";
  out += "  \"layerMax\": " + std::to_string(config.layerMax) + ",\n";
  out += "  \"verifyAgainstWorld\": " + boolean(config.verifyAgainstWorld) + ",\n";
  out += "  \"verifyExtras\": " + boolean(config.verifyExtras) + ",\n";
  out += "  \"toolbarX\": " + std::to_string(config.toolbarX) + ",\n";
  out += "  \"toolbarY\": " + std::to_string(config.toolbarY) + ",\n";
  out += "  \"materialX\": " + std::to_string(config.materialX) + ",\n";
  out += "  \"materialY\": " + std::to_string(config.materialY) + ",\n";
  out += "  \"maxRenderDistance\": " + std::to_string(config.maxRenderDistance) + ",\n";
  out += "  \"lastLoadedFile\": \"" + escape(config.lastLoadedFile) + "\"\n";
  out += "}\n";
  return out;
}

} // namespace bitematica::mod
