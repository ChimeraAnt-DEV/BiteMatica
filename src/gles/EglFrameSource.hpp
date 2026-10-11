#pragma once

#include <functional>

namespace bitematica::gles {

/// A per-frame callback that runs on the render thread with the GL context current.
using FrameCallback = std::function<void()>;

/// Delivers a per-frame callback without depending on any game-build-specific hook.
///
/// The preloader's own render callback is gated to game builds whose render slot has been verified,
/// which is why world-space rendering did not work on 1.26-1.26.52. This instead hooks
/// **`eglSwapBuffers`**, an EGL entry point that *every* GLES game calls exactly once per frame, on
/// the render thread, with the context current — and whose signature is fixed by the EGL standard,
/// not by any game build. That makes the seam version-independent.
///
/// The detour calls the original first (so the frame the game just produced is on screen and the
/// context state is settled), then runs the registered callbacks. It never touches the EGL
/// arguments, so a wrong assumption cannot fault the render thread.
class EglFrameSource {
public:
  /// Installs the `eglSwapBuffers` hook. Idempotent. Returns true when the detour is live.
  ///
  /// May fail if the game has not loaded a GLES library yet (the mod enables early, the context
  /// comes later). `tryInstall` is the same call and exists so a caller can retry it; a failed
  /// install is not latched, so a later attempt can still succeed once EGL is mapped.
  static bool install();

  /// Retries installation. Returns true once the hook is live.
  static bool tryInstall();

  /// True when a hook attempt has been made and failed because no EGL library was found yet.
  static bool waitingForEgl();

  /// True once the hook has observed a frame.
  static bool live();

  /// Registers the callback invoked each frame. A single slot, replaced on re-register.
  static void setCallback(FrameCallback callback);

  /// The number of frames observed.
  static unsigned long long frameCount();

  /// Reads the current GL viewport, for the camera's aspect ratio. Cheap; render thread only.
  static void readViewport(int &width, int &height);
};

} // namespace bitematica::gles
