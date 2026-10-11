#include "render/GameCamera.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <mutex>
#include <string>
#include <vector>

namespace bitematica::render {
namespace {

constexpr const char *kGameModule = "libminecraftpe.so";

struct Region {
  std::uintptr_t start = 0;
  std::uintptr_t end = 0;
};

std::atomic<std::uintptr_t> g_viewAddress{0};
std::atomic<std::uintptr_t> g_projectionAddress{0};
std::atomic<int> g_viewportWidth{0};
std::atomic<int> g_viewportHeight{0};
std::once_flag g_scanOnce;

void readableWritableRegionsFor(const std::string &moduleName, std::vector<Region> &out) {
  std::FILE *maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return;
  char line[4096];
  while (std::fgets(line, sizeof(line), maps)) {
    if (std::strstr(line, moduleName.c_str()) == nullptr) continue;
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    char perms[5] = {};
    if (std::sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) != 3) continue;
    // The camera lives in writable data; scanning code ranges only adds constant-pool false hits.
    if (end <= start || perms[0] != 'r' || perms[1] != 'w') continue;
    out.push_back(Region{start, end});
  }
  std::fclose(maps);
}

bool finite(float v) { return std::isfinite(v); }

} // namespace

namespace detail {

bool validateProjection(const float *m) {
  // Column-major perspective: focal scales on the diagonal, -1 in m[11], near/far remap in m[10].
  if (!finite(m[0]) || !finite(m[5]) || !finite(m[10]) || !finite(m[11])) return false;
  if (!(m[0] > 0.4F && m[0] < 8.0F)) return false;
  if (!(m[5] > 0.4F && m[5] < 8.0F)) return false;
  if (!(m[10] < -0.3F && m[10] > -2.5F)) return false;
  if (std::fabs(m[11] + 1.0F) > 0.3F) return false;
  if (std::fabs(m[1]) > 0.08F || std::fabs(m[4]) > 0.08F) return false;
  return true;
}

bool validateView(const float *m) {
  // A rigid transform: bottom row (0,0,0,1), orthonormal basis in the upper-left 3x3.
  for (int i = 0; i < 16; ++i) {
    if (!finite(m[i])) return false;
  }
  if (std::fabs(m[3]) > 0.05F || std::fabs(m[7]) > 0.05F || std::fabs(m[11]) > 0.05F) return false;
  if (std::fabs(m[15] - 1.0F) > 0.05F) return false;
  auto length = [](float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); };
  const float lc = length(m[0], m[1], m[2]);
  const float lu = length(m[4], m[5], m[6]);
  const float lf = length(m[8], m[9], m[10]);
  if (lc < 0.5F || lc > 2.0F || lu < 0.5F || lu > 2.0F || lf < 0.5F || lf > 2.0F) return false;
  if (std::fabs(m[0] * m[4] + m[1] * m[5] + m[2] * m[6]) > 0.1F) return false;
  if (std::fabs(m[0] * m[8] + m[1] * m[9] + m[2] * m[10]) > 0.1F) return false;
  if (std::fabs(m[4] * m[8] + m[5] * m[9] + m[6] * m[10]) > 0.1F) return false;
  return true;
}

} // namespace detail

void GameCamera::resolveOnce() {
  std::call_once(g_scanOnce, [] {
    std::vector<Region> regions;
    readableWritableRegionsFor(kGameModule, regions);
    for (const Region &region : regions) {
      const std::size_t count = (region.end - region.start) / sizeof(float);
      if (count < 32) continue;
      const float *base = reinterpret_cast<const float *>(region.start);
      for (std::size_t i = 16; i + 16 <= count; ++i) {
        const float *projection = base + i;
        if (!detail::validateProjection(projection)) continue;
        const float *view = base + i - 16;
        if (!detail::validateView(view)) continue;
        g_viewAddress.store(region.start + (i - 16) * sizeof(float), std::memory_order_release);
        g_projectionAddress.store(region.start + i * sizeof(float), std::memory_order_release);
        return;
      }
    }
  });
}

bool GameCamera::resolved() {
  return g_viewAddress.load(std::memory_order_acquire) != 0 &&
         g_projectionAddress.load(std::memory_order_acquire) != 0;
}

void GameCamera::setViewport(int width, int height) {
  g_viewportWidth.store(width, std::memory_order_relaxed);
  g_viewportHeight.store(height, std::memory_order_relaxed);
}

CameraFrame GameCamera::read() {
  CameraFrame frame;
  const std::uintptr_t viewAddr = g_viewAddress.load(std::memory_order_acquire);
  const std::uintptr_t projAddr = g_projectionAddress.load(std::memory_order_acquire);
  if (viewAddr == 0 || projAddr == 0) return frame;

  const float *view = reinterpret_cast<const float *>(viewAddr);
  const float *projection = reinterpret_cast<const float *>(projAddr);
  // Re-validate every frame: the address could be repurposed if the renderer tears down, and a
  // stale read must never become a wrong matrix.
  if (!detail::validateView(view) || !detail::validateProjection(projection)) return frame;

  std::memcpy(frame.view.m, view, sizeof(float) * 16);
  std::memcpy(frame.projection.m, projection, sizeof(float) * 16);
  frame.viewProjection = Mat4::multiply(frame.projection, frame.view);

  // The eye is the inverse translation of the view matrix: -R^T * t.
  frame.position = Vec3{
      -(frame.view.m[0] * frame.view.m[12] + frame.view.m[1] * frame.view.m[13] +
        frame.view.m[2] * frame.view.m[14]),
      -(frame.view.m[4] * frame.view.m[12] + frame.view.m[5] * frame.view.m[13] +
        frame.view.m[6] * frame.view.m[14]),
      -(frame.view.m[8] * frame.view.m[12] + frame.view.m[9] * frame.view.m[13] +
        frame.view.m[10] * frame.view.m[14]),
  };
  // The forward direction is the negated third row of the rotation.
  frame.forward = Vec3{-frame.view.m[2], -frame.view.m[6], -frame.view.m[10]};

  frame.viewportWidth = g_viewportWidth.load(std::memory_order_relaxed);
  frame.viewportHeight = g_viewportHeight.load(std::memory_order_relaxed);
  frame.valid = frame.viewProjection.isFinite();
  return frame;
}

} // namespace bitematica::render
