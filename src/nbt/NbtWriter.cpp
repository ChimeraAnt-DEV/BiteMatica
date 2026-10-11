#include "nbt/NbtWriter.hpp"

#include <zlib.h>

#include <cstring>

namespace bitematica::nbt {

void NbtWriter::writeBigEndian(std::vector<uint8_t> &out, int64_t value, size_t width) {
  const uint64_t bits = static_cast<uint64_t>(value);
  for (size_t i = 0; i < width; ++i) {
    const int shift = static_cast<int>((width - 1 - i) * 8);
    out.push_back(static_cast<uint8_t>((bits >> shift) & 0xff));
  }
}

void NbtWriter::writeName(std::vector<uint8_t> &out, const std::string &name) {
  writeBigEndian(out, static_cast<int64_t>(name.size()), 2);
  out.insert(out.end(), name.begin(), name.end());
}

TagType NbtWriter::listElementType(const Tag &list) {
  // A list's declared element type is the type of its first child; an empty list is
  // written as an End-typed list, which every reader accepts as empty.
  if (list.children.empty()) return TagType::End;
  return list.children.front().type;
}

void NbtWriter::writeTag(std::vector<uint8_t> &out, const Tag &tag, bool named) {
  out.push_back(static_cast<uint8_t>(tag.type));
  if (named) writeName(out, tag.name);
  writePayload(out, tag);
}

/// Writes a tag's payload with no type byte and no name.
///
/// A LIST carries its element type once in its own header, and list entries are never
/// named, so each element is exactly its payload. Emitting a type byte per element is
/// the classic NBT list bug: the list still reads back (the header's element type is
/// authoritative) but every tag after it is misaligned.
void NbtWriter::writePayload(std::vector<uint8_t> &out, const Tag &tag) {
  switch (tag.type) {
    case TagType::End:
      break;
    case TagType::Byte:
      writeBigEndian(out, tag.intValue, 1);
      break;
    case TagType::Short:
      writeBigEndian(out, tag.intValue, 2);
      break;
    case TagType::Int:
      writeBigEndian(out, tag.intValue, 4);
      break;
    case TagType::Long:
      writeBigEndian(out, tag.intValue, 8);
      break;
    case TagType::Float: {
      const float f = static_cast<float>(tag.doubleValue);
      uint32_t bits;
      std::memcpy(&bits, &f, sizeof(bits));
      writeBigEndian(out, static_cast<int64_t>(bits), 4);
      break;
    }
    case TagType::Double: {
      const double d = tag.doubleValue;
      uint64_t bits;
      std::memcpy(&bits, &d, sizeof(bits));
      writeBigEndian(out, static_cast<int64_t>(bits), 8);
      break;
    }
    case TagType::ByteArray:
    case TagType::IntArray:
    case TagType::LongArray: {
      const size_t width =
          tag.type == TagType::ByteArray ? 1 : (tag.type == TagType::IntArray ? 4 : 8);
      writeBigEndian(out, static_cast<int64_t>(tag.array.size()), 4);
      for (int64_t value : tag.array) writeBigEndian(out, value, width);
      break;
    }
    case TagType::String:
      writeName(out, tag.stringValue);
      break;
    case TagType::List: {
      out.push_back(static_cast<uint8_t>(listElementType(tag)));
      writeBigEndian(out, static_cast<int64_t>(tag.children.size()), 4);
      for (const Tag &child : tag.children) writePayload(out, child);
      break;
    }
    case TagType::Compound:
      for (const Tag &child : tag.children) writeTag(out, child, true);
      out.push_back(static_cast<uint8_t>(TagType::End));
      break;
  }
}

std::vector<uint8_t> NbtWriter::writeRaw(const Tag &root) {
  std::vector<uint8_t> out;
  writeTag(out, root, true);
  return out;
}

std::vector<uint8_t> NbtWriter::writeFile(const Tag &root, bool compressed) {
  const std::vector<uint8_t> raw = writeRaw(root);
  if (!compressed) return raw;

  z_stream stream{};
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return raw;
  }
  std::vector<uint8_t> out;
  out.resize(deflateBound(&stream, static_cast<uLong>(raw.size())));
  stream.next_in = const_cast<Bytef *>(raw.data());
  stream.avail_in = static_cast<uInt>(raw.size());
  stream.next_out = out.data();
  stream.avail_out = static_cast<uInt>(out.size());
  const int status = ::deflate(&stream, Z_FINISH);
  if (status == Z_STREAM_END) {
    out.resize(stream.total_out);
  } else {
    out = raw;
  }
  deflateEnd(&stream);
  return out;
}

Tag NbtWriter::compound(const std::string &name) {
  Tag tag;
  tag.type = TagType::Compound;
  tag.name = name;
  return tag;
}

Tag NbtWriter::list(const std::string &name, TagType elementType) {
  Tag tag;
  tag.type = TagType::List;
  tag.name = name;
  // The element type is implied by the children on write; recorded for completeness.
  (void)elementType;
  return tag;
}

Tag NbtWriter::string(const std::string &name, const std::string &value) {
  Tag tag;
  tag.type = TagType::String;
  tag.name = name;
  tag.stringValue = value;
  return tag;
}

Tag NbtWriter::intTag(const std::string &name, int64_t value) {
  Tag tag;
  tag.type = TagType::Int;
  tag.name = name;
  tag.intValue = value;
  return tag;
}

Tag NbtWriter::byteTag(const std::string &name, int value) {
  Tag tag;
  tag.type = TagType::Byte;
  tag.name = name;
  tag.intValue = value;
  return tag;
}

Tag NbtWriter::floatTag(const std::string &name, double value) {
  Tag tag;
  tag.type = TagType::Float;
  tag.name = name;
  tag.doubleValue = value;
  return tag;
}

} // namespace bitematica::nbt
