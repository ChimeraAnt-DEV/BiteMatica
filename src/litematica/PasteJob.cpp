#include "litematica/PasteJob.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

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

std::string PasteJob::mcfunctionText(const std::vector<std::string> &commands) {
  // A `.mcfunction` file: one command per line, no leading slash (a function is already a command
  // context). A trailing newline keeps text tools happy.
  std::string out;
  for (const std::string &command : commands) {
    out += command;
    out += '\n';
  }
  return out;
}

long long PasteJob::overwriteCount(const core::Schematic &schematic,
                                   const world::Placement &placement,
                                   const std::function<bool(const core::BlockPos &)> &isSolid,
                                   const PasteOptions &options) {
  long long count = 0;
  if (schematic.empty() || !isSolid) return 0;
  for (int y = 0; y < schematic.sizeY(); ++y) {
    for (int z = 0; z < schematic.sizeZ(); ++z) {
      for (int x = 0; x < schematic.sizeX(); ++x) {
        const core::BlockState &state = schematic.stateAt(x, y, z);
        // Air is only overwritten when `skipAir` is off (a true "replace everything" paste).
        if (state.isAir() && options.skipAir) continue;
        const core::BlockPos world = placement.toWorld(schematic, x, y, z);
        if (isSolid(world)) ++count;
      }
    }
  }
  return count;
}

bool PasteJob::nextSuggestedBlock(const core::Schematic &schematic,
                                  const world::Placement &placement,
                                  const std::vector<uint8_t> &statuses, int layerMin, int layerMax,
                                  core::BlockPos &outWorld, core::BlockState &outWanted) {
  if (schematic.empty() || statuses.size() != schematic.blocks.size()) return false;
  // Scan bottom-up so the lowest incomplete layer is suggested first, which is how a player builds.
  for (int y = layerMin; y <= layerMax && y < schematic.sizeY(); ++y) {
    if (y < 0) continue;
    for (int z = 0; z < schematic.sizeZ(); ++z) {
      for (int x = 0; x < schematic.sizeX(); ++x) {
        const size_t index = static_cast<size_t>(schematic.indexOf(x, y, z));
        // Missing (1) or WrongBlock (2) or WrongState (3) are all "place this next".
        const uint8_t status = statuses[index];
        if (status != 1 && status != 2 && status != 3) continue;
        const core::BlockState &wanted = schematic.stateAt(x, y, z);
        if (wanted.isAir()) continue;
        outWorld = placement.toWorld(schematic, x, y, z);
        outWanted = wanted;
        return true;
      }
    }
  }
  return false;
}

} // namespace bitematica::litematica
