// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/surface_texture_external_texture_vk_impeller.h"

#include <GLES2/gl2.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>

#include "flutter/impeller/renderer/backend/vulkan/surface_context_vk.h"
#include "flutter/impeller/toolkit/glvk_trampoline/texture_source_glvk.h"

namespace flutter {

using namespace impeller;

SurfaceTextureExternalTextureVKImpeller::
    SurfaceTextureExternalTextureVKImpeller(
        std::shared_ptr<impeller::ContextVK> context,
        int64_t id,
        const fml::jni::ScopedJavaGlobalRef<jobject>& surface_texture,
        const std::shared_ptr<PlatformViewAndroidJNI>& jni_facade)
    : SurfaceTextureExternalTexture(id, surface_texture, jni_facade),
      context_(std::move(context)) {
  // We are going to be talking to surface textures that only understand OpenGL
  // APIs. But we don't need or have an OpenGL context when rendering with
  // Vulkan. Create a context with a 1x1 pbuffer surface for this purpose.

  egl_display_ = std::make_unique<egl::Display>();
  if (!egl_display_->IsValid()) {
    VALIDATION_LOG
        << "Could not create EGL display for external texture interop.";
    return;
  }

  egl::ConfigDescriptor config_desc;
  config_desc.api = egl::API::kOpenGLES2;
  config_desc.samples = egl::Samples::kOne;
  config_desc.color_format = egl::ColorFormat::kRGBA8888;
  config_desc.stencil_bits = egl::StencilBits::kZero;
  config_desc.depth_bits = egl::DepthBits::kZero;
  config_desc.surface_type = egl::SurfaceType::kPBuffer;
  auto config = egl_display_->ChooseConfig(config_desc);
  if (!config) {
    VALIDATION_LOG
        << "Could not choose EGL config for external texture interop.";
    return;
  }

  egl_surface_ = egl_display_->CreatePixelBufferSurface(*config, 1u, 1u);
  egl_context_ = egl_display_->CreateContext(*config, nullptr);

  if (!egl_surface_ || !egl_context_) {
    VALIDATION_LOG << "Could not create EGL surface and/or context for "
                      "external texture interop.";
    return;
  }

  // Make the context current so the proc addresses can be resolved.
  if (!egl_context_->MakeCurrent(*egl_surface_)) {
    VALIDATION_LOG << "Could not make the context current.";
    return;
  }

  fml::ScopedCleanupClosure clear_context(
      [&]() { egl_context_->ClearCurrent(); });

  trampoline_ =
      std::make_shared<glvk::TrampolineGLVK>(egl::CreateProcAddressResolver());

  if (!trampoline_->IsValid()) {
    VALIDATION_LOG << "Could not create valid trampoline.";
    return;
  }

  is_valid_ = true;
}

SurfaceTextureExternalTextureVKImpeller::
    ~SurfaceTextureExternalTextureVKImpeller() = default;

// |SurfaceTextureExternalTexture|
void SurfaceTextureExternalTextureVKImpeller::ProcessFrame(
    PaintContext& context,
    const SkRect& bounds) {
  if (!is_valid_) {
    VALIDATION_LOG << "Invalid external texture.";
    return;
  }

  if (!context.aiks_context ||
      context.aiks_context->GetContext()->GetBackendType() !=
          impeller::Context::BackendType::kVulkan) {
    VALIDATION_LOG << "Invalid context.";
    return;
  }

  // TODO(csg): These casts are extremely dodgy after the introduction of the
  // surface context. Make this easier to reconcile. Perhaps by removing the
  // need for a surface context.
  const auto& context_vk = ContextVK::Cast(
      *SurfaceContextVK::Cast(*context.aiks_context->GetContext()).GetParent());

  if (!egl_context_->MakeCurrent(*egl_surface_)) {
    VALIDATION_LOG
        << "Could not make the context current for external texture interop.";
    return;
  }

  fml::ScopedCleanupClosure clear_context(
      [&]() { egl_context_->ClearCurrent(); });

  GLuint external_texture = {};
  glGenTextures(1u, &external_texture);
  Attach(external_texture);
  Update();
  glDeleteTextures(1u, &external_texture);

  // TODO(csg): Cache this by the size of the texture.
  auto texture = std::make_shared<glvk::TextureSourceGLVK>(
      context_vk,                                     //
      trampoline_,                                    //
      ISize::MakeWH(bounds.width(), bounds.height())  //
  );

  if (!texture->IsValid()) {
    VALIDATION_LOG << "Could not create trampoline texture.";
    return;
  }
}

// |SurfaceTextureExternalTexture|
void SurfaceTextureExternalTextureVKImpeller::Detach() {}

}  // namespace flutter
