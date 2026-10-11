#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "nbt/NbtTag.hpp"

namespace bitematica::nbt {

/// Encodes NBT back to bytes, always gzip-wrapped.
///
/// Used for the one place BiteMatica produces NBT rather than consuming it: writing
/// a structure-file (`.mcstructure`) from an in-memory schematic so a build can be
/// shared with other Bedrock tools. Big-endian, matching the reader, so the round
/// trip is symmetric.
class NbtWriter {
public:
  /// Serializes a root compound. `compressed` controls gzip wrapping.
  static std::vector<uint8_t> writeFile(const Tag &root, bool compressed = true);

  /// Serializes a raw (uncompressed) NBT document.
  static std::vector<uint8_t> writeRaw(const Tag &root);

  /// Convenience builders so callers do not hand-roll tag construction.
  static Tag compound(const std::string &name = {});
  static Tag list(const std::string &name, TagType elementType);
  static Tag string(const std::string &name, const std::string &value);
  static Tag intTag(const std::string &name, int64_t value);
  static Tag byteTag(const std::string &name, int value);
  static Tag floatTag(const std::string &name, double value);

private:
  static void writeTag(std::vector<uint8_t> &out, const Tag &tag, bool named);
  static void writePayload(std::vector<uint8_t> &out, const Tag &tag);
  static void writeBigEndian(std::vector<uint8_t> &out, int64_t value, size_t width);
  static void writeName(std::vector<uint8_t> &out, const std::string &name);
  static TagType listElementType(const Tag &list);
};

} // namespace bitematica::nbt
