// A host benchmark for the mesher and the verifier (K7).
//
// Not a test: it prints timings so a change that makes meshing or verification much slower is
// visible before it reaches a device. Run it with `./build/host/bitematica_bench`.

#include <chrono>
#include <cstdio>
#include <vector>

#include "core/Schematic.hpp"
#include "mesh/SectionMesher.hpp"
#include "world/Placement.hpp"
#include "world/Verifier.hpp"
#include "world/WorldSource.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double millis(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

/// A schematic of `n` cubed blocks, filled with a checkerboard so the mesher cannot merge much.
bitematica::core::Schematic makeSchematic(int n) {
  bitematica::core::Schematic s;
  s.palette.intern(bitematica::core::BlockState{});
  bitematica::core::BlockState stone;
  stone.name = "minecraft:stone";
  bitematica::core::BlockState dirt;
  dirt.name = "minecraft:dirt";
  const uint32_t stoneIndex = s.palette.intern(stone);
  const uint32_t dirtIndex = s.palette.intern(dirt);
  s.bounds.min = {0, 0, 0};
  s.bounds.max = {n - 1, n - 1, n - 1};
  s.blocks.resize(static_cast<size_t>(s.volume()));
  for (int y = 0; y < n; ++y) {
    for (int z = 0; z < n; ++z) {
      for (int x = 0; x < n; ++x) {
        s.blocks[static_cast<size_t>(s.indexOf(x, y, z))] = ((x + y + z) & 1) ? stoneIndex : dirtIndex;
      }
    }
  }
  return s;
}

class CountingWorld : public bitematica::world::WorldSource {
public:
  bool readBlock(const bitematica::core::BlockPos &, bitematica::core::BlockState &out) override {
    out.name = "minecraft:stone";
    return true;
  }
  bool available() const override { return true; }
};

} // namespace

int main() {
  std::printf("BiteMatica benchmarks\n");
  std::printf("=====================\n");

  for (int n : {16, 32, 48}) {
    const auto schematic = makeSchematic(n);
    bitematica::world::Placement placement;

    const auto meshStart = Clock::now();
    const auto mesh = bitematica::mesh::SectionMesher::build(schematic, placement, 0, n - 1);
    const auto meshEnd = Clock::now();
    long long triangles = 0;
    for (const auto &section : mesh.sections) triangles += static_cast<long long>(section.triangles.size());
    std::printf("mesher  %3d^3 (%8lld blocks): %8.2f ms, %zu sections, %lld triangles\n", n,
                schematic.volume(), millis(meshStart, meshEnd), mesh.sections.size(), triangles);

    CountingWorld world;
    const auto verifyStart = Clock::now();
    const auto result =
        bitematica::world::Verifier::verify(schematic, placement, world, 0, n - 1);
    const auto verifyEnd = Clock::now();
    std::printf("verifier %3d^3: %8.2f ms, %lld correct\n", n, millis(verifyStart, verifyEnd),
                result.correct);
  }

  std::printf("=====================\n");
  return 0;
}
