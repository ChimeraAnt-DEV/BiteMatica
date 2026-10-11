#include "gles/SchematicRenderer.hpp"

#include <cmath>
#include <cstring>

namespace bitematica::gles {
namespace {

// Vertex shader: transforms a section-relative vertex into camera-relative space by applying the
// placement's yaw and the placement origin *relative to the camera*, then multiplies by the
// camera-relative clip matrix. `uMvp` is `projection * rotation(view)` and `uPlacementOrigin` is
// `placementOrigin − cameraPosition`, so no large world coordinate ever enters a float here — the
// subtraction was done in double precision on the CPU.
constexpr const char *kVertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aFace;
layout(location = 3) in float aVerdict;
uniform mat4 uMvp;
uniform vec3 uPlacementOrigin;
uniform float uPlacementYawCos;
uniform float uPlacementYawSin;
out vec4 vColor;
out float vFace;
out float vVerdict;
void main() {
  // Rotate about the placement origin by the placement's yaw, then offset to camera-relative space.
  vec3 p = aPosition;
  float rx = p.x * uPlacementYawCos - p.z * uPlacementYawSin;
  float rz = p.x * uPlacementYawSin + p.z * uPlacementYawCos;
  p = vec3(rx, p.y, rz) + uPlacementOrigin;
  vColor = aColor;
  vFace = aFace;
  vVerdict = aVerdict;
  gl_Position = uMvp * vec4(p, 1.0);
}
)";

constexpr const char *kFragmentSource = R"(#version 300 es
precision mediump float;
in vec4 vColor;
in float vFace;
in float vVerdict;
uniform float uAlpha;
uniform int uMode;
uniform int uMismatchMode;
uniform vec4 uPalette[6];
out vec4 fragColor;
void main() {
  // The mismatch display modes (F6). The verdict is an integer 0..5; the mode decides whether this
  // fragment is drawn at all. `discard` avoids a per-vertex remesh when the mode changes.
  int verdict = int(vVerdict + 0.5);
  bool correct = verdict == 0;
  bool unknown = verdict == 5;
  if (uMode == 2) { // mismatch mode: colour by verdict, honour the display filter
    if (uMismatchMode == 1 && (correct || unknown)) discard; // only mismatches
    if (uMismatchMode == 2 && correct) discard;               // hide correct
  }
  // A little directional shading so faces read apart without any lighting model.
  float shade = vFace < 0.5 ? 1.0 : (vFace < 1.5 ? 0.82 : 0.6);
  vec3 rgb = vColor.rgb;
  if (uMode == 2) {
    vec4 pal = uPalette[clamp(verdict, 0, 5)];
    rgb = pal.rgb;
  }
  fragColor = vec4(rgb * shade, vColor.a * uAlpha);
}
)";

// Outline pass: the same vertex shader, a flat-colour fragment shader. A line vertex carries the
// outline colour; alpha comes from the same uniform so the whole ghost fades together.
constexpr const char *kLineFragmentSource = R"(#version 300 es
precision mediump float;
in vec4 vColor;
in float vFace;
in float vVerdict;
uniform float uAlpha;
out vec4 fragColor;
void main() {
  fragColor = vec4(vColor.rgb, vColor.a * uAlpha);
}
)";

using glGetIntegerv_t = void (*)(GLenum, GLint *);
using glGetBooleanv_t = void (*)(GLenum, GLboolean *);
using glEnable_t = void (*)(GLenum);
using glDisable_t = void (*)(GLenum);
using glDepthMask_t = void (*)(GLboolean);
using glDepthFunc_t = void (*)(GLenum);
using glCullFace_t = void (*)(GLenum);
using glFrontFace_t = void (*)(GLenum);
using glBlendFuncSeparate_t = void (*)(GLenum, GLenum, GLenum, GLenum);
using glGenBuffers_t = void (*)(GLsizei, GLuint *);
using glBindBuffer_t = void (*)(GLenum, GLuint);
using glBufferData_t = void (*)(GLenum, GLsizeiptr, const void *, GLenum);
using glDeleteBuffers_t = void (*)(GLsizei, const GLuint *);
using glGenVertexArrays_t = void (*)(GLsizei, GLuint *);
using glBindVertexArray_t = void (*)(GLuint);
using glDeleteVertexArrays_t = void (*)(GLsizei, const GLuint *);
using glEnableVertexAttribArray_t = void (*)(GLuint);
using glVertexAttribPointer_t = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
using glCreateShader_t = GLuint (*)(GLenum);
using glShaderSource_t = void (*)(GLuint, GLsizei, const char *const *, const GLint *);
using glCompileShader_t = void (*)(GLuint);
using glGetShaderiv_t = void (*)(GLuint, GLenum, GLint *);
using glDeleteShader_t = void (*)(GLuint);
using glCreateProgram_t = GLuint (*)();
using glAttachShader_t = void (*)(GLuint, GLuint);
using glLinkProgram_t = void (*)(GLuint);
using glGetProgramiv_t = void (*)(GLuint, GLenum, GLint *);
using glUseProgram_t = void (*)(GLuint);
using glGetUniformLocation_t = GLint (*)(GLuint, const char *);
using glUniformMatrix4fv_t = void (*)(GLint, GLsizei, GLboolean, const GLfloat *);
using glUniform1f_t = void (*)(GLint, GLfloat);
using glDrawElements_t = void (*)(GLenum, GLsizei, GLenum, const void *);
using glActiveTexture_t = void (*)(GLenum);
using glBindTexture_t = void (*)(GLenum, GLuint);

constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kVertexShaderType = 0x8B31;
constexpr GLenum kFragmentShaderType = 0x8B30;
constexpr GLenum kBack = 0x0405;
constexpr GLenum kCcw = 0x0901;

} // namespace

void SchematicRenderer::saveState(SavedState &state) const {
  const Functions &g = resolve();
  auto geti = reinterpret_cast<glGetIntegerv_t>(g.GetIntegerv);
  auto getb = reinterpret_cast<glGetBooleanv_t>(g.GetBooleanv);
  geti(GL_CURRENT_PROGRAM, &state.program);
  geti(GL_VERTEX_ARRAY_BINDING, &state.vao);
  geti(GL_ARRAY_BUFFER, &state.arrayBuffer);
  geti(GL_ELEMENT_ARRAY_BUFFER, &state.elementBuffer);
  // The shader binds no texture, so the 2D binding is left alone; only the active unit is global
  // state the game may depend on, so it is captured and restored.
  geti(GL_ACTIVE_TEXTURE, &state.activeTexture);
  getb(GL_BLEND, &state.blend);
  getb(GL_DEPTH_TEST, &state.depthTest);
  getb(GL_CULL_FACE, &state.cullFace);
  getb(GL_DEPTH_WRITEMASK, &state.depthMask);
  geti(GL_DEPTH_FUNC, &state.depthFunc);
  geti(GL_CULL_FACE_MODE, &state.cullMode);
  geti(GL_FRONT_FACE, &state.frontFace);
  geti(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
  geti(GL_BLEND_DST_RGB, &state.blendDstRgb);
  geti(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
  geti(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
  geti(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
  geti(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
  // The game may leave these enabled, which would make the ghost invisible or clipped.
  getb(GL_STENCIL_TEST, &state.stencilTest);
  getb(GL_SCISSOR_TEST, &state.scissorTest);
  geti(GL_COLOR_WRITEMASK, state.colorMask);
}

void SchematicRenderer::restoreState(const SavedState &state) const {
  const Functions &g = resolve();
  auto bindVao = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);
  auto bindBuf = reinterpret_cast<glBindBuffer_t>(g.BindBuffer);
  auto useProg = reinterpret_cast<glUseProgram_t>(g.UseProgram);
  auto activeTex = reinterpret_cast<glActiveTexture_t>(g.ActiveTexture);
  auto enable = reinterpret_cast<glEnable_t>(g.Enable);
  auto disable = reinterpret_cast<glDisable_t>(g.Disable);
  auto depthMask = reinterpret_cast<glDepthMask_t>(g.DepthMask);
  auto depthFunc = reinterpret_cast<glDepthFunc_t>(g.DepthFunc);
  auto cullFace = reinterpret_cast<glCullFace_t>(g.CullFace);
  auto frontFace = reinterpret_cast<glFrontFace_t>(g.FrontFace);
  auto blendFunc = reinterpret_cast<glBlendFuncSeparate_t>(g.BlendFuncSeparate);
  auto blendEquation =
      reinterpret_cast<void (*)(GLenum, GLenum)>(g.BlendEquationSeparate);
  auto colorMask = reinterpret_cast<void (*)(GLboolean, GLboolean, GLboolean, GLboolean)>(
      g.ColorMask);

  // Restore in the reverse order of how they were set, and restore *everything* — a stale binding
  // left behind is what corrupts the game's next draw.
  activeTex(static_cast<GLenum>(state.activeTexture));
  bindVao(static_cast<GLuint>(state.vao));
  bindBuf(GL_ARRAY_BUFFER, static_cast<GLuint>(state.arrayBuffer));
  bindBuf(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(state.elementBuffer));
  useProg(static_cast<GLuint>(state.program));
  if (state.blend) enable(GL_BLEND); else disable(GL_BLEND);
  if (state.depthTest) enable(GL_DEPTH_TEST); else disable(GL_DEPTH_TEST);
  if (state.cullFace) enable(GL_CULL_FACE); else disable(GL_CULL_FACE);
  if (state.stencilTest) enable(GL_STENCIL_TEST); else disable(GL_STENCIL_TEST);
  if (state.scissorTest) enable(GL_SCISSOR_TEST); else disable(GL_SCISSOR_TEST);
  depthMask(state.depthMask);
  depthFunc(static_cast<GLenum>(state.depthFunc));
  cullFace(static_cast<GLenum>(state.cullMode));
  frontFace(static_cast<GLenum>(state.frontFace));
  if (blendFunc) {
    blendFunc(static_cast<GLenum>(state.blendSrcRgb), static_cast<GLenum>(state.blendDstRgb),
              static_cast<GLenum>(state.blendSrcAlpha), static_cast<GLenum>(state.blendDstAlpha));
  }
  if (blendEquation) {
    blendEquation(static_cast<GLenum>(state.blendEquationRgb),
                  static_cast<GLenum>(state.blendEquationAlpha));
  }
  if (colorMask) {
    colorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
  }
}

void SchematicRenderer::probeBackend() {
  const Functions &g = resolve();
  if (!g.usable()) {
    mBackendUsable = false;
    std::snprintf(mBackendDescription, sizeof(mBackendDescription),
                  "GLES entry points unavailable (non-GL backend?)");
    return;
  }
  if (g.GetString == nullptr) {
    // No glGetString means we cannot confirm the version; treat the resolved core as enough.
    mBackendUsable = true;
    std::snprintf(mBackendDescription, sizeof(mBackendDescription), "OpenGL ES (version unknown)");
    return;
  }
  using GetStringFn = const unsigned char *(*)(GLenum);
  auto getString = reinterpret_cast<GetStringFn>(g.GetString);
  const char *version = reinterpret_cast<const char *>(getString(GL_VERSION));
  const char *renderer = reinterpret_cast<const char *>(getString(GL_RENDERER));
  if (version == nullptr) {
    mBackendUsable = false;
    std::snprintf(mBackendDescription, sizeof(mBackendDescription), "GL_VERSION unavailable");
    return;
  }
  mBackendUsable = isGles3OrLater(version);
  std::snprintf(mBackendDescription, sizeof(mBackendDescription), "%s%s%s", version,
                renderer != nullptr ? " / " : "", renderer != nullptr ? renderer : "");
}

bool SchematicRenderer::initialize() {
  if (mReady) return true;
  const Functions &g = resolve();
  if (!g.usable()) {
    probeBackend();
    return false;
  }
  probeBackend();
  if (!mBackendUsable) {
    // A non-GLES-3 backend (e.g. Vulkan-only): do not attempt GL calls. The caller reports the
    // named reason from `backendDescription()` and draws nothing.
    return false;
  }

  auto createShader = reinterpret_cast<glCreateShader_t>(g.CreateShader);
  auto shaderSource = reinterpret_cast<glShaderSource_t>(g.ShaderSource);
  auto compileShader = reinterpret_cast<glCompileShader_t>(g.CompileShader);
  auto getShaderiv = reinterpret_cast<glGetShaderiv_t>(g.GetShaderiv);
  auto deleteShader = reinterpret_cast<glDeleteShader_t>(g.DeleteShader);
  auto createProgram = reinterpret_cast<glCreateProgram_t>(g.CreateProgram);
  auto attachShader = reinterpret_cast<glAttachShader_t>(g.AttachShader);
  auto linkProgram = reinterpret_cast<glLinkProgram_t>(g.LinkProgram);
  auto getProgramiv = reinterpret_cast<glGetProgramiv_t>(g.GetProgramiv);

  auto compile = [&](GLenum type, const char *src) -> GLuint {
    const GLuint shader = createShader(type);
    if (shader == 0) return 0;
    shaderSource(shader, 1, &src, nullptr);
    compileShader(shader);
    GLint status = 0;
    getShaderiv(shader, kCompileStatus, &status);
    if (status == 0) {
      deleteShader(shader);
      return 0;
    }
    return shader;
  };

  // Builds a program from a vertex+fragment source pair, deleting both shaders on any failure so a
  // failed link never leaks. Returns 0 on failure.
  auto buildProgram = [&](const char *vertexSrc, const char *fragmentSrc) -> GLuint {
    const GLuint vs = compile(kVertexShaderType, vertexSrc);
    const GLuint fs = compile(kFragmentShaderType, fragmentSrc);
    if (vs == 0 || fs == 0) {
      if (vs != 0) deleteShader(vs);
      if (fs != 0) deleteShader(fs);
      return 0;
    }
    const GLuint program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, kLinkStatus, &linked);
    deleteShader(vs);
    deleteShader(fs);
    if (linked == 0) {
      if (program != 0 && g.DeleteProgram) {
        reinterpret_cast<void (*)(GLuint)>(g.DeleteProgram)(program);
      }
      return 0;
    }
    return program;
  };

  mProgram = buildProgram(kVertexSource, kFragmentSource);
  if (mProgram == 0) return false;
  mLineProgram = buildProgram(kVertexSource, kLineFragmentSource);
  if (mLineProgram == 0) {
    // Free the fill program too, so a partial init does not leak.
    if (g.DeleteProgram) reinterpret_cast<void (*)(GLuint)>(g.DeleteProgram)(mProgram);
    mProgram = 0;
    return false;
  }

  // Resolve every uniform once, here, rather than per frame. A uniform lookup is a string compare
  // against the program's reflection data; doing it on the render thread every frame is waste.
  auto getUniformLocation = reinterpret_cast<glGetUniformLocation_t>(g.GetUniformLocation);
  mUniformMvp = getUniformLocation(mProgram, "uMvp");
  mUniformAlpha = getUniformLocation(mProgram, "uAlpha");
  mUniformPlacementOrigin = getUniformLocation(mProgram, "uPlacementOrigin");
  mUniformPlacementYawCos = getUniformLocation(mProgram, "uPlacementYawCos");
  mUniformPlacementYawSin = getUniformLocation(mProgram, "uPlacementYawSin");
  mUniformMode = getUniformLocation(mProgram, "uMode");
  mUniformMismatchMode = getUniformLocation(mProgram, "uMismatchMode");
  mUniformPalette = getUniformLocation(mProgram, "uPalette");
  mLineUniformMvp = getUniformLocation(mLineProgram, "uMvp");
  mLineUniformAlpha = getUniformLocation(mLineProgram, "uAlpha");
  mLineUniformPlacementOrigin = getUniformLocation(mLineProgram, "uPlacementOrigin");
  mLineUniformPlacementYawCos = getUniformLocation(mLineProgram, "uPlacementYawCos");
  mLineUniformPlacementYawSin = getUniformLocation(mLineProgram, "uPlacementYawSin");
  mReady = true;
  return true;
}

void SchematicRenderer::bindVertexLayout() const {
  const Functions &g = resolve();
  auto enableAttrib = reinterpret_cast<glEnableVertexAttribArray_t>(g.EnableVertexAttribArray);
  auto attribPointer = reinterpret_cast<glVertexAttribPointer_t>(g.VertexAttribPointer);
  // MeshVertex: position(3 float) + colour(4 ubyte normalized) + face(1 ubyte) = 20 bytes. Both the
  // fill and the line programs declare the same layout, so the VAO is reusable across them.
  const GLsizei stride = static_cast<GLsizei>(sizeof(mesh::MeshVertex));
  enableAttrib(0);
  attribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, x)));
  enableAttrib(1);
  attribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, color)));
  enableAttrib(2);
  attribPointer(2, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, face)));
  // The per-vertex verdict (F6), read as a normalized byte widened to float in the shader.
  enableAttrib(3);
  attribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, verdict)));
}

bool SchematicRenderer::uploadSection(SectionBuffer &buffer, const mesh::SectionMesh &section) {
  if (!mReady || section.empty()) return false;
  const Functions &g = resolve();

  auto genBuffers = reinterpret_cast<glGenBuffers_t>(g.GenBuffers);
  auto bindBuffer = reinterpret_cast<glBindBuffer_t>(g.BindBuffer);
  auto bufferData = reinterpret_cast<glBufferData_t>(g.BufferData);
  auto genVertexArrays = reinterpret_cast<glGenVertexArrays_t>(g.GenVertexArrays);
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);

  if (!buffer.uploaded) {
    genBuffers(1, &buffer.vbo);
    genBuffers(1, &buffer.ibo);
    genVertexArrays(1, &buffer.vao);
  }

  bindVertexArray(buffer.vao);
  bindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
  bufferData(GL_ARRAY_BUFFER,
             static_cast<GLsizeiptr>(section.vertices.size() * sizeof(mesh::MeshVertex)),
             section.vertices.data(), GL_STATIC_DRAW);
  bindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo);
  bufferData(GL_ELEMENT_ARRAY_BUFFER,
             static_cast<GLsizeiptr>(section.triangles.size() * sizeof(mesh::MeshTriangle)),
             section.triangles.data(), GL_STATIC_DRAW);
  bindVertexLayout();

  buffer.indexCount = static_cast<int>(section.triangles.size() * 3);
  buffer.min = render::Vec3{section.originX, section.originY, section.originZ};
  buffer.max = render::Vec3{section.originX + mesh::SectionMesher::kSectionSize,
                            section.originY + mesh::SectionMesher::kSectionSize,
                            section.originZ + mesh::SectionMesher::kSectionSize};
  buffer.uploaded = true;
  buffer.linePrimitive = false;

  bindVertexArray(0);
  bindBuffer(GL_ARRAY_BUFFER, 0);
  bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  return true;
}

bool SchematicRenderer::uploadOutlines(SectionBuffer &buffer,
                                       const std::vector<mesh::MeshVertex> &lineVertices,
                                       const std::vector<mesh::MeshTriangle> &lineSegments,
                                       const render::Vec3 &min, const render::Vec3 &max) {
  if (!mReady || lineSegments.empty()) return false;
  const Functions &g = resolve();

  auto genBuffers = reinterpret_cast<glGenBuffers_t>(g.GenBuffers);
  auto bindBuffer = reinterpret_cast<glBindBuffer_t>(g.BindBuffer);
  auto bufferData = reinterpret_cast<glBufferData_t>(g.BufferData);
  auto genVertexArrays = reinterpret_cast<glGenVertexArrays_t>(g.GenVertexArrays);
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);

  if (!buffer.uploaded) {
    genBuffers(1, &buffer.vbo);
    genBuffers(1, &buffer.ibo);
    genVertexArrays(1, &buffer.vao);
  }
  bindVertexArray(buffer.vao);
  bindBuffer(GL_ARRAY_BUFFER, buffer.vbo);
  bufferData(GL_ARRAY_BUFFER,
             static_cast<GLsizeiptr>(lineVertices.size() * sizeof(mesh::MeshVertex)),
             lineVertices.data(), GL_STATIC_DRAW);
  bindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo);
  bufferData(GL_ELEMENT_ARRAY_BUFFER,
             static_cast<GLsizeiptr>(lineSegments.size() * sizeof(mesh::MeshTriangle)),
             lineSegments.data(), GL_STATIC_DRAW);
  bindVertexLayout();

  // A line index list: two indices per segment.
  buffer.indexCount = static_cast<int>(lineSegments.size() * 2);
  buffer.min = min;
  buffer.max = max;
  buffer.uploaded = true;
  buffer.linePrimitive = true;

  bindVertexArray(0);
  bindBuffer(GL_ARRAY_BUFFER, 0);
  bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  return true;
}

void SchematicRenderer::releaseSection(SectionBuffer &buffer) {
  if (!buffer.uploaded) return;
  const Functions &g = resolve();
  auto deleteBuffers = reinterpret_cast<glDeleteBuffers_t>(g.DeleteBuffers);
  auto deleteVertexArrays = reinterpret_cast<glDeleteVertexArrays_t>(g.DeleteVertexArrays);
  if (deleteBuffers) {
    GLuint buffers[2] = {buffer.vbo, buffer.ibo};
    deleteBuffers(2, buffers);
  }
  if (deleteVertexArrays && buffer.vao != 0) {
    deleteVertexArrays(1, &buffer.vao);
  }
  buffer = SectionBuffer{};
}

void SchematicRenderer::shutdown() {
  const Functions &g = resolve();
  if (g.DeleteProgram) {
    auto deleteProgram = reinterpret_cast<void (*)(GLuint)>(g.DeleteProgram);
    if (mProgram != 0) deleteProgram(mProgram);
    if (mLineProgram != 0) deleteProgram(mLineProgram);
  }
  mProgram = 0;
  mLineProgram = 0;
  mReady = false;
}

void SchematicRenderer::setPlacementUniforms(GLuint program, const render::Mat4 &mvp,
                                             const render::Vec3 &cameraRelativeOrigin,
                                             float placementRotationDegrees) const {
  const Functions &g = resolve();
  auto uniformMatrix = reinterpret_cast<glUniformMatrix4fv_t>(g.UniformMatrix4fv);
  auto uniform1f = reinterpret_cast<glUniform1f_t>(g.Uniform1f);
  auto uniform3f = reinterpret_cast<void (*)(GLint, GLfloat, GLfloat, GLfloat)>(g.Uniform3f);

  const GLint uMvp = program == mProgram ? mUniformMvp : mLineUniformMvp;
  const GLint uOrigin = program == mProgram ? mUniformPlacementOrigin : mLineUniformPlacementOrigin;
  const GLint uCos = program == mProgram ? mUniformPlacementYawCos : mLineUniformPlacementYawCos;
  const GLint uSin = program == mProgram ? mUniformPlacementYawSin : mLineUniformPlacementYawSin;

  uniformMatrix(uMvp, 1, GL_FALSE, mvp.m);
  const float yawRadians = placementRotationDegrees * 3.14159265358979f / 180.0f;
  // Yaw is passed as pre-computed sin/cos so the shader avoids a trig call per vertex.
  if (uniform3f && uOrigin >= 0) {
    uniform3f(uOrigin, cameraRelativeOrigin.x, cameraRelativeOrigin.y, cameraRelativeOrigin.z);
  }
  if (uCos >= 0) uniform1f(uCos, std::cos(yawRadians));
  if (uSin >= 0) uniform1f(uSin, std::sin(yawRadians));
}

/// Draws one list of section buffers with a program already bound. Shared by the fill pass, the
/// outline pass and the outline-only mode, so the culling and state logic lives once.
static void drawSectionList(const Functions &g, GLuint program, GLint uniformAlpha, float alpha,
                            const std::vector<SectionBuffer> &sections,
                            const render::Vec3 &localCameraPosition, float maxDistance,
                            const render::Frustum &localFrustum, bool lines, int &triangles,
                            int &sectionCount) {
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);
  auto drawElements = reinterpret_cast<glDrawElements_t>(g.DrawElements);
  auto uniform1f = reinterpret_cast<glUniform1f_t>(g.Uniform1f);
  (void)program;
  if (uniformAlpha >= 0) uniform1f(uniformAlpha, alpha);

  for (const SectionBuffer &section : sections) {
    if (!section.uploaded || section.indexCount == 0) continue;
    if (section.linePrimitive != lines) continue;
    // Both culls run in placement-local space, where a section's box is axis-aligned regardless of
    // the placement's rotation. The camera was transformed into this space by the caller.
    if (maxDistance > 0.0f) {
      const render::Vec3 center{(section.min.x + section.max.x) * 0.5f,
                                (section.min.y + section.max.y) * 0.5f,
                                (section.min.z + section.max.z) * 0.5f};
      if (render::length(center - localCameraPosition) > maxDistance) continue;
    }
    if (!localFrustum.intersectsBox(section.min, section.max)) continue;

    bindVertexArray(section.vao);
    drawElements(lines ? GL_LINES : GL_TRIANGLES, section.indexCount, GL_UNSIGNED_INT, nullptr);
    if (!lines) triangles += section.indexCount / 3;
    ++sectionCount;
  }
}

void SchematicRenderer::draw(const render::Mat4 &mvp, const render::Vec3 &cameraRelativeOrigin,
                             float placementRotationDegrees, const DrawOptions &options,
                             const render::Vec3 &localCameraPosition,
                             const render::Frustum &localFrustum,
                             const std::vector<SectionBuffer> &sections,
                             const std::vector<SectionBuffer> &outlineSections) {
  mLastTriangles = 0;
  mLastSections = 0;
  if (!mReady) return;
  if (sections.empty() && outlineSections.empty()) return;
  const Functions &g = resolve();

  // Save everything touched, draw, restore. The restore runs even if nothing is drawn, so a frame
  // with no visible sections cannot leave the game's state changed.
  SavedState saved;
  saveState(saved);

  auto enable = reinterpret_cast<glEnable_t>(g.Enable);
  auto disable = reinterpret_cast<glDisable_t>(g.Disable);
  auto depthMask = reinterpret_cast<glDepthMask_t>(g.DepthMask);
  auto depthFunc = reinterpret_cast<glDepthFunc_t>(g.DepthFunc);
  auto cullFace = reinterpret_cast<glCullFace_t>(g.CullFace);
  auto frontFace = reinterpret_cast<glFrontFace_t>(g.FrontFace);
  auto blendFunc = reinterpret_cast<glBlendFuncSeparate_t>(g.BlendFuncSeparate);
  auto blendEquation = reinterpret_cast<void (*)(GLenum, GLenum)>(g.BlendEquationSeparate);
  auto colorMask = reinterpret_cast<void (*)(GLboolean, GLboolean, GLboolean, GLboolean)>(
      g.ColorMask);
  auto useProgram = reinterpret_cast<glUseProgram_t>(g.UseProgram);
  auto uniform1f = reinterpret_cast<glUniform1f_t>(g.Uniform1f);
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);
  auto polygonOffset = reinterpret_cast<void (*)(GLfloat, GLfloat)>(g.PolygonOffset);
  auto lineWidth = reinterpret_cast<void (*)(GLfloat)>(g.LineWidth);

  // Depth test stays ON so real terrain occludes the ghost blocks (the whole point of item 5).
  enable(GL_DEPTH_TEST);
  depthFunc(GL_LEQUAL);
  enable(GL_CULL_FACE);
  cullFace(kBack);
  frontFace(kCcw);
  // The game may leave stencil or scissor enabled, which would clip the ghost or fail its stencil
  // test and make it invisible, and a game-set colour mask could hide its channels. Saving these is
  // not enough — they have to be actively turned off / opened for the draw, then restored.
  disable(GL_STENCIL_TEST);
  disable(GL_SCISSOR_TEST);
  if (colorMask) colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  // A non-default blend equation would make a translucent draw come out wrong; force the default.
  if (blendEquation) blendEquation(GL_FUNC_ADD, GL_FUNC_ADD);

  const bool outlineOnly = options.mode == DrawMode::Outlines;

  // Pass 1: filled ghost faces (translucent/mismatch), unless this is the outline-only mode.
  if (!outlineOnly && !sections.empty()) {
    useProgram(mProgram);
    // Translucent draws must not write depth, or they hide each other; mismatch does write it.
    depthMask(options.mode == DrawMode::Translucent ? GL_FALSE : GL_TRUE);
    if (options.mode == DrawMode::Translucent || options.mode == DrawMode::Mismatch) {
      enable(GL_BLEND);
      if (blendFunc) {
        blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
    } else {
      disable(GL_BLEND);
    }
    // Ghost faces coincide with real block faces; pull them toward the camera so they do not
    // z-fight. Both the factor and units are configurable.
    if (polygonOffset) {
      enable(GL_POLYGON_OFFSET_FILL);
      polygonOffset(options.polygonOffsetFactor, options.polygonOffsetUnits);
    }
    setPlacementUniforms(mProgram, mvp, cameraRelativeOrigin, placementRotationDegrees);
    if (mUniformAlpha >= 0) uniform1f(mUniformAlpha, options.alpha);
    // The mismatch display mode and palette (F6). Cheap scalars/array, set every frame.
    auto uniform1i = reinterpret_cast<void (*)(GLint, GLint)>(g.Uniform1i);
    auto uniform4fv = reinterpret_cast<void (*)(GLint, GLsizei, const GLfloat *)>(g.Uniform4fv);
    if (uniform1i && mUniformMode >= 0) uniform1i(mUniformMode, static_cast<GLint>(options.mode));
    if (uniform1i && mUniformMismatchMode >= 0) {
      uniform1i(mUniformMismatchMode, static_cast<GLint>(options.mismatchMode));
    }
    if (uniform4fv && mUniformPalette >= 0) {
      // The palette in verdict order: Correct, Missing, WrongBlock, WrongState, Extra, Unknown.
      const bitematica::world::MismatchPalette p = options.palette;
      const GLfloat pal[6 * 4] = {
          paletteChannel(p.correct, 16), paletteChannel(p.correct, 8), paletteChannel(p.correct, 0), paletteChannel(p.correct, 24),
          paletteChannel(p.missing, 16), paletteChannel(p.missing, 8), paletteChannel(p.missing, 0), paletteChannel(p.missing, 24),
          paletteChannel(p.wrongBlock, 16), paletteChannel(p.wrongBlock, 8), paletteChannel(p.wrongBlock, 0), paletteChannel(p.wrongBlock, 24),
          paletteChannel(p.wrongState, 16), paletteChannel(p.wrongState, 8), paletteChannel(p.wrongState, 0), paletteChannel(p.wrongState, 24),
          paletteChannel(p.extra, 16), paletteChannel(p.extra, 8), paletteChannel(p.extra, 0), paletteChannel(p.extra, 24),
          paletteChannel(p.unknown, 16), paletteChannel(p.unknown, 8), paletteChannel(p.unknown, 0), paletteChannel(p.unknown, 24)};
      uniform4fv(mUniformPalette, 6, pal);
    }
    drawSectionList(g, mProgram, mUniformAlpha, options.alpha, sections, localCameraPosition,
                    options.maxDistance, localFrustum, false, mLastTriangles, mLastSections);
    if (polygonOffset) {
      disable(GL_POLYGON_OFFSET_FILL);
      polygonOffset(0.0f, 0.0f);
    }
  }

  // Pass 2: block-edge outlines (B8), when enabled or in the outline-only mode.
  if ((options.outlines || outlineOnly) && !outlineSections.empty()) {
    useProgram(mLineProgram);
    depthMask(GL_FALSE); // lines must not write depth either
    enable(GL_BLEND);
    if (blendFunc) {
      blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // A smaller offset than the faces, so the lines sit just in front without poking through.
    if (polygonOffset) {
      enable(GL_POLYGON_OFFSET_FILL);
      polygonOffset(options.polygonOffsetFactor * 0.5f, options.polygonOffsetUnits * 0.5f);
    }
    if (lineWidth) lineWidth(options.outlineWidth);
    setPlacementUniforms(mLineProgram, mvp, cameraRelativeOrigin, placementRotationDegrees);
    // `drawSectionList` sets uAlpha on whichever program is bound; the line program has its own.
    drawSectionList(g, mLineProgram, mLineUniformAlpha, options.alpha, outlineSections,
                    localCameraPosition, options.maxDistance, localFrustum, true, mLastTriangles,
                    mLastSections);
    if (polygonOffset) {
      disable(GL_POLYGON_OFFSET_FILL);
      polygonOffset(0.0f, 0.0f);
    }
  }

  bindVertexArray(0);
  restoreState(saved);
}

void SchematicRenderer::drawOutlines(const render::Mat4 &mvp,
                                     const render::Vec3 &cameraRelativeOrigin,
                                     float placementRotationDegrees, const DrawOptions &options,
                                     const render::Vec3 &localCameraPosition,
                                     const render::Frustum &localFrustum,
                                     const std::vector<SectionBuffer> &outlineSections) {
  // The outline-only render mode: the same pass as `draw`'s outline stage, without the fill.
  DrawOptions outlineOptions = options;
  outlineOptions.mode = DrawMode::Outlines;
  outlineOptions.outlines = true;
  draw(mvp, cameraRelativeOrigin, placementRotationDegrees, outlineOptions, localCameraPosition,
       localFrustum, {}, outlineSections);
}

} // namespace bitematica::gles
