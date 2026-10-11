#include "gles/SchematicRenderer.hpp"

#include <cmath>
#include <cstring>

namespace bitematica::gles {
namespace {

// Vertex shader: transforms a section-relative vertex by the world->clip matrix after applying the
// placement's translation and yaw, so one section's VBO serves every placement transform.
constexpr const char *kVertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aFace;
uniform mat4 uMvp;
uniform vec3 uPlacementOrigin;
uniform float uPlacementYawCos;
uniform float uPlacementYawSin;
out vec4 vColor;
out float vFace;
void main() {
  // Rotate about the placement origin by the placement's yaw, then translate to world space.
  vec3 p = aPosition;
  float rx = p.x * uPlacementYawCos - p.z * uPlacementYawSin;
  float rz = p.x * uPlacementYawSin + p.z * uPlacementYawCos;
  p = vec3(rx, p.y, rz) + uPlacementOrigin;
  vColor = aColor;
  vFace = aFace;
  gl_Position = uMvp * vec4(p, 1.0);
}
)";

constexpr const char *kFragmentSource = R"(#version 300 es
precision mediump float;
in vec4 vColor;
in float vFace;
uniform float uAlpha;
uniform int uMode;
out vec4 fragColor;
void main() {
  // A little directional shading so faces read apart without any lighting model.
  float shade = vFace < 0.5 ? 1.0 : (vFace < 1.5 ? 0.82 : 0.6);
  fragColor = vec4(vColor.rgb * shade, vColor.a * uAlpha);
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

bool SchematicRenderer::initialize() {
  if (mReady) return true;
  const Functions &g = resolve();
  if (!g.usable()) return false;

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

  const GLuint vs = compile(kVertexShaderType, kVertexSource);
  const GLuint fs = compile(kFragmentShaderType, kFragmentSource);
  if (vs == 0 || fs == 0) {
    // A failed pair must not leak the shader that did compile.
    if (vs != 0) deleteShader(vs);
    if (fs != 0) deleteShader(fs);
    return false;
  }

  mProgram = createProgram();
  attachShader(mProgram, vs);
  attachShader(mProgram, fs);
  linkProgram(mProgram);
  GLint linked = 0;
  getProgramiv(mProgram, kLinkStatus, &linked);
  // The shaders are owned by the program once attached; they are deleted either way.
  deleteShader(vs);
  deleteShader(fs);
  if (linked == 0) {
    if (mProgram != 0 && g.DeleteProgram) {
      reinterpret_cast<void (*)(GLuint)>(g.DeleteProgram)(mProgram);
    }
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
  mReady = true;
  return true;
}

bool SchematicRenderer::uploadSection(SectionBuffer &buffer, const mesh::SectionMesh &section) {
  if (!mReady || section.empty()) return false;
  const Functions &g = resolve();

  auto genBuffers = reinterpret_cast<glGenBuffers_t>(g.GenBuffers);
  auto bindBuffer = reinterpret_cast<glBindBuffer_t>(g.BindBuffer);
  auto bufferData = reinterpret_cast<glBufferData_t>(g.BufferData);
  auto genVertexArrays = reinterpret_cast<glGenVertexArrays_t>(g.GenVertexArrays);
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);
  auto enableAttrib = reinterpret_cast<glEnableVertexAttribArray_t>(g.EnableVertexAttribArray);
  auto attribPointer = reinterpret_cast<glVertexAttribPointer_t>(g.VertexAttribPointer);

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

  // MeshVertex: position(3 float) + color(4 ubyte normalized) + face(1 float) = 20 bytes.
  const GLsizei stride = static_cast<GLsizei>(sizeof(mesh::MeshVertex));
  enableAttrib(0);
  attribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, x)));
  enableAttrib(1);
  attribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, color)));
  // The shader declares aFace as a float; store it as a normalized byte offset is awkward, so
  // instead point at the byte and let the shader convert. Simplest correct choice: re-interpret the
  // face byte as a normalized unsigned byte attribute and widen in the shader.
  enableAttrib(2);
  attribPointer(2, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                reinterpret_cast<const void *>(offsetof(mesh::MeshVertex, face)));

  buffer.indexCount = static_cast<int>(section.triangles.size() * 3);
  buffer.min = render::Vec3{section.originX, section.originY, section.originZ};
  buffer.max = render::Vec3{section.originX + mesh::SectionMesher::kSectionSize,
                            section.originY + mesh::SectionMesher::kSectionSize,
                            section.originZ + mesh::SectionMesher::kSectionSize};
  buffer.uploaded = true;

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
  if (mProgram != 0 && g.DeleteProgram) {
    reinterpret_cast<void (*)(GLuint)>(g.DeleteProgram)(mProgram);
  }
  mProgram = 0;
  mReady = false;
}

void SchematicRenderer::draw(const render::Mat4 &mvp, const render::Vec3 &placementOrigin,
                             float placementRotationDegrees, DrawMode mode, float alpha,
                             const render::Vec3 &localCameraPosition, float maxDistance,
                             const render::Frustum &localFrustum,
                             const std::vector<SectionBuffer> &sections) {
  mLastTriangles = 0;
  mLastSections = 0;
  if (!mReady || sections.empty()) return;
  const Functions &g = resolve();

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
  auto uniformMatrix = reinterpret_cast<glUniformMatrix4fv_t>(g.UniformMatrix4fv);
  auto uniform1f = reinterpret_cast<glUniform1f_t>(g.Uniform1f);
  auto bindVertexArray = reinterpret_cast<glBindVertexArray_t>(g.BindVertexArray);
  auto drawElements = reinterpret_cast<glDrawElements_t>(g.DrawElements);

  useProgram(mProgram);

  // Depth test stays ON so real terrain occludes the ghost blocks (the whole point of item 5).
  enable(GL_DEPTH_TEST);
  depthFunc(GL_LEQUAL);
  // Translucent draws must not write depth, or they hide each other; outlines/mismatch do write it.
  depthMask(mode == DrawMode::Translucent ? GL_FALSE : GL_TRUE);
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
  if (mode == DrawMode::Translucent || mode == DrawMode::Mismatch) {
    enable(GL_BLEND);
    if (blendFunc) {
      blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
  } else {
    disable(GL_BLEND);
  }

  // No textures are bound by this program, but a stray binding would be sampled by the game's next
  // draw, so it is restored by restoreState regardless.

  // `mvp` is the plain world->clip matrix: the shader applies the placement transform itself.
  uniformMatrix(mUniformMvp, 1, GL_FALSE, mvp.m);
  uniform1f(mUniformAlpha, alpha);

  const float yawRadians = placementRotationDegrees * 3.14159265358979f / 180.0f;
  // The yaw is passed as pre-computed sin/cos so the shader avoids a trig call per vertex. The
  // locations were resolved once at init.
  auto uniform3f = reinterpret_cast<void (*)(GLint, GLfloat, GLfloat, GLfloat)>(g.Uniform3f);
  if (uniform3f && mUniformPlacementOrigin >= 0) {
    uniform3f(mUniformPlacementOrigin, placementOrigin.x, placementOrigin.y, placementOrigin.z);
  }
  if (mUniformPlacementYawCos >= 0) uniform1f(mUniformPlacementYawCos, std::cos(yawRadians));
  if (mUniformPlacementYawSin >= 0) uniform1f(mUniformPlacementYawSin, std::sin(yawRadians));

  for (const SectionBuffer &section : sections) {
    if (!section.uploaded || section.indexCount == 0) continue;
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
    drawElements(GL_TRIANGLES, section.indexCount, GL_UNSIGNED_INT, nullptr);
    mLastTriangles += section.indexCount / 3;
    ++mLastSections;
  }

  bindVertexArray(0);
  restoreState(saved);
}

} // namespace bitematica::gles
