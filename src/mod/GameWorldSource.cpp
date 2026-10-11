#include "mod/GameWorldSource.hpp"

#include <mutex>

#include <pl/Logger.hpp>

namespace bitematica::mod {
namespace {

std::mutex g_readerMutex;
GameWorldSource::BlockReader g_reader;

} // namespace

void GameWorldSource::setBlockReader(BlockReader reader) {
  std::lock_guard<std::mutex> lock(g_readerMutex);
  g_reader = std::move(reader);
}

bool GameWorldSource::hasBlockReader() {
  std::lock_guard<std::mutex> lock(g_readerMutex);
  return static_cast<bool>(g_reader);
}

bool GameWorldSource::runProbe(const core::BlockPos &feetPosition,
                               const core::BlockPos &skyPosition) {
  BlockReader reader;
  {
    std::lock_guard<std::mutex> lock(g_readerMutex);
    reader = g_reader;
  }
  const ProbeResult result = probe(reader, feetPosition, skyPosition);
  if (result == ProbeResult::Passed) {
    mProbed = true;
    mConsecutiveErrors = 0;
    return true;
  }
  mSessionDisabled = true;
  switch (result) {
    case ProbeResult::FeetIsAir:
      mDiagnostic = "world source probe failed: the block under the player read as air "
                    "(slot likely wrong) - disabled for this session";
      break;
    case ProbeResult::SkyIsNotAir:
      mDiagnostic = "world source probe failed: 400 blocks up read as solid "
                    "(slot likely wrong) - disabled for this session";
      break;
    case ProbeResult::Unreadable:
    default:
      mDiagnostic = "world source probe failed: the reader returned nothing - disabled for this "
                    "session";
      break;
  }
  preloaderLogger.warn("BiteMatica: {}", mDiagnostic);
  return false;
}

void GameWorldSource::configure(int getBlockSlot, const std::string &configuredVersionTag,
                                const std::string &runningVersionTag) {
  mConfigured = true;
  mSlot = getBlockSlot;

  if (getBlockSlot <= 0) {
    mDiagnostic = "BlockSource::getBlock slot is not set for this build; verification disabled";
    preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
    return;
  }
  if (!hasBlockReader()) {
    // The slot is configured but the confirmed reader has not been installed. Without a reader with
    // a known signature there is nothing safe to call, so the source stays off until the human
    // confirms the slot and ABI for their build.
    mDiagnostic = "BlockSource::getBlock slot set but no confirmed reader installed; see "
                  "docs/FINDING_GETBLOCK.md";
    preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
    return;
  }
  mVersionGateOpen = versionGateOpen(configuredVersionTag, runningVersionTag);
  if (!mVersionGateOpen) {
    mDiagnostic = "world source not verified for this game version (" +
                  (runningVersionTag.empty() ? std::string("unknown") : runningVersionTag) +
                  "; verified for " +
                  (configuredVersionTag.empty() ? std::string("nothing") : configuredVersionTag) +
                  ")";
    preloaderLogger.info("BiteMatica: world source disabled ({})", mDiagnostic);
    return;
  }
  mDiagnostic = "ready (slot " + std::to_string(getBlockSlot) + ", version " + runningVersionTag +
                "); probe pending";
  preloaderLogger.info("BiteMatica: world source {}", mDiagnostic);
}

bool GameWorldSource::available() const {
  if (!mConfigured || mSlot <= 0 || mSessionDisabled) return false;
  if (!mVersionGateOpen) return false;
  return hasBlockReader();
}

bool GameWorldSource::readBlock(const core::BlockPos &position, core::BlockState &out) {
  if (!available()) return false;

  // The sanity probe must have passed first. The mod glue runs it once, with the player's feet and
  // a position 400 blocks above, before the first real read. Until then, calling the reader would be
  // exactly the unverified call this design avoids.
  if (!mProbed) return false;

  BlockReader reader;
  {
    std::lock_guard<std::mutex> lock(g_readerMutex);
    reader = g_reader;
  }
  if (!reader) return false;

  if (!reader(position, out)) {
    // The error budget (F9): a reader that fails repeatedly is disabled for the session rather than
    // retried in a tight loop.
    if (++mConsecutiveErrors >= kErrorBudget) {
      mSessionDisabled = true;
      mDiagnostic = "world source disabled: " + std::to_string(mConsecutiveErrors) +
                    " consecutive read failures";
      preloaderLogger.warn("BiteMatica: {}", mDiagnostic);
    }
    return false;
  }
  mConsecutiveErrors = 0;
  return true;
}

} // namespace bitematica::mod
