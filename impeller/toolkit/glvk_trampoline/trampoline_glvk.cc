// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/glvk_trampoline/trampoline_glvk.h"

#include <array>

#include "impeller/base/validation.h"
#include "impeller/geometry/point.h"
#include "impeller/renderer/backend/gles/formats_gles.h"
#include "impeller/toolkit/glvk_trampoline/texture_source_glvk.h"

namespace impeller::glvk {

static GLuint kAttributePositionIndex = 0u;
static GLuint kAttributeTexCoordIndex = 1u;

static constexpr const char* kVertShader = R"IMPELLER_SHADER(#version 100

precision mediump float;

attribute vec2 aPosition;
attribute vec2 aTexCoord;

varying vec2 vTexCoord;

void main() {
  gl_Position = vec4(aPosition, 0.0, 1.0);
  vTexCoord = aTexCoord;
}
)IMPELLER_SHADER";

static constexpr const char* kFragShader = R"IMPELLER_SHADER(#version 100

#extension GL_OES_EGL_image_external : require

precision mediump float;

uniform samplerExternalOES uTexture;

varying vec2 vTexCoord;

void main() {
  gl_FragColor = mix(texture2D(uTexture, vTexCoord), vec4(0.0, 0.0, 0.0, 1.0), 0.0);
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

  gl_.BindAttribLocation(program_, kAttributePositionIndex, "aPosition");
  gl_.BindAttribLocation(program_, kAttributeTexCoordIndex, "aTexCoord");

  gl_.LinkProgram(program_);

  GLint link_status = GL_FALSE;
  gl_.GetProgramiv(program_, GL_LINK_STATUS, &link_status);
  FML_CHECK(link_status == GL_TRUE);

  texture_uniform_location_ = gl_.GetUniformLocation(program_, "uTexture");

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

bool TrampolineGLVK::CopyTexture(GLuint from_texture,
                                 const TextureSourceGLVK& to_texture) const {
  if (!is_valid_) {
    return false;
  }

  const auto& fb_size = to_texture.GetTextureDescriptor().size;

  GLuint fbo = GL_NONE;
  gl_.GenFramebuffers(1u, &fbo);
  gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);
  gl_.FramebufferTexture2D(GL_FRAMEBUFFER,                   //
                           GL_COLOR_ATTACHMENT0,             //
                           GL_TEXTURE_2D,                    //
                           to_texture.GetGLTextureHandle(),  //
                           0                                 //
  );

  FML_CHECK(gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE);

  gl_.Disable(GL_DITHER);
  gl_.Disable(GL_BLEND);
  gl_.Disable(GL_SCISSOR_TEST);
  gl_.Disable(GL_CULL_FACE);
  gl_.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  gl_.ClearColor(1.0, 0.0, 1.0, 1.0);
  gl_.Clear(GL_COLOR_BUFFER_BIT);
  gl_.Viewport(0, fb_size.height, fb_size.width, 0);

  gl_.UseProgram(program_);

  gl_.BindBuffer(GL_ARRAY_BUFFER, GL_NONE);
  gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_NONE);

  static constexpr const std::array<Point, 4> kPositions = {
      Point{0.5f, 0.5f},    // top right
      Point{0.5f, -0.5f},   // bottom right
      Point{-0.5f, -0.5f},  // bottom left
      Point{-0.5f, 0.5f},   // top left
  };
  gl_.EnableVertexAttribArray(kAttributePositionIndex);
  gl_.VertexAttribPointer(kAttributePositionIndex, 2, GL_FLOAT, GL_FALSE, 0,
                          kPositions.data());

  static constexpr const std::array<Point, 4> kTextureCoords = {
      Point{1.0f, 1.0f},  // top right
      Point{1.0f, 0.0f},  // bottom right
      Point{0.0f, 0.0f},  // bottom left
      Point{0.0f, 1.0f},  // top left
  };
  static_assert(kPositions.size() == kTextureCoords.size());

  gl_.EnableVertexAttribArray(kAttributeTexCoordIndex);
  gl_.VertexAttribPointer(kAttributeTexCoordIndex, 2, GL_FLOAT, GL_FALSE, 0,
                          kTextureCoords.data());

  gl_.ActiveTexture(GL_TEXTURE0);
  gl_.BindTexture(GL_TEXTURE_EXTERNAL_OES, from_texture);
  gl_.TexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl_.TexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl_.TexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
  gl_.TexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
  gl_.Uniform1i(texture_uniform_location_, 0u);

  static constexpr std::array<GLubyte, 6> kIndices = {1, 2, 3, 3, 0, 1};
  gl_.DrawElements(GL_TRIANGLES, kIndices.size(), GL_UNSIGNED_BYTE,
                   kIndices.data());

  gl_.Flush();

  gl_.DeleteFramebuffers(1u, &fbo);

  return true;
}

}  // namespace impeller::glvk
