#include "world/Selection.hpp"

#include <algorithm>

namespace bitematica::world {

bool Selection::capture(WorldSource &source, const std::string &name, long long maxBlocks,
                        core::Schematic &out, std::string &error) const {
  if (!complete()) {
    error = "the selection needs two corners";
    return false;
  }
  const long long volume = this->volume();
  if (volume > maxBlocks) {
    error = "the selection is too large to capture (" + std::to_string(volume) + " blocks)";
    return false;
  }
  if (!source.available()) {
    error = "the world is not readable (no block source installed on this build)";
    return false;
  }

  core::BlockPos min;
  core::BlockPos max;
  bounds(min, max);

  out = core::Schematic{};
  out.name = name;
  out.sourceFormat = "selection";
  out.bounds = {{0, 0, 0}, {max.x - min.x, max.y - min.y, max.z - min.z}};
  out.allocateBlocks();

  for (int y = min.y; y <= max.y; ++y) {
    for (int z = min.z; z <= max.z; ++z) {
      for (int x = min.x; x <= max.x; ++x) {
        core::BlockState state;
        if (!source.readBlock(core::BlockPos{x, y, z}, state)) {
          state = core::BlockState{};
        }
        const uint32_t paletteIndex = out.palette.intern(state);
        out.setBlock(x - min.x, y - min.y, z - min.z, paletteIndex);
      }
    }
  }
  return true;
}

} // namespace bitematica::world
