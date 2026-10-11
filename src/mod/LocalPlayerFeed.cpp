#include "mod/LocalPlayerFeed.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <mutex>
#include <numbers>

#include <pl/Logger.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Vtable.hpp>

#include "render/MathTypes.hpp"

namespace bitematica::mod {
namespace {

constexpr const char *kGameModule = "libminecraftpe.so";

// The local player's entity origin (a Vec3) and view rotation (yaw, pitch in degrees) on the
// Actor/LocalPlayer object. These offsets are the same ones the launcher's preloader uses, and they
// are shared by every build the feed targets (1.26.33 / 1.26.50 / 1.26.51), confirmed against the
// 1.26.50.4 and 1.26.60.28 binaries.
constexpr std::uintptr_t kActorPositionOffset = 0x230;
constexpr std::uintptr_t kActorRotationOffset = 0x238;

// ClientInstance's vtable slot whose call returns the local player, resolved by RTTI name so no
// per-build code address is baked in.
constexpr std::size_t kGetLocalPlayerSlot = 31;

// A snapshot is live for this long after the last game call; after teardown it goes stale and the
// readers stop reporting rather than handing back a stale position.
constexpr long long kSnapshotFreshMs = 2000;

// The exact signature of ClientInstance::getLocalPlayer: it takes the instance and returns a
// pointer. Unlike an unknown-ABI hook this one is known, so the detour may safely use the return.
using GetLocalPlayerFn = void *(*)(void *self);

GetLocalPlayerFn g_original = nullptr;
std::atomic<bool> g_hookInstalled{false};
std::atomic<bool> g_hookLive{false};

// Seqlock: the game thread writes, the UI thread reads, and a torn cross-field read must not be
// observable.
std::atomic<std::uint32_t> g_seq{0};
float g_position[3] = {0.0f, 0.0f, 0.0f};
float g_rotation[2] = {0.0f, 0.0f};
std::atomic<long long> g_stampMs{0};
std::mutex g_installMutex;

long long monotonicMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

bool finitePosition(const float p[3]) {
  // A garbage read shows up as NaN/inf, and an absurd magnitude is outside the world border, so both
  // are treated as "no data".
  constexpr float kWorldLimit = 3.0e7f;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(p[i]) || std::fabs(p[i]) > kWorldLimit) return false;
  }
  return true;
}

void publishSnapshot(const void *localPlayer) {
  const auto base = reinterpret_cast<std::uintptr_t>(localPlayer);
  const auto *position = reinterpret_cast<const float *>(base + kActorPositionOffset);
  const auto *rotation = reinterpret_cast<const float *>(base + kActorRotationOffset);

  float nextPosition[3] = {position[0], position[1], position[2]};
  float nextRotation[2] = {rotation[0], rotation[1]};
  if (!finitePosition(nextPosition)) return;
  if (!std::isfinite(nextRotation[0]) || !std::isfinite(nextRotation[1])) return;

  g_seq.fetch_add(1, std::memory_order_acq_rel); // odd: a write is in progress
  std::memcpy(g_position, nextPosition, sizeof(g_position));
  std::memcpy(g_rotation, nextRotation, sizeof(g_rotation));
  g_stampMs.store(monotonicMs(), std::memory_order_relaxed);
  g_seq.fetch_add(1, std::memory_order_release); // even: the write is complete
}

bool readSnapshot(float outPosition[3], float outRotation[2]) {
  std::uint32_t s1 = 0;
  std::uint32_t s2 = 0;
  float position[3];
  float rotation[2];
  long long stamp = 0;
  do {
    s1 = g_seq.load(std::memory_order_acquire);
    if ((s1 & 1U) != 0U) return false;
    std::memcpy(position, g_position, sizeof(position));
    std::memcpy(rotation, g_rotation, sizeof(rotation));
    stamp = g_stampMs.load(std::memory_order_relaxed);
    s2 = g_seq.load(std::memory_order_acquire);
  } while (s1 != s2);

  if (stamp == 0 || monotonicMs() - stamp > kSnapshotFreshMs) return false;
  std::memcpy(outPosition, position, sizeof(position));
  std::memcpy(outRotation, rotation, sizeof(rotation));
  return true;
}

/// The detour. The original is called first; then, if it returned a player, the snapshot is taken.
///
/// The dereference happens here, on the game thread, right after the game itself returned the
/// pointer — so a session ending can never leave the UI thread reading freed memory.
void *hookGetLocalPlayer(void *self) {
  void *localPlayer = g_original ? g_original(self) : nullptr;
  if (localPlayer != nullptr) {
    publishSnapshot(localPlayer);
    g_hookLive.store(true, std::memory_order_release);
  }
  return localPlayer;
}

} // namespace

std::uintptr_t LocalPlayerFeed::positionOffset() { return kActorPositionOffset; }
std::uintptr_t LocalPlayerFeed::rotationOffset() { return kActorRotationOffset; }

bool LocalPlayerFeed::install() {
  std::lock_guard<std::mutex> lock(g_installMutex);
  if (g_hookInstalled.load(std::memory_order_acquire)) {
    return g_hookLive.load(std::memory_order_acquire);
  }

  const std::uintptr_t slot =
      pl::memory::resolveVtableFunction("14ClientInstance", kGetLocalPlayerSlot, kGameModule);
  if (slot == 0) {
    preloaderLogger.warn(
        "BiteMatica: ClientInstance local-player slot unresolved; world-space camera unavailable");
    return false;
  }

  if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(slot),
                       reinterpret_cast<pl::memory::FuncPtr>(&hookGetLocalPlayer),
                       reinterpret_cast<pl::memory::FuncPtr *>(&g_original),
                       pl::memory::HookPriority::Normal, "ClientInstance::getLocalPlayer") != 0) {
    preloaderLogger.warn("BiteMatica: local-player hook install failed");
    return false;
  }

  g_hookInstalled.store(true, std::memory_order_release);
  preloaderLogger.info("BiteMatica: local-player feed hooked (ClientInstance slot {})",
                       kGetLocalPlayerSlot);
  return true;
}

bool LocalPlayerFeed::available() {
  float position[3];
  float rotation[2];
  return readSnapshot(position, rotation);
}

bool LocalPlayerFeed::readPosition(float out[3]) {
  float position[3];
  float rotation[2];
  if (!readSnapshot(position, rotation)) return false;
  out[0] = position[0];
  out[1] = position[1];
  out[2] = position[2];
  return true;
}

bool LocalPlayerFeed::readRotation(float out[2]) {
  float position[3];
  float rotation[2];
  if (!readSnapshot(position, rotation)) return false;
  out[0] = rotation[0];
  out[1] = rotation[1];
  return true;
}

const char *LocalPlayerFeed::diagnostic() {
  if (!g_hookInstalled.load(std::memory_order_acquire)) return "hook not installed";
  if (!g_hookLive.load(std::memory_order_acquire)) return "hook installed, no player yet";
  if (!available()) return "stale (not in a world)";
  return "live";
}

bool LocalPlayerFeed::buildLocalCamera(float fovDegrees, float aspectRatio, float nearPlane,
                                       float farPlane, render::Mat4 &outViewProjection,
                                       render::Vec3 &outEye, render::Vec3 &outForward,
                                       render::Mat4 &outView, render::Mat4 &outProjection) {
  float position[3];
  float rotation[2];
  if (!readSnapshot(position, rotation)) return false;

  // Bedrock yaw is measured clockwise from -Z, and pitch is positive looking down. Forward is
  // therefore (-sin(yaw)cos(pitch), -sin(pitch), -cos(yaw)cos(pitch)).
  const float yaw = rotation[0] * static_cast<float>(std::numbers::pi) / 180.0f;
  const float pitch = rotation[1] * static_cast<float>(std::numbers::pi) / 180.0f;
  const float cosPitch = std::cos(pitch);
  outForward = render::Vec3{-std::sin(yaw) * cosPitch, -std::sin(pitch),
                            -std::cos(yaw) * cosPitch};
  // The eye sits at the player's feet plus eye height, the same offset Bedrock uses.
  outEye = render::Vec3{position[0], position[1] + 1.62f, position[2]};

  outView = render::lookAt(outEye, outForward, render::Vec3{0.0f, 1.0f, 0.0f});
  outProjection = render::perspective(fovDegrees, aspectRatio, nearPlane, farPlane);
  outViewProjection = render::Mat4::multiply(outProjection, outView);
  return outViewProjection.isFinite();
}

} // namespace bitematica::mod
