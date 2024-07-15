// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_PROC_TABLE_GLVK_H_
#define FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_PROC_TABLE_GLVK_H_

#include "impeller/renderer/backend/gles/proc_table_gles.h"
#include "impeller/toolkit/gles/gles.h"

namespace impeller::glvk {

// https://registry.khronos.org/OpenGL/extensions/EXT/EXT_external_objects.txt
// https://registry.khronos.org/OpenGL/extensions/EXT/EXT_external_objects_fd.txt

#define FOR_EACH_GLVK_PROC(PROC) \
  PROC(CreateMemoryObjectsEXT)   \
  PROC(ImportMemoryFdEXT)        \
  PROC(TextureStorageMem2DEXT)   \
  PROC(GenTextures)              \
  PROC(CreateShader)             \
  PROC(DeleteShader)             \
  PROC(CompileShader)            \
  PROC(ShaderSource)             \
  PROC(GenFramebuffers)          \
  PROC(AttachShader)             \
  PROC(BindFramebuffer)          \
  PROC(LinkProgram)              \
  PROC(DeleteProgram)            \
  PROC(FramebufferTexture2D)     \
  PROC(GetProgramiv)             \
  PROC(GetShaderiv)              \
  PROC(ClearColor)               \
  PROC(CreateProgram)            \
  PROC(Enable)                   \
  PROC(Disable)                  \
  PROC(Clear)                    \
  PROC(Viewport)                 \
  PROC(BufferData)               \
  PROC(BindTexture)              \
  PROC(DeleteTextures)           \
  PROC(DeleteMemoryObjectsEXT)

class ProcTableGLVK {
 public:
  using Resolver = std::function<void*(const char* function_name)>;

  explicit ProcTableGLVK(Resolver resolver);

  ~ProcTableGLVK();

  ProcTableGLVK(const ProcTableGLVK&) = delete;

  ProcTableGLVK& operator=(const ProcTableGLVK&) = delete;

  bool IsValid() const;

#define GLVK_PROC(name) GLProc<decltype(gl##name)> name = {"gl" #name, nullptr};

  FOR_EACH_GLVK_PROC(GLVK_PROC);

#undef GLVK_PROC

 private:
  bool is_valid_ = false;
};

}  // namespace impeller::glvk

#endif  // FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_PROC_TABLE_GLVK_H_
