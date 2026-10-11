#include "world/WorldSource.hpp"

#include <atomic>

namespace bitematica::world {
namespace {
std::atomic<WorldSource *> g_source{nullptr};
} // namespace

void setWorldSource(WorldSource *source) { g_source.store(source, std::memory_order_release); }

WorldSource *worldSource() { return g_source.load(std::memory_order_acquire); }

} // namespace bitematica::world
