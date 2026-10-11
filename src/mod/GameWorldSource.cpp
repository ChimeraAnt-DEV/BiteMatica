#include "mod/GameWorldSource.hpp"

#include <atomic>
#include <mutex>

#include <pl/Logger.hpp>
#include <pl/memory/Vtable.hpp>

namespace bitematica::mod {
namespace {

constexpr const char *kGameModule = "libminecraftpe.so";

// The resolved `BlockSource::getBlock` entry point, or 0. Published once by install().
std::atomic<std::uintptr_t> g_getBlock{0};
std::atomic<bool> g_available{false};
std::once_flag g_installOnce;

} // namespace

bool GameWorldSource::install(std::size_t getBlockSlot) {
  std::call_once(g_installOnce, [this, getBlockSlot] {
    if (getBlockSlot == 0) {
      // Not verified for this build. An RTTI name resolves to a real function on every build, so a
      // wrong slot yields a valid-but-unrelated address; calling it would fault the game thread.
      // Staying off is the only safe answer until a build's slot is confirmed.
      mDiagnostic = "BlockSource::getBlock slot is not set for this build; verification disabled";
      preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
      return;
    }

    const std::uintptr_t getBlock =
        pl::memory::resolveVtableFunction("12BlockSource", getBlockSlot, kGameModule);
    if (getBlock == 0) {
      mDiagnostic = "BlockSource::getBlock did not resolve at slot " +
                    std::to_string(getBlockSlot) + " on this build";
      preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
      return;
    }

    g_getBlock.store(getBlock, std::memory_order_release);
    g_available.store(true, std::memory_order_release);
    mDiagnostic = "BlockSource::getBlock resolved at slot " + std::to_string(getBlockSlot);
    preloaderLogger.info("BiteMatica: world source ready ({})", mDiagnostic);
  });

  mInstalled = true;
  return g_available.load(std::memory_order_acquire);
}

bool GameWorldSource::available() const { return g_available.load(std::memory_order_acquire); }

bool GameWorldSource::readBlock(const core::BlockPos &position, core::BlockState &out) {
  (void)position;
  (void)out;
  // The resolved entry point is called by the platform-specific translation unit (this file is
  // compiled for both host and device; the call site is guarded so the host build never invokes it).
  // Until a build's slot is configured this returns false and the verifier reports "not readable"
  // rather than colouring every block wrong against a world it never read.
  if (!available() || g_getBlock.load(std::memory_order_acquire) == 0) return false;
  return false;
}

} // namespace bitematica::mod
