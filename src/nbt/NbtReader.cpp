#include "nbt/NbtReader.hpp"

#include <zlib.h>

#include <cstring>

namespace bitematica::nbt {
namespace {

constexpr size_t kMaxInflatedBytes = 512u * 1024u * 1024u;

bool looksGzip(const std::vector<uint8_t> &b) {
  return b.size() >= 2 && b[0] == 0x1f && b[1] == 0x8b;
}

/// zlib streams begin with a byte whose low nibble is 8 (deflate) and the pair is
/// divisible by 31 (the zlib header checksum).
bool looksZlib(const std::vector<uint8_t> &b) {
  return b.size() >= 2 && (b[0] & 0x0f) == 0x08 &&
         ((static_cast<int>(b[0]) << 8) | b[1]) % 31 == 0;
}

} // namespace

bool NbtReader::fail(const std::string &message) {
  if (mError.empty()) mError = message;
  return false;
}

int64_t NbtReader::readBigEndian(size_t width) {
  if (mPos + width > mBytes.size()) {
    fail("unexpected end of NBT data");
    return 0;
  }
  uint64_t value = 0;
  for (size_t i = 0; i < width; ++i) {
    value = (value << 8) | mBytes[mPos + i];
  }
  mPos += width;
  // Sign-extend to 64-bit so Byte/Short/Int keep their negative values.
  if (width < 8) {
    const int shift = static_cast<int>((8 - width) * 8);
    value = static_cast<uint64_t>(static_cast<int64_t>(value << shift) >> shift);
  }
  return static_cast<int64_t>(value);
}

TagType NbtReader::readType() {
  if (mPos >= mBytes.size()) {
    fail("unexpected end of NBT data while reading tag type");
    return TagType::End;
  }
  return static_cast<TagType>(mBytes[mPos++]);
}

std::string NbtReader::readName() {
  const int64_t length = readBigEndian(2);
  if (length < 0 || mPos + static_cast<size_t>(length) > mBytes.size()) {
    fail("invalid NBT tag name length");
    return {};
  }
  std::string name(reinterpret_cast<const char *>(mBytes.data() + mPos),
                   static_cast<size_t>(length));
  mPos += static_cast<size_t>(length);
  return name;
}

Tag NbtReader::readTag(TagType type, bool named) {
  Tag tag;
  tag.type = type;
  if (named) tag.name = readName();
  if (!ok()) return tag;

  switch (type) {
    case TagType::End:
      break;
    case TagType::Byte:
      tag.intValue = readBigEndian(1);
      break;
    case TagType::Short:
      tag.intValue = readBigEndian(2);
      break;
    case TagType::Int:
      tag.intValue = readBigEndian(4);
      break;
    case TagType::Long:
      tag.intValue = readBigEndian(8);
      break;
    case TagType::Float: {
      const int64_t bits = readBigEndian(4);
      uint32_t asUint = static_cast<uint32_t>(bits);
      float f;
      std::memcpy(&f, &asUint, sizeof(f));
      tag.doubleValue = f;
      break;
    }
    case TagType::Double: {
      const int64_t bits = readBigEndian(8);
      uint64_t asUint = static_cast<uint64_t>(bits);
      double d;
      std::memcpy(&d, &asUint, sizeof(d));
      tag.doubleValue = d;
      break;
    }
    case TagType::ByteArray:
    case TagType::IntArray:
    case TagType::LongArray: {
      const size_t width =
          type == TagType::ByteArray ? 1 : (type == TagType::IntArray ? 4 : 8);
      const int64_t count = readBigEndian(4);
      if (count < 0) {
        fail("negative NBT array length");
        break;
      }
      if (mPos + static_cast<size_t>(count) * width > mBytes.size()) {
        fail("NBT array runs past end of data");
        break;
      }
      tag.array.reserve(static_cast<size_t>(count));
      for (int64_t i = 0; i < count; ++i) {
        tag.array.push_back(readBigEndian(width));
      }
      break;
    }
    case TagType::String: {
      const int64_t length = readBigEndian(2);
      if (length < 0 || mPos + static_cast<size_t>(length) > mBytes.size()) {
        fail("invalid NBT string length");
        break;
      }
      tag.stringValue.assign(
          reinterpret_cast<const char *>(mBytes.data() + mPos),
          static_cast<size_t>(length));
      mPos += static_cast<size_t>(length);
      break;
    }
    case TagType::List: {
      const TagType elementType = readType();
      const int64_t count = readBigEndian(4);
      if (!ok()) break;
      if (count < 0) {
        fail("negative NBT list length");
        break;
      }
      tag.children.reserve(static_cast<size_t>(count));
      for (int64_t i = 0; i < count; ++i) {
        // List elements share the list's declared type and are never named.
        Tag element = readTag(elementType, false);
        if (!ok()) break;
        tag.children.push_back(std::move(element));
      }
      break;
    }
    case TagType::Compound: {
      while (ok()) {
        const TagType childType = readType();
        if (!ok()) break;
        if (childType == TagType::End) break; // 0x00 terminates a compound.
        Tag child = readTag(childType, true);
        if (!ok()) break;
        tag.children.push_back(std::move(child));
      }
      break;
    }
  }
  return tag;
}

Tag NbtReader::readRoot() {
  const TagType type = readType();
  if (!ok() || type == TagType::End) {
    fail("NBT root tag is missing");
    return {};
  }
  return readTag(type, true);
}

Document NbtReader::readRaw(const std::vector<uint8_t> &bytes) {
  Document document;
  NbtReader reader(bytes);
  document.root = reader.readRoot();
  if (!reader.ok() || document.root.type == TagType::End) {
    document.error = reader.mError.empty() ? "empty NBT document" : reader.mError;
  }
  return document;
}

bool NbtReader::inflate(const std::vector<uint8_t> &bytes, std::vector<uint8_t> &out,
                        bool &wasGzip, bool &wasZlib, std::string &error) {
  const bool gzip = looksGzip(bytes);
  const bool zlib = !gzip && looksZlib(bytes);
  if (!gzip && !zlib) {
    out = bytes;
    return true;
  }
  wasGzip = gzip;
  wasZlib = zlib;

  z_stream stream{};
  // 15 + 32 tells zlib to auto-detect a gzip or zlib header.
  if (inflateInit2(&stream, 15 + 32) != Z_OK) {
    error = "could not start zlib inflate";
    return false;
  }
  stream.next_in = const_cast<Bytef *>(bytes.data());
  stream.avail_in = static_cast<uInt>(bytes.size());

  out.clear();
  std::vector<uint8_t> chunk(1 << 16);
  int status = Z_OK;
  do {
    stream.next_out = chunk.data();
    stream.avail_out = static_cast<uInt>(chunk.size());
    status = ::inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
      inflateEnd(&stream);
      error = "corrupt compressed NBT data";
      return false;
    }
    const size_t produced = chunk.size() - stream.avail_out;
    out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
    if (out.size() > kMaxInflatedBytes) {
      inflateEnd(&stream);
      error = "compressed NBT expands beyond the size limit";
      return false;
    }
    if (status == Z_BUF_ERROR && produced == 0) break;
  } while (status != Z_STREAM_END);
  inflateEnd(&stream);
  return true;
}

Document NbtReader::readFile(const std::vector<uint8_t> &bytes) {
  Document document;
  std::vector<uint8_t> inflated;
  std::string error;
  if (!inflate(bytes, inflated, document.compressionGzip, document.compressionZlib,
               error)) {
    document.error = error;
    return document;
  }
  Document decoded = readRaw(inflated);
  // readRaw starts a fresh document, so carry the detected wrapping back onto it.
  decoded.compressionGzip = document.compressionGzip;
  decoded.compressionZlib = document.compressionZlib;
  return decoded;
}

} // namespace bitematica::nbt
