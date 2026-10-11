#include "gles/Gles.hpp"

#include <dlfcn.h>

#include <initializer_list>

namespace bitematica::gles {
namespace {

Functions g_functions;
bool g_resolved = false;

void *loadLibrary() {
  for (const char *candidate : {"libGLESv3.so", "libGLESv2.so", "libGLESv1_CM.so"}) {
    // Prefer the copy the game has already loaded; fall back to a fresh dlopen.
    void *handle = dlopen(candidate, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) handle = dlopen(candidate, RTLD_NOW);
    if (handle) return handle;
  }
  return nullptr;
}

} // namespace

const Functions &resolve() {
  if (g_resolved) return g_functions;
  g_resolved = true;

  void *handle = loadLibrary();
  if (!handle) return g_functions;

#define LOAD(member, name) \
  g_functions.member = dlsym(handle, name)
  LOAD(GetError, "glGetError");
  LOAD(GetIntegerv, "glGetIntegerv");
  LOAD(GetBooleanv, "glGetBooleanv");
  LOAD(Enable, "glEnable");
  LOAD(Disable, "glDisable");
  LOAD(DepthMask, "glDepthMask");
  LOAD(DepthFunc, "glDepthFunc");
  LOAD(CullFace, "glCullFace");
  LOAD(FrontFace, "glFrontFace");
  LOAD(BlendFuncSeparate, "glBlendFuncSeparate");
  LOAD(BlendEquationSeparate, "glBlendEquationSeparate");
  LOAD(ColorMask, "glColorMask");
  LOAD(Viewport, "glViewport");
  LOAD(GenBuffers, "glGenBuffers");
  LOAD(BindBuffer, "glBindBuffer");
  LOAD(BufferData, "glBufferData");
  LOAD(DeleteBuffers, "glDeleteBuffers");
  LOAD(GenVertexArrays, "glGenVertexArrays");
  LOAD(BindVertexArray, "glBindVertexArray");
  LOAD(DeleteVertexArrays, "glDeleteVertexArrays");
  LOAD(EnableVertexAttribArray, "glEnableVertexAttribArray");
  LOAD(VertexAttribPointer, "glVertexAttribPointer");
  LOAD(CreateShader, "glCreateShader");
  LOAD(ShaderSource, "glShaderSource");
  LOAD(CompileShader, "glCompileShader");
  LOAD(GetShaderiv, "glGetShaderiv");
  LOAD(DeleteShader, "glDeleteShader");
  LOAD(CreateProgram, "glCreateProgram");
  LOAD(AttachShader, "glAttachShader");
  LOAD(LinkProgram, "glLinkProgram");
  LOAD(GetProgramiv, "glGetProgramiv");
  LOAD(DeleteProgram, "glDeleteProgram");
  LOAD(UseProgram, "glUseProgram");
  LOAD(GetUniformLocation, "glGetUniformLocation");
  LOAD(UniformMatrix4fv, "glUniformMatrix4fv");
  LOAD(Uniform1f, "glUniform1f");
  LOAD(Uniform3f, "glUniform3f");
  LOAD(Uniform4f, "glUniform4f");
  LOAD(ActiveTexture, "glActiveTexture");
  LOAD(BindTexture, "glBindTexture");
  LOAD(DrawElements, "glDrawElements");
  LOAD(DrawArrays, "glDrawArrays");
  LOAD(PolygonOffset, "glPolygonOffset");
  LOAD(LineWidth, "glLineWidth");
  LOAD(GetString, "glGetString");
#undef LOAD

  return g_functions;
}

} // namespace bitematica::gles
