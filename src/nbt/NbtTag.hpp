#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bitematica::nbt {

/// NBT tag types, matching the numeric ids used on disk.
enum class TagType : int {
  End = 0,
  Byte = 1,
  Short = 2,
  Int = 3,
  Long = 4,
  Float = 5,
  Double = 6,
  ByteArray = 7,
  String = 8,
  List = 9,
  Compound = 10,
  IntArray = 11,
  LongArray = 12,
};

/// A parsed NBT tag.
///
/// One struct covers every scalar and container type rather than a tagged union of
/// distinct classes: the readers only ever want "give me the int named X" or "give me
/// the compound named Y", so a flat shape with typed accessors is smaller to write and
/// keeps the accessor code at the call sites readable. Container tags (`List`,
/// `Compound`) store their elements in `children`; `Compound` children carry a `name`
/// and `List` children do not. Array tags store their values in `array`.
struct Tag {
  TagType type{TagType::End};
  std::string name;

  // Scalar payload. The active field depends on `type`.
  int64_t intValue{};
  double doubleValue{};
  std::string stringValue;
  std::vector<int64_t> array;   // ByteArray / IntArray / LongArray

  std::vector<Tag> children;    // List items / Compound entries

  bool isCompound() const { return type == TagType::Compound; }
  bool isList() const { return type == TagType::List; }
  bool isString() const { return type == TagType::String; }

  /// Finds a named child of a compound; nullptr when absent.
  const Tag *find(const std::string &key) const {
    for (const Tag &child : children) {
      if (child.name == key) return &child;
    }
    return nullptr;
  }

  /// Reads an integer-valued tag as a 64-bit int; `fallback` when absent or wrong type.
  int64_t asInt(int64_t fallback = 0) const {
    switch (type) {
      case TagType::Byte:
      case TagType::Short:
      case TagType::Int:
      case TagType::Long:
        return intValue;
      default:
        return fallback;
    }
  }

  double asDouble(double fallback = 0.0) const {
    switch (type) {
      case TagType::Float:
      case TagType::Double:
        return doubleValue;
      case TagType::Byte:
      case TagType::Short:
      case TagType::Int:
      case TagType::Long:
        return static_cast<double>(intValue);
      default:
        return fallback;
    }
  }

  std::string asString(const std::string &fallback = {}) const {
    return type == TagType::String ? stringValue : fallback;
  }

  int size() const { return static_cast<int>(children.size()); }
};

/// A parsed document: the root compound plus the decompression path used to get there.
struct Document {
  Tag root;
  std::string error;
  bool compressionGzip{};
  bool compressionZlib{};
  bool valid() const { return error.empty(); }
};

} // namespace bitematica::nbt
