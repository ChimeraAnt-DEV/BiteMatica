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
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_waitingForEgl{false};
std::atomic<bool> g_live{false};
std::atomic<unsigned long long> g_frames{0};
std::mutex g_installMutex;

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
  // Every GLES game reaches one of these. ANGLE and the vendor drivers use different names, and the
  // game may reach EGL through a wrapper, so all the common ones are tried. `RTLD_NOLOAD` is
  // preferred so an already-mapped copy is used; a fresh open is the fallback for the case where the
  // library is present but not yet mapped.
  for (const char *name : {"libEGL.so", "libEGL_angle.so", "libEGL_adreno.so",
                           "libEGL_mali.so", "libEGL_emulation.so"}) {
    void *handle = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) handle = dlopen(name, RTLD_NOW);
    if (handle) {
      void *symbol = dlsym(handle, "eglSwapBuffers");
      if (symbol) return symbol;
      // Some drivers only export the damage variant; it is called once per frame too.
      symbol = dlsym(handle, "eglSwapBuffersWithDamageKHR");
      if (symbol) return symbol;
    }
  }
  return nullptr;
}

} // namespace

bool EglFrameSource::tryInstall() { return install(); }

bool EglFrameSource::install() {
  // Already live: nothing to do.
  if (g_hooked.load(std::memory_order_acquire)) return true;

  // A failed attempt is deliberately NOT latched: the mod enables before the game has necessarily
  // mapped a GLES library, so the first attempt can legitimately fail and a later one succeed. The
  // install path is cheap and only runs until it succeeds.
  void *target = findEglSwapBuffers();
  if (target == nullptr) {
    g_waitingForEgl.store(true, std::memory_order_release);
    return false;
  }

  std::lock_guard<std::mutex> lock(g_installMutex);
  if (g_hooked.load(std::memory_order_acquire)) return true;

  if (pl::memory::hook(target, reinterpret_cast<pl::memory::FuncPtr>(&hookSwapBuffers),
                       reinterpret_cast<pl::memory::FuncPtr *>(&g_originalSwapBuffers),
                       pl::memory::HookPriority::Normal, "eglSwapBuffers") != 0) {
    preloaderLogger.warn("BiteMatica: eglSwapBuffers hook install failed");
    return false;
  }

  g_hooked.store(true, std::memory_order_release);
  g_waitingForEgl.store(false, std::memory_order_release);
  preloaderLogger.info("BiteMatica: eglSwapBuffers hooked (version-independent render seam)");
  return true;
}

bool EglFrameSource::waitingForEgl() {
  return g_waitingForEgl.load(std::memory_order_acquire);
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
