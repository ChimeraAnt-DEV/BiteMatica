#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nbt/NbtTag.hpp"

namespace bitematica::nbt {

/// Decodes NBT, the binary tag format every Minecraft save and schematic uses.
///
/// The reader accepts the three wrappings a schematic actually ships in: raw,
/// gzip, or zlib. Detection is done by magic bytes, not by the file name, because
/// a `.litematic` is gzip while a `.schem` is gzip too but a `.nbt` structure file
/// may be zlib or raw, and a renamed file must still open. Only the containers the
/// schematic formats use are decoded; a tag the reader does not understand is an
/// error rather than a silent skip, so a truncated file cannot half-load.
class NbtReader {
public:
  /// Parses a whole file image, inflating it first when needed.
  static Document readFile(const std::vector<uint8_t> &bytes);

  /// Parses a raw (already decompressed) buffer.
  static Document readRaw(const std::vector<uint8_t> &bytes);

  /// Inflates gzip/zlib data, leaving raw data untouched.
  static bool inflate(const std::vector<uint8_t> &bytes, std::vector<uint8_t> &out,
                      bool &wasGzip, bool &wasZlib, std::string &error);

private:
  NbtReader(const std::vector<uint8_t> &bytes) : mBytes(bytes) {}

  Tag readTag(TagType type, bool named);
  Tag readRoot();
  TagType readType();
  std::string readName();
  int64_t readBigEndian(size_t width);

  const std::vector<uint8_t> &mBytes;
  size_t mPos{};
  std::string mError;

  bool fail(const std::string &message);
  bool ok() const { return mError.empty(); }
};

} // namespace bitematica::nbt
