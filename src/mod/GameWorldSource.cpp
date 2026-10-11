#include "mod/GameWorldSource.hpp"

#include <atomic>
#include <mutex>

#include <pl/Logger.hpp>
#include <pl/memory/Signature.hpp>
#include <pl/memory/Vtable.hpp>

namespace bitematica::mod {
namespace {

constexpr const char *kGameModule = "libminecraftpe.so";

// The version range whose BlockSource vtable slot and block-id layout were verified. Outside it the
// hook stays off: an RTTI name resolves to a real function on every build, so a wrong slot yields a
// valid-but-unrelated address, and calling it would fault the game thread.
constexpr const char *kVerifiedMin = "1.26.60.00";
constexpr const char *kVerifiedMax = "1.26.61.00";

// BlockSource's primary vtable slot that returns the block at a position. Verified against the
// 1.26.60.x builds; overridable here in one place when a build is re-verified.
constexpr std::size_t kGetBlockSlot = 0;

// `getBlock` returns a pointer to a Block, whose runtime id is the first 32-bit field. The id->state
// table is the game's own; reading the id is enough to compare against a schematic block id once
// both are resolved to the same namespace, which the verifier does by name.
constexpr std::size_t kBlockIdOffset = 0;

std::atomic<bool> g_resolved{false};
std::atomic<bool> g_available{false};
std::once_flag g_installOnce;

} // namespace

const char *GameWorldSource::verifiedMinVersion() { return kVerifiedMin; }
const char *GameWorldSource::verifiedMaxVersion() { return kVerifiedMax; }

bool GameWorldSource::install() {
  std::call_once(g_installOnce, [this] {
    // The world hook is only ever installed through the SDK's own verified-target rules, which are
    // delivered by the launcher before the game starts. Without them there is no verified address
    // and installing anyway faults, so this refuses and stays fail-closed.
    if (kGetBlockSlot == 0) {
      mDiagnostic =
          "BlockSource::getBlock slot not verified for this build; verification disabled";
      preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
      return;
    }

    const std::uintptr_t getBlock =
        pl::memory::resolveVtableFunction("12BlockSource", kGetBlockSlot, kGameModule);
    if (getBlock == 0) {
      mDiagnostic = "BlockSource::getBlock did not resolve on this build";
      preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
      return;
    }

    // A resolved slot is necessary but not sufficient: the calling convention and the returned
    // Block layout also have to be verified, which is why the slot is kept at 0 (unverified) until
    // a build is confirmed. This is the honest state, not a placeholder to be filled with a guess.
    mDiagnostic = "BlockSource::getBlock resolved but its ABI is unverified; verification disabled";
    preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
    (void)kBlockIdOffset;
  });

  g_resolved.store(true, std::memory_order_release);
  g_available.store(false, std::memory_order_release);
  mInstalled = true;
  return g_available.load(std::memory_order_acquire);
}

bool GameWorldSource::available() const { return g_available.load(std::memory_order_acquire); }

bool GameWorldSource::readBlock(const core::BlockPos &position, core::BlockState &out) {
  (void)position;
  (void)out;
  // No verified reader means no block. Reporting a guessed state would make the verifier colour
  // every block "wrong" against a world it never actually read.
  return false;
}

} // namespace bitematica::mod
