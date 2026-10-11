#include "gles/EglFrameSource.hpp"

#include <atomic>
#include <mutex>

#include <dlfcn.h>

#include "gles/Gles.hpp"
#include "pl/Logger.hpp"
#include "pl/memory/Hook.hpp"

namespace bitematica::gles {
namespace {

// The EGL entry point every GLES game calls once per frame. Its signature is fixed by the EGL
// standard: EGLBoolean eglSwapBuffers(EGLDisplay, EGLSurface).
using EglSwapBuffersFn = unsigned int (*)(void *display, void *surface);

EglSwapBuffersFn g_originalSwapBuffers = nullptr;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_live{false};
std::atomic<unsigned long long> g_frames{0};

std::mutex g_callbackMutex;
FrameCallback g_callback;

/// The detour. Runs on the render thread with the context current.
///
/// The original is called first so the game's frame is presented and its GL state is settled; then
/// the registered callback draws into the just-presented buffer. Nothing in the EGL arguments is
/// dereferenced, so an unexpected value cannot fault the render thread.
unsigned int hookSwapBuffers(void *display, void *surface) {
  const unsigned int result = g_originalSwapBuffers ? g_originalSwapBuffers(display, surface) : 1;
  g_live.store(true, std::memory_order_release);
  g_frames.fetch_add(1, std::memory_order_relaxed);

  FrameCallback callback;
  {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    callback = g_callback;
  }
  if (callback) {
    try {
      callback();
    } catch (...) {
      // A callback must never take the render thread down.
    }
  }
  return result;
}

void *findEglSwapBuffers() {
  // The game has libEGL loaded; prefer that handle, fall back to a fresh open.
  for (const char *name : {"libEGL.so", "libEGL_angle.so"}) {
    void *handle = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) handle = dlopen(name, RTLD_NOW);
    if (handle) {
      void *symbol = dlsym(handle, "eglSwapBuffers");
      if (symbol) return symbol;
    }
  }
  return nullptr;
}

} // namespace

bool EglFrameSource::install() {
  if (g_installed.exchange(true, std::memory_order_relaxed)) {
    return g_live.load(std::memory_order_relaxed);
  }

  void *target = findEglSwapBuffers();
  if (target == nullptr) {
    preloaderLogger.warn("BiteMatica: eglSwapBuffers not found; world-space rendering unavailable");
    return false;
  }

  if (pl::memory::hook(target, reinterpret_cast<pl::memory::FuncPtr>(&hookSwapBuffers),
                       reinterpret_cast<pl::memory::FuncPtr *>(&g_originalSwapBuffers),
                       pl::memory::HookPriority::Normal, "eglSwapBuffers") != 0) {
    preloaderLogger.warn("BiteMatica: eglSwapBuffers hook install failed");
    return false;
  }

  preloaderLogger.info("BiteMatica: eglSwapBuffers hooked (version-independent render seam)");
  return true;
}

bool EglFrameSource::live() { return g_live.load(std::memory_order_acquire); }

void EglFrameSource::setCallback(FrameCallback callback) {
  std::lock_guard<std::mutex> lock(g_callbackMutex);
  g_callback = std::move(callback);
}

unsigned long long EglFrameSource::frameCount() {
  return g_frames.load(std::memory_order_relaxed);
}

void EglFrameSource::readViewport(int &width, int &height) {
  width = 0;
  height = 0;
  const Functions &g = resolve();
  if (!g.GetIntegerv) return;
  using GetIntegervFn = void (*)(std::uint32_t, std::int32_t *);
  auto getIntegerv = reinterpret_cast<GetIntegervFn>(g.GetIntegerv);
  std::int32_t viewport[4] = {0, 0, 0, 0};
  getIntegerv(0x0BA2 /* GL_VIEWPORT */, viewport);
  width = viewport[2];
  height = viewport[3];
}

} // namespace bitematica::gles
