#include "format/LitematicWriter.hpp"

#include <cstdint>
#include <fstream>

#include "nbt/NbtReader.hpp"
#include "nbt/NbtWriter.hpp"

namespace bitematica::format {
namespace {

using bitematica::nbt::NbtWriter;
using bitematica::nbt::Tag;
using bitematica::nbt::TagType;

/// The minimum bits needed to index a palette, with Litematica's floor of 2.
int bitsFor(int paletteSize) {
  int bits = 2;
  while ((1 << bits) < paletteSize) ++bits;
  return bits;
}

/// Packs palette indices into 64-bit longs, little-endian bit order (Litematica's layout).
std::vector<int64_t> packLongs(const std::vector<uint32_t> &values, int bits) {
  std::vector<int64_t> longs;
  const int perLong = 64 / bits;
  uint64_t current = 0;
  int offset = 0;
  int inLong = 0;
  const uint64_t mask = (1ull << bits) - 1;
  for (uint32_t value : values) {
    current |= (static_cast<uint64_t>(value) & mask) << offset;
    offset += bits;
    ++inLong;
    if (inLong >= perLong) {
      longs.push_back(static_cast<int64_t>(current));
      current = 0;
      offset = 0;
      inLong = 0;
    }
  }
  if (offset > 0) longs.push_back(static_cast<int64_t>(current));
  return longs;
}

} // namespace

std::string LitematicWriter::bytes(const core::Schematic &schematic, const std::string &name,
                                   const std::string &author) {
  const int sizeX = schematic.sizeX();
  const int sizeY = schematic.sizeY();
  const int sizeZ = schematic.sizeZ();

  // BlockStatePalette: a LIST of COMPOUND { Name, Properties }.
  Tag palette = NbtWriter::list("BlockStatePalette", TagType::Compound);
  for (const core::BlockState &state : schematic.palette.entries()) {
    Tag entry = NbtWriter::compound("");
    entry.children.push_back(NbtWriter::string("Name", state.name));
    if (!state.states.empty()) {
      Tag properties = NbtWriter::compound("Properties");
      for (const auto &[key, value] : state.states) {
        properties.children.push_back(NbtWriter::string(key, value));
      }
      entry.children.push_back(std::move(properties));
    }
    palette.children.push_back(std::move(entry));
  }

  // BlockStates: the packed long array, X-major (index = (y * sizeZ + z) * sizeX + x), which is the
  // order `Schematic::blocks` already uses.
  const int bits = bitsFor(static_cast<int>(schematic.palette.size()));
  Tag blockStates = NbtWriter::compound("");
  blockStates.type = TagType::LongArray;
  blockStates.name = "BlockStates";
  blockStates.array = packLongs(schematic.blocks, bits);

  Tag region = NbtWriter::compound("main");
  Tag position = NbtWriter::compound("Position");
  position.children.push_back(NbtWriter::intTag("x", 0));
  position.children.push_back(NbtWriter::intTag("y", 0));
  position.children.push_back(NbtWriter::intTag("z", 0));
  Tag size = NbtWriter::compound("Size");
  size.children.push_back(NbtWriter::intTag("x", sizeX));
  size.children.push_back(NbtWriter::intTag("y", sizeY));
  size.children.push_back(NbtWriter::intTag("z", sizeZ));
  region.children.push_back(std::move(position));
  region.children.push_back(std::move(size));
  region.children.push_back(std::move(palette));
  region.children.push_back(std::move(blockStates));

  // Block entities (G5): each is raw NBT bytes captured by a reader. Re-parse it into a tag and
  // attach its `Pos` (relative to the region) so Litematica can place the chest contents, sign
  // text, etc. A block entity that will not re-parse is skipped, not written as garbage.
  if (!schematic.blockEntities.empty()) {
    Tag blockEntities = NbtWriter::list("BlockEntities", TagType::Compound);
    for (const auto &[index, raw] : schematic.blockEntities) {
      // The raw bytes are a full named compound; parse them back through the reader.
      const nbt::Document parsed =
          nbt::NbtReader::readRaw(std::vector<uint8_t>(raw.begin(), raw.end()));
      if (!parsed.valid()) continue;
      Tag entry = parsed.root;
      entry.name.clear();
      // Attach the position if the reader did not (some formats store it elsewhere).
      if (entry.find("Pos") == nullptr && index >= 0) {
        const int x = index % sizeX;
        const int y = (index / sizeX) / sizeZ;
        const int z = (index / sizeX) % sizeZ;
        Tag pos = NbtWriter::list("Pos", TagType::Int);
        pos.children.push_back(NbtWriter::intTag("", x));
        pos.children.push_back(NbtWriter::intTag("", y));
        pos.children.push_back(NbtWriter::intTag("", z));
        entry.children.push_back(std::move(pos));
      }
      blockEntities.children.push_back(std::move(entry));
    }
    if (!blockEntities.children.empty()) region.children.push_back(std::move(blockEntities));
  }

  Tag regions = NbtWriter::compound("Regions");
  regions.children.push_back(std::move(region));

  Tag metadata = NbtWriter::compound("Metadata");
  metadata.children.push_back(
      NbtWriter::string("Name", name.empty() ? schematic.name : name));
  metadata.children.push_back(NbtWriter::string("Author", author.empty() ? "BiteMatica" : author));
  metadata.children.push_back(NbtWriter::string("Description", schematic.description));
  metadata.children.push_back(NbtWriter::intTag("RegionCount", 1));

  Tag root = NbtWriter::compound("Schematic");
  root.children.push_back(NbtWriter::intTag("MinecraftDataVersion", 3120));
  root.children.push_back(NbtWriter::intTag("Version", 6));
  root.children.push_back(std::move(metadata));
  root.children.push_back(std::move(regions));

  const std::vector<uint8_t> encoded = NbtWriter::writeFile(root, true);
  return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

bool LitematicWriter::write(const core::Schematic &schematic, const std::string &path,
                            const std::string &name, const std::string &author,
                            std::string &error) {
  if (schematic.empty()) {
    error = "nothing is loaded to save";
    return false;
  }
  const std::string data = bytes(schematic, name, author);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "could not open the save path";
    return false;
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    error = "could not write the .litematic";
    return false;
  }
  return true;
}

} // namespace bitematica::format
