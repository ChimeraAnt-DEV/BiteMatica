#include "format/SchematicFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>

#include "format/BedrockStructureReader.hpp"
#include "format/LitematicReader.hpp"
#include "format/McEditReader.hpp"
#include "format/SpongeReader.hpp"
#include "nbt/NbtReader.hpp"

namespace bitematica::format {
namespace {

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string extensionOf(const std::string &fileName) {
  const size_t dot = fileName.find_last_of('.');
  if (dot == std::string::npos) return {};
  return toLower(fileName.substr(dot));
}

/// Chooses the reader by inspecting the root compound's members, which is the only
/// reliable signal once the bytes are decompressed.
FileKind classifyRoot(const nbt::Tag &root) {
  if (root.find("Regions") != nullptr) return FileKind::Litematic;
  if (root.find("BlockData") != nullptr && root.find("Palette") != nullptr) {
    return FileKind::SpongeSchem;
  }
  if (root.find("block_indices") != nullptr || root.find("block_palette") != nullptr) {
    return FileKind::Structure;
  }
  if (root.find("Blocks") != nullptr && root.find("Width") != nullptr) {
    return FileKind::McEdit;
  }
  // A litematic from an older tool may only carry Version + Regions-like Metadata.
  if (root.find("MinecraftDataVersion") != nullptr) return FileKind::Litematic;
  return FileKind::Unknown;
}

LoadResult readAs(FileKind kind, const nbt::Tag &root) {
  switch (kind) {
    case FileKind::Litematic:
      return LitematicReader::read(root);
    case FileKind::SpongeSchem:
      return SpongeReader::read(root);
    case FileKind::McEdit:
      return McEditReader::read(root);
    case FileKind::Structure:
      return BedrockStructureReader::read(root);
    case FileKind::Unknown:
      break;
  }
  return LoadResult::fail("unrecognized schematic format");
}

} // namespace

FileKind SchematicFormat::detect(const std::vector<uint8_t> &bytes,
                                 const std::string &fileName) {
  const nbt::Document document = nbt::NbtReader::readFile(bytes);
  if (!document.valid()) return FileKind::Unknown;

  const FileKind byShape = classifyRoot(document.root);
  if (byShape != FileKind::Unknown) return byShape;

  // Shape was inconclusive; fall back to the extension as a tie-break.
  const std::string ext = extensionOf(fileName);
  if (ext == ".litematic") return FileKind::Litematic;
  if (ext == ".schem") return FileKind::SpongeSchem;
  if (ext == ".schematic") return FileKind::McEdit;
  if (ext == ".nbt" || ext == ".mcstructure") return FileKind::Structure;
  return FileKind::Unknown;
}

LoadResult SchematicFormat::read(const std::vector<uint8_t> &bytes,
                                 const std::string &fileName) {
  if (bytes.empty()) return LoadResult::fail("file is empty");

  const nbt::Document document = nbt::NbtReader::readFile(bytes);
  if (!document.valid()) {
    return LoadResult::fail("not a readable NBT document: " + document.error);
  }

  const FileKind kind = classifyRoot(document.root);
  if (kind != FileKind::Unknown) {
    LoadResult result = readAs(kind, document.root);
    if (result.success) return result;
    // A shape match that failed to parse: try the extension's reader before giving up.
  }

  const FileKind byExt = detect(bytes, fileName);
  if (byExt != FileKind::Unknown && byExt != kind) {
    LoadResult result = readAs(byExt, document.root);
    if (result.success) return result;
  }

  if (kind == FileKind::Unknown) {
    return LoadResult::fail("unrecognized schematic format");
  }
  return readAs(kind, document.root);
}

LoadResult SchematicFormat::readPath(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return LoadResult::fail("could not open file: " + path);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  return read(bytes, path);
}

const char *SchematicFormat::kindName(FileKind kind) {
  switch (kind) {
    case FileKind::Litematic:
      return "Litematica (.litematic)";
    case FileKind::SpongeSchem:
      return "Sponge/WorldEdit (.schem)";
    case FileKind::McEdit:
      return "MCEdit (.schematic)";
    case FileKind::Structure:
      return "Bedrock structure (.nbt)";
    case FileKind::Unknown:
      break;
  }
  return "Unknown";
}

bool SchematicFormat::looksLikeSchematic(const std::string &fileName) {
  const std::string ext = extensionOf(fileName);
  return ext == ".litematic" || ext == ".schem" || ext == ".schematic" ||
         ext == ".nbt" || ext == ".mcstructure";
}

} // namespace bitematica::format
