#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gles/Gles.hpp"
#include "mesh/SectionMesher.hpp"
#include "render/MathTypes.hpp"

namespace bitematica::gles {

/// How the ghost blocks are drawn.
enum class DrawMode : int {
  Translucent = 0, ///< filled, alpha-blended, depth-tested (hidden by terrain)
  Outlines = 1,    ///< wireframe boxes only
  Mismatch = 2,    ///< filled, coloured by verification state
};

/// A GPU buffer for one section mesh.
struct SectionBuffer {
  GLuint vbo{};
  GLuint ibo{};
  GLuint vao{};
  int indexCount{};
  /// World-space bounds of the section, for frustum/distance culling.
  render::Vec3 min{};
  render::Vec3 max{};
  bool uploaded{false};
};

/// Draws schematic section meshes into the game's frame.
///
/// This is the GLES half. Everything that can be wrong without a GPU (the mesher, the transforms,
/// the culling math) lives elsewhere and is host-tested; this class is deliberately thin: upload a
/// mesh, set uniforms, draw, and — critically — **save and restore every piece of GL state it
/// touches**. Corrupting the game's program/VAO/blend/depth/cull/texture bindings is the single
/// most damaging bug this mod could have, so the save/restore is explicit and exhaustive, and the
/// restore runs even when a draw is skipped.
class SchematicRenderer {
public:
  /// Compiles the shader program. Idempotent; safe to call from the render thread.
  bool initialize();

  /// True when the GLES surface resolved and the program linked.
  bool ready() const { return mReady; }

  /// Uploads or replaces a section's buffers. Must run with the GL context current.
  bool uploadSection(SectionBuffer &buffer, const mesh::SectionMesh &section);

  /// Releases a section's GPU resources.
  void releaseSection(SectionBuffer &buffer);

  /// Releases every buffer and the program.
  void shutdown();

  /// Draws the uploaded sections for this frame.
  ///
  /// The two coordinate spaces are deliberately separate and must not be swapped:
  ///
  ///  * `mvp` is the **world** -> clip matrix uploaded as `uMvp`. The vertex shader applies the
  ///    placement's rotation and translation itself, so `mvp` must NOT include them — doing so
  ///    transforms every vertex twice.
  ///  * `localFrustum` and `localCameraPosition` are in **placement-local** space and are used only
  ///    for culling, where section boxes are axis-aligned regardless of the placement's rotation.
  ///
  /// @param mvp the plain world->clip matrix (no placement transform)
  /// @param placementOrigin world position of the placement's minimum corner
  /// @param placementRotationDegrees the placement's yaw (0/90/180/270) applied about the origin
  /// @param mode translucent / outlines / mismatch
  /// @param alpha 0..1 overall alpha
  /// @param localCameraPosition the camera in placement-local space, for distance culling
  /// @param maxDistance distance cull radius in blocks (<= 0 disables it)
  /// @param localFrustum frustum in placement-local space, for frustum culling
  void draw(const render::Mat4 &mvp, const render::Vec3 &placementOrigin,
            float placementRotationDegrees, DrawMode mode, float alpha,
            const render::Vec3 &localCameraPosition, float maxDistance,
            const render::Frustum &localFrustum, const std::vector<SectionBuffer> &sections);

  /// The number of triangles drawn in the last `draw` call, for the perf HUD.
  int lastDrawnTriangles() const { return mLastTriangles; }
  /// The number of sections drawn in the last `draw` call.
  int lastDrawnSections() const { return mLastSections; }

private:
  /// Every piece of GL state this renderer touches, captured before a draw and restored after.
  ///
  /// The shader binds no textures, so the texture unit and binding are not touched at all — only
  /// GL_ACTIVE_TEXTURE is saved and restored, because it is global state the game may rely on.
  /// Stencil, scissor and colour mask are captured because the game may leave them enabled, which
  /// would otherwise make the ghost blocks invisible or clipped; both blend alpha factors and the
  /// blend equation are captured so a restore cannot leave the game blending differently.
  struct SavedState {
    GLint program{};
    GLint vao{};
    GLint arrayBuffer{};
    GLint elementBuffer{};
    GLint activeTexture{};
    GLboolean blend{};
    GLboolean depthTest{};
    GLboolean cullFace{};
    GLboolean depthMask{};
    GLint depthFunc{};
    GLint cullMode{};
    GLint frontFace{};
    GLint blendSrcRgb{};
    GLint blendDstRgb{};
    GLint blendSrcAlpha{};
    GLint blendDstAlpha{};
    GLint blendEquationRgb{};
    GLint blendEquationAlpha{};
    GLboolean stencilTest{};
    GLboolean scissorTest{};
    GLint colorMask[4]{};
  };

  void saveState(SavedState &state) const;
  void restoreState(const SavedState &state) const;

  bool mReady{false};
  GLuint mProgram{};
  GLint mUniformMvp{-1};
  GLint mUniformAlpha{-1};
  GLint mUniformPlacementOrigin{-1};
  GLint mUniformPlacementYawCos{-1};
  GLint mUniformPlacementYawSin{-1};
  int mLastTriangles{0};
  int mLastSections{0};
};

} // namespace bitematica::gles
