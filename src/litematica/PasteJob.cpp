#include "litematica/PasteJob.hpp"

#include <algorithm>
#include <cctype>

namespace bitematica::litematica {

std::string PasteJob::blockIdFor(const core::BlockState &state) {
  std::string id = state.name;
  if (state.states.empty()) return id;
  id += "[";
  for (size_t i = 0; i < state.states.size(); ++i) {
    if (i) id += ",";
    id += "\"" + state.states[i].first + "\"=";
    // Numeric values are unquoted in Bedrock's block-state syntax; strings are quoted.
    const std::string &value = state.states[i].second;
    const bool numeric = !value.empty() &&
                         std::all_of(value.begin(), value.end(), [](unsigned char c) {
                           return std::isdigit(c) || c == '-' || c == '.';
                         });
    id += numeric ? value : ("\"" + value + "\"");
  }
  id += "]";
  return id;
}

std::string PasteJob::setBlockCommand(const core::BlockPos &position,
                                      const core::BlockState &state) {
  return "setblock " + std::to_string(position.x) + " " + std::to_string(position.y) + " " +
         std::to_string(position.z) + " " + blockIdFor(state) + " replace";
}

std::string PasteJob::fillCommand(const core::BlockPos &from, const core::BlockPos &to,
                                  const core::BlockState &state) {
  return "fill " + std::to_string(from.x) + " " + std::to_string(from.y) + " " +
         std::to_string(from.z) + " " + std::to_string(to.x) + " " + std::to_string(to.y) + " " +
         std::to_string(to.z) + " " + blockIdFor(state) + " replace";
}

std::vector<std::string> PasteJob::build(const core::Schematic &schematic,
                                         const world::Placement &placement,
                                         const PasteOptions &options) {
  std::vector<std::string> commands;
  if (schematic.empty()) return commands;

  const int sizeX = schematic.sizeX();
  const int sizeY = schematic.sizeY();
  const int sizeZ = schematic.sizeZ();

  // Bottom-up so a build that depends on support does not fall.
  for (int y = 0; y < sizeY && static_cast<int>(commands.size()) < options.maxCommands; ++y) {
    for (int z = 0; z < sizeZ && static_cast<int>(commands.size()) < options.maxCommands; ++z) {
      int x = 0;
      while (x < sizeX && static_cast<int>(commands.size()) < options.maxCommands) {
        const core::BlockState &state = schematic.stateAt(x, y, z);
        if (state.isAir()) {
          if (options.skipAir) {
            ++x;
            continue;
          }
        }

        const std::string id = blockIdFor(state);
        // Extend the run along X while the block id matches.
        int runEnd = x;
        if (options.useFill) {
          while (runEnd + 1 < sizeX && blockIdFor(schematic.stateAt(runEnd + 1, y, z)) == id) {
            ++runEnd;
          }
        }

        const core::BlockPos from = placement.toWorld(schematic, x, y, z);
        const core::BlockPos to = placement.toWorld(schematic, runEnd, y, z);
        const long long volume =
            static_cast<long long>(runEnd - x + 1) * 1 * 1; // a 1-high X run
        if (options.useFill && runEnd > x && volume <= options.maxFillVolume) {
          commands.push_back(fillCommand(from, to, state));
        } else if (runEnd > x) {
          // Too large to fill in one command: emit per block.
          for (int bx = x; bx <= runEnd; ++bx) {
            commands.push_back(
                setBlockCommand(placement.toWorld(schematic, bx, y, z), state));
            if (static_cast<int>(commands.size()) >= options.maxCommands) break;
          }
        } else {
          commands.push_back(setBlockCommand(from, state));
        }
        x = runEnd + 1;
      }
    }
  }
  return commands;
}

} // namespace bitematica::litematica
