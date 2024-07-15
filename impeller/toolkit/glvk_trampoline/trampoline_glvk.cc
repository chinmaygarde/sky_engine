// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/glvk_trampoline/trampoline_glvk.h"

#include "impeller/base/validation.h"
#include "impeller/renderer/backend/gles/formats_gles.h"

namespace impeller::glvk {

static constexpr const char* kVertShader = R"IMPELLER_SHADER(#version 100
attribute vec2 vPosition;
void main() {
  gl_Position = vec4(vPosition, 0.0, 1.0);
}
)IMPELLER_SHADER";

static constexpr const char* kFragShader = R"IMPELLER_SHADER(#version 100
void main() {
  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)IMPELLER_SHADER";

TrampolineGLVK::TrampolineGLVK(ProcTableGLVK::Resolver resolver)
    : gl_(resolver) {
  if (!gl_.IsValid()) {
    VALIDATION_LOG << "Could not setup trampoline proc table.";
    return;
  }

  // TODO(csg): Perform extensions check.

  // Generate program object.
  auto vert_shader = gl_.CreateShader(GL_VERTEX_SHADER);
  auto frag_shader = gl_.CreateShader(GL_FRAGMENT_SHADER);

  GLint vert_shader_size = strlen(kVertShader);
  GLint frag_shader_size = strlen(kFragShader);

  gl_.ShaderSource(vert_shader, 1u, &kVertShader, &vert_shader_size);
  gl_.ShaderSource(frag_shader, 1u, &kFragShader, &frag_shader_size);

  gl_.CompileShader(vert_shader);
  gl_.CompileShader(frag_shader);

  GLint vert_status = GL_FALSE;
  GLint frag_status = GL_FALSE;

  gl_.GetShaderiv(vert_shader, GL_COMPILE_STATUS, &vert_status);
  gl_.GetShaderiv(frag_shader, GL_COMPILE_STATUS, &frag_status);

  FML_CHECK(vert_status == frag_status == GL_TRUE);

  program_ = gl_.CreateProgram();
  gl_.AttachShader(program_, vert_shader);
  gl_.AttachShader(program_, frag_shader);

  gl_.LinkProgram(program_);

  GLint link_status = GL_FALSE;
  gl_.GetProgramiv(program_, GL_LINK_STATUS, &link_status);
  FML_CHECK(link_status == GL_TRUE);

  gl_.DeleteShader(vert_shader);
  gl_.DeleteShader(frag_shader);

  is_valid_ = true;
}

TrampolineGLVK::~TrampolineGLVK() {
  if (is_valid_) {
    gl_.DeleteProgram(program_);
    program_ = GL_NONE;
  }
}

const ProcTableGLVK& TrampolineGLVK::GetProcTable() const {
  return gl_;
}

bool TrampolineGLVK::IsValid() const {
  return is_valid_;
}

bool TrampolineGLVK::CopyTexture(GLuint from_texture, GLuint to_texture) const {
  if (!is_valid_) {
    return false;
  }

  GLuint fbo = GL_NONE;
  gl_.GenFramebuffers(1u, &fbo);
  gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl_.FramebufferTexture2D(GL_FRAMEBUFFER,        //
                           GL_COLOR_ATTACHMENT0,  //
                           GL_TEXTURE_2D,         //
                           to_texture,            //
                           0                      //
  );
  FML_LOG(IMPORTANT) << "From Texture: " << from_texture
                     << " To Texture: " << to_texture;
  const auto fb_status = gl_.CheckFramebufferStatus(GL_FRAMEBUFFER);
  FML_CHECK(fb_status == GL_FRAMEBUFFER_COMPLETE)
      << "Incomplete framebuffer: " << DebugToFramebufferError(fb_status);

  gl_.Disable(GL_DITHER);

  gl_.ClearColor(0.0, 1.0, 0.0, 1.0);
  gl_.Clear(GL_COLOR_BUFFER_BIT);

  gl_.UseProgram(program_);

  static constexpr GLfloat kVertices[] = {
      0.0f,  0.5f,   //
      -0.5f, -0.5f,  //
      0.5f,  -0.5f,  //
      0.5f,  -0.5f,  //
  };
  gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_TRUE, 0, kVertices);
  gl_.EnableVertexAttribArray(0);
  gl_.DrawArrays(GL_TRIANGLE_FAN, 0, 4);

  gl_.DeleteFramebuffers(1u, &fbo);
  return true;
}

}  // namespace impeller::glvk
