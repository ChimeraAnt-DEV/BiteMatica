#include "format/StructureWriter.hpp"

#include <fstream>
#include <vector>

#include "nbt/NbtWriter.hpp"

namespace bitematica::format {
namespace {

using bitematica::nbt::Tag;
using bitematica::nbt::TagType;

Tag makeList(const std::string &name, TagType elementType) {
  return bitematica::nbt::NbtWriter::list(name, elementType);
}

Tag makeCompound(const std::string &name) {
  return bitematica::nbt::NbtWriter::compound(name);
}

} // namespace

std::string StructureWriter::bytes(const core::Schematic &schematic) {
  const int sx = schematic.sizeX();
  const int sy = schematic.sizeY();
  const int sz = schematic.sizeZ();

  // size: LIST of INT [x, y, z]
  Tag size = makeList("size", TagType::Int);
  for (int value : {sx, sy, sz}) {
    Tag element = bitematica::nbt::NbtWriter::intTag("", value);
    size.children.push_back(std::move(element));
  }

  // palette/default/block_palette: LIST of COMPOUND { name, states }
  Tag blockPalette = makeList("block_palette", TagType::Compound);
  for (const core::BlockState &state : schematic.palette.entries()) {
    Tag entry = makeCompound("");
    entry.children.push_back(bitematica::nbt::NbtWriter::string("name", state.name));
    Tag states = makeCompound("states");
    for (const auto &[key, value] : state.states) {
      states.children.push_back(bitematica::nbt::NbtWriter::string(key, value));
    }
    entry.children.push_back(std::move(states));
    blockPalette.children.push_back(std::move(entry));
  }
  Tag defaultLayer = makeCompound("default");
  defaultLayer.children.push_back(std::move(blockPalette));
  Tag palette = makeCompound("palette");
  palette.children.push_back(std::move(defaultLayer));

  // block_indices: LIST with one inner LIST of INT (the main layer), in X-major order.
  Tag mainLayer = makeList("", TagType::Int);
  const size_t volume = static_cast<size_t>(sx) * sy * sz;
  mainLayer.children.reserve(volume);
  for (size_t i = 0; i < volume && i < schematic.blocks.size(); ++i) {
    Tag element = bitematica::nbt::NbtWriter::intTag(
        "", static_cast<int64_t>(schematic.blocks[i]));
    mainLayer.children.push_back(std::move(element));
  }
  Tag blockIndices = makeList("block_indices", TagType::List);
  blockIndices.children.push_back(std::move(mainLayer));

  Tag root = makeCompound("structure");
  root.children.push_back(bitematica::nbt::NbtWriter::intTag("format_version", 1));
  root.children.push_back(std::move(size));
  root.children.push_back(std::move(blockIndices));
  root.children.push_back(std::move(palette));

  // Structure files are uncompressed NBT.
  const std::vector<uint8_t> encoded = bitematica::nbt::NbtWriter::writeFile(root, false);
  return std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size());
}

bool StructureWriter::write(const core::Schematic &schematic, const std::string &path,
                            std::string &error) {
  if (schematic.empty()) {
    error = "nothing is loaded to export";
    return false;
  }
  const std::string data = bytes(schematic);
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "could not open the export path";
    return false;
  }
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) {
    error = "could not write the export file";
    return false;
  }
  return true;
}

} // namespace bitematica::format
