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

/// Options for one frame's draw. Grouped so a new option does not keep widening `draw`'s signature.
struct DrawOptions {
  DrawMode mode{DrawMode::Translucent};
  /// Overall alpha 0..1 for the filled pass.
  float alpha{0.6f};
  /// Distance cull radius in blocks (<= 0 disables).
  float maxDistance{256.0f};
  /// Polygon offset factor/units for the filled pass, to stop z-fighting with real block faces.
  /// Negative values pull the ghost toward the camera. Configurable.
  float polygonOffsetFactor{-1.0f};
  float polygonOffsetUnits{-1.0f};
  /// Draw block-edge outlines after the filled pass (Phase B8).
  bool outlines{false};
  /// Outline line width in pixels (clamped by the driver).
  float outlineWidth{1.5f};
};

/// A GPU buffer for one section mesh.
struct SectionBuffer {
  GLuint vbo{};
  GLuint ibo{};
  GLuint vao{};
  int indexCount{};
  /// Placement-local bounds of the section, for frustum/distance culling.
  render::Vec3 min{};
  render::Vec3 max{};
  bool uploaded{false};
  /// True when the index list is a line-segment list (drawn with GL_LINES), not triangles.
  bool linePrimitive{false};
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

  /// Uploads the outline (block-edge) buffers for a section, if the outline pass is enabled.
  bool uploadOutlines(SectionBuffer &buffer, const std::vector<mesh::MeshVertex> &lineVertices,
                      const std::vector<mesh::MeshTriangle> &lineSegments, const render::Vec3 &min,
                      const render::Vec3 &max);

  /// Releases a section's GPU resources.
  void releaseSection(SectionBuffer &buffer);

  /// Releases every buffer and the program.
  void shutdown();

  /// True when the running GLES backend is usable for world-space rendering (GLES 3.0+, and the
  /// entry points resolved). False on a non-GL backend. Safe to call after `initialize`.
  bool backendUsable() const { return mBackendUsable; }

  /// A short description of the backend for the status line ("OpenGL ES 3.2 ..." or a failure).
  const char *backendDescription() const { return mBackendDescription; }

  /// Draws the uploaded sections for this frame.
  ///
  /// The coordinate spaces are deliberately separate and must not be swapped:
  ///
  ///  * `mvp` is the **camera-relative** clip matrix `projection * rotation(view)`, uploaded as
  ///    `uMvp`. It contains no placement transform (the vertex shader applies the yaw) and no world
  ///    translation (the placement origin passed in is already relative to the camera).
  ///  * `localFrustum` and `localCameraPosition` are in **placement-local** space and are used only
  ///    for culling, where section boxes are axis-aligned regardless of the placement's rotation.
  ///
  /// @param mvp the camera-relative clip matrix (no placement transform, no world translation)
  /// @param cameraRelativeOrigin the placement origin measured from the camera (already subtracted in
  ///        double precision on the CPU), so the shader never adds a large world offset
  /// @param placementRotationDegrees the placement's yaw (0/90/180/270) applied about the origin
  /// @param options mode, alpha, culling, polygon offset and outline settings
  /// @param localCameraPosition the camera in placement-local space, for distance culling
  /// @param localFrustum frustum in placement-local space, for frustum culling
  /// @param sections the filled section meshes
  /// @param outlineSections the outline section buffers (may be empty)
  void draw(const render::Mat4 &mvp, const render::Vec3 &cameraRelativeOrigin,
            float placementRotationDegrees, const DrawOptions &options,
            const render::Vec3 &localCameraPosition, const render::Frustum &localFrustum,
            const std::vector<SectionBuffer> &sections,
            const std::vector<SectionBuffer> &outlineSections);

  /// Draws the outline buffers alone (the outline-only render mode). Same transform rules as `draw`.
  void drawOutlines(const render::Mat4 &mvp, const render::Vec3 &cameraRelativeOrigin,
                    float placementRotationDegrees, const DrawOptions &options,
                    const render::Vec3 &localCameraPosition, const render::Frustum &localFrustum,
                    const std::vector<SectionBuffer> &outlineSections);

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

  /// Binds the shader's per-vertex attributes for a VAO. Shared by the fill and line programs, which
  /// use the same `MeshVertex` layout.
  void bindVertexLayout() const;

  /// Sets the uniforms common to the fill and line programs (mvp, placement origin, yaw).
  void setPlacementUniforms(GLuint program, const render::Mat4 &mvp,
                            const render::Vec3 &cameraRelativeOrigin,
                            float placementRotationDegrees) const;

  void saveState(SavedState &state) const;
  void restoreState(const SavedState &state) const;

  /// Probes GL_VERSION / GL_RENDERER and records whether the backend is usable (GLES 3.0+).
  void probeBackend();

  bool mReady{false};
  GLuint mProgram{};
  GLint mUniformMvp{-1};
  GLint mUniformAlpha{-1};
  GLint mUniformPlacementOrigin{-1};
  GLint mUniformPlacementYawCos{-1};
  GLint mUniformPlacementYawSin{-1};

  /// A second, tiny program for the outline pass: the same vertex shader, a flat-colour fragment
  /// shader. Kept separate so the fill program's shading is untouched by line drawing.
  GLuint mLineProgram{};
  GLint mLineUniformMvp{-1};
  GLint mLineUniformAlpha{-1};
  GLint mLineUniformPlacementOrigin{-1};
  GLint mLineUniformPlacementYawCos{-1};
  GLint mLineUniformPlacementYawSin{-1};

  bool mBackendUsable{false};
  char mBackendDescription[96] = "backend not probed";

  int mLastTriangles{0};
  int mLastSections{0};
};

} // namespace bitematica::gles
