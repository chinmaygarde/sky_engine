// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_TRAMPOLINE_GLVK_H_
#define FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_TRAMPOLINE_GLVK_H_

#include "impeller/toolkit/glvk_trampoline/proc_table_glvk.h"

namespace impeller::glvk {

class TrampolineGLVK {
 public:
  TrampolineGLVK(ProcTableGLVK::Resolver resolver);

  ~TrampolineGLVK();

  TrampolineGLVK(const TrampolineGLVK&) = delete;

  TrampolineGLVK& operator=(const TrampolineGLVK&) = delete;

  const ProcTableGLVK& GetProcTable() const;

  bool IsValid() const;

  bool CopyTexture(GLuint from_texture, GLuint to_texture) const;

 private:
  ProcTableGLVK gl_;
  GLuint program_ = GL_NONE;

  bool is_valid_ = false;
};

}  // namespace impeller::glvk

#endif  // FLUTTER_IMPELLER_TOOLKIT_GLVK_TRAMPOLINE_TRAMPOLINE_GLVK_H_
