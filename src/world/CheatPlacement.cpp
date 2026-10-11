#include "world/CheatPlacement.hpp"

#include <cctype>

namespace bitematica::world {
namespace {

std::string sanitize(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    } else if (!out.empty() && out.back() != '_') {
      out.push_back('_');
    }
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out.empty() ? "bitematica_structure" : out;
}

} // namespace

std::string CheatPlacement::loadCommand(const std::string &structureName,
                                        const core::BlockPos &origin) {
  return "structure load " + sanitize(structureName) + " " + std::to_string(origin.x) + " " +
         std::to_string(origin.y) + " " + std::to_string(origin.z);
}

std::string CheatPlacement::saveCommand(const std::string &structureName, const core::BlockPos &from,
                                        const core::BlockPos &to) {
  return "structure save " + sanitize(structureName) + " " + std::to_string(from.x) + " " +
         std::to_string(from.y) + " " + std::to_string(from.z) + " " + std::to_string(to.x) + " " +
         std::to_string(to.y) + " " + std::to_string(to.z);
}

std::string CheatPlacement::rotationArgument(Rotation rotation) {
  switch (rotation) {
    case Rotation::None: return "0_degrees";
    case Rotation::Cw90: return "90_degrees";
    case Rotation::Cw180: return "180_degrees";
    case Rotation::Cw270: return "270_degrees";
  }
  return "0_degrees";
}

std::string CheatPlacement::loadCommandForPlacement(const std::string &structureName,
                                                    const Placement &placement) {
  return "structure load " + sanitize(structureName) + " " + std::to_string(placement.origin.x) +
         " " + std::to_string(placement.origin.y + placement.yOffset) + " " +
         std::to_string(placement.origin.z) + " " + rotationArgument(placement.rotation);
}

std::string CheatPlacement::structureNameFor(const core::Schematic &schematic) {
  return sanitize(schematic.name);
}

} // namespace bitematica::world
