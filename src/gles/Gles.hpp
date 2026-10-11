#pragma once

#include <cstddef>
#include <cstdint>

namespace bitematica::gles {

using GLenum = std::uint32_t;
using GLboolean = std::uint8_t;
using GLint = std::int32_t;
using GLsizei = std::int32_t;
using GLuint = std::uint32_t;
using GLsizeiptr = std::intptr_t;
using GLfloat = float;

// GL enum values used here. Kept as constants so no GLES header is required in every TU (the host
// test build has none), matching how the preloader's own renderer is written.
inline constexpr GLenum GL_FALSE = 0;
inline constexpr GLenum GL_TRUE = 1;
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_LINES = 0x0001;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_CULL_FACE = 0x0B44;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_LEQUAL = 0x0203;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_CURRENT_PROGRAM = 0x8B31;
inline constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x8CA6;
inline constexpr GLenum GL_TEXTURE_BINDING_2D = 0x0BDE;
inline constexpr GLenum GL_DEPTH_WRITEMASK = 0x0B24;
inline constexpr GLenum GL_VIEWPORT = 0x0BA2;
inline constexpr GLenum GL_BLEND_SRC_RGB = 0x80C9;
inline constexpr GLenum GL_BLEND_DST_RGB = 0x80C8;
inline constexpr GLenum GL_DEPTH_FUNC = 0x0B74;
inline constexpr GLenum GL_CULL_FACE_MODE = 0x0B45;
inline constexpr GLenum GL_FRONT_FACE = 0x0B46;
inline constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;

/// The runtime-resolved GLES entry points BiteMatica needs.
///
/// The mod does not link a GLES library: the game's own GLES library is already mapped in the
/// process, and the entry points are resolved from it at runtime. A null pointer makes that
/// primitive a no-op and `usable()` false, so a Vulkan-only build fails closed instead of crashing.
struct Functions {
  void *GetError = nullptr;
  void *GetIntegerv = nullptr;
  void *GetBooleanv = nullptr;
  void *Enable = nullptr;
  void *Disable = nullptr;
  void *DepthMask = nullptr;
  void *DepthFunc = nullptr;
  void *CullFace = nullptr;
  void *FrontFace = nullptr;
  void *BlendFuncSeparate = nullptr;
  void *Viewport = nullptr;
  void *GenBuffers = nullptr;
  void *BindBuffer = nullptr;
  void *BufferData = nullptr;
  void *DeleteBuffers = nullptr;
  void *GenVertexArrays = nullptr;
  void *BindVertexArray = nullptr;
  void *DeleteVertexArrays = nullptr;
  void *EnableVertexAttribArray = nullptr;
  void *VertexAttribPointer = nullptr;
  void *CreateShader = nullptr;
  void *ShaderSource = nullptr;
  void *CompileShader = nullptr;
  void *GetShaderiv = nullptr;
  void *DeleteShader = nullptr;
  void *CreateProgram = nullptr;
  void *AttachShader = nullptr;
  void *LinkProgram = nullptr;
  void *GetProgramiv = nullptr;
  void *DeleteProgram = nullptr;
  void *UseProgram = nullptr;
  void *GetUniformLocation = nullptr;
  void *UniformMatrix4fv = nullptr;
  void *Uniform1f = nullptr;
  void *Uniform3f = nullptr;
  void *Uniform4f = nullptr;
  void *ActiveTexture = nullptr;
  void *BindTexture = nullptr;
  void *DrawElements = nullptr;
  void *DrawArrays = nullptr;

  bool usable() const {
    return GetIntegerv && GetBooleanv && Enable && Disable && DepthMask && CullFace &&
           GenBuffers && BindBuffer && BufferData && GenVertexArrays && BindVertexArray &&
           EnableVertexAttribArray && VertexAttribPointer && CreateShader && ShaderSource &&
           CompileShader && GetShaderiv && CreateProgram && AttachShader && LinkProgram &&
           GetProgramiv && UseProgram && GetUniformLocation && UniformMatrix4fv &&
           ActiveTexture && BindTexture && DrawElements;
  }
};

/// Resolves the GLES entry points from the process's own GLES library. Cached after the first call.
const Functions &resolve();

} // namespace bitematica::gles
