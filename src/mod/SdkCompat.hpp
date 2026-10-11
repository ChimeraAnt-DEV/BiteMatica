#pragma once

/**
 * @file SdkCompat.hpp
 * @brief Makes the mod load on preloader builds that predate the render API, and fail closed there.
 *
 * The world-space renderer needs `pl/Render.hpp`, which was added to the preloader after the
 * commit GlowberryClient currently pins. A mod whose `.so` has an *undefined, non-weak* symbol that
 * the running preloader does not export fails to load at all: `dlopen` cannot resolve it, the
 * launcher reports "Built for a different game build (missing symbol)", and nothing runs.
 *
 * The fix is to declare the render API's entry points **weak**. A weak undefined symbol does not
 * fail `dlopen`; it resolves to null when the preloader lacks it. The mod then checks the pointers
 * at runtime. When the render API is absent the mod **draws no schematic at all** and shows a named
 * status message telling the user to update the launcher — it does not fall back to a screen-space
 * overlay, because an overlay is not world-space rendering and would be a lie about what the mod is.
 *
 * The weak declarations must come *after* the real header is included so they attach the weak
 * attribute to the existing declarations rather than conflicting with them.
 */

#include <pl/Render.hpp>

namespace pl::render {

// Re-declared weak. On a preloader with the render API these resolve normally; on one without, they
// are null and `SdkRender::available()` is false.
__attribute__((weak)) bool RegisterRenderCallback(RenderCallback callback);
__attribute__((weak)) std::size_t UnregisterRenderCallbacks(bool all);
__attribute__((weak)) bool ComputeViewProjection(float fovDegrees, float aspectRatio,
                                                 float nearPlane, float farPlane, Mat4 &out);
__attribute__((weak)) bool MakePerspective(float fovDegrees, float aspectRatio, float nearPlane,
                                           float farPlane, Mat4 &out);
__attribute__((weak)) bool MakeView(const Vec3 &eye, const Vec3 &forward, const Vec3 &up,
                                    Mat4 &out);
__attribute__((weak)) bool ReadLocalPlayerPosition(float out[3]);
__attribute__((weak)) bool ReadLocalPlayerRotation(float out[2]);

} // namespace pl::render

namespace bitematica::mod {

/// Runtime capability probe for the preloader's render API.
///
/// Every method checks the weak pointer before calling it, so a preloader without the render API
/// simply reports "unavailable". The caller then draws nothing and says so on screen: there is no
/// schematic fallback. This is what lets one `.AntEgg` load on both preloader generations while
/// still only ever drawing the schematic in world space.
class SdkRender {
public:
  /// True when the running preloader exports the per-frame render callback API.
  static bool available() {
    return pl::render::RegisterRenderCallback != nullptr &&
           pl::render::UnregisterRenderCallbacks != nullptr &&
           pl::render::ComputeViewProjection != nullptr;
  }

  /// True when the preloader can supply the local player's position (older API, usually present).
  static bool hasLocalPlayer() { return pl::render::ReadLocalPlayerPosition != nullptr; }

  static bool registerCallback(pl::render::RenderCallback callback) {
    if (pl::render::RegisterRenderCallback == nullptr) return false;
    return pl::render::RegisterRenderCallback(std::move(callback));
  }

  static std::size_t unregisterCallbacks(bool all) {
    if (pl::render::UnregisterRenderCallbacks == nullptr) return 0;
    return pl::render::UnregisterRenderCallbacks(all);
  }

  static bool computeViewProjection(float fovDegrees, float aspectRatio, float nearPlane,
                                    float farPlane, pl::render::Mat4 &out) {
    if (pl::render::ComputeViewProjection == nullptr) return false;
    return pl::render::ComputeViewProjection(fovDegrees, aspectRatio, nearPlane, farPlane, out);
  }

  static bool readLocalPlayerPosition(float out[3]) {
    if (pl::render::ReadLocalPlayerPosition == nullptr) return false;
    return pl::render::ReadLocalPlayerPosition(out);
  }
};

} // namespace bitematica::mod
