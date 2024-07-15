// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/surface_texture_external_texture_vk_impeller.h"

#include <chrono>

#include <GLES2/gl2.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>

#include "flutter/fml/trace_event.h"
#include "flutter/impeller/display_list/dl_image_impeller.h"
#include "flutter/impeller/renderer/backend/vulkan/command_buffer_vk.h"
#include "flutter/impeller/renderer/backend/vulkan/command_encoder_vk.h"
#include "flutter/impeller/renderer/backend/vulkan/surface_context_vk.h"
#include "flutter/impeller/renderer/backend/vulkan/texture_vk.h"
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

static bool SetTextureLayoutSync(const ContextVK& context,
                                 const TextureSourceVK* texture,
                                 vk::ImageLayout layout) {
  if (!texture) {
    return true;
  }
  auto command_buffer = context.CreateCommandBuffer();
  if (!command_buffer) {
    VALIDATION_LOG
        << "Could not create command buffer for texture layout update.";
    return false;
  }
  command_buffer->SetLabel("GLVKTextureLayoutUpdateCB");
  const auto& encoder = CommandBufferVK::Cast(*command_buffer).GetEncoder();
  const auto command_buffer_vk = encoder->GetCommandBuffer();

  BarrierVK barrier;
  barrier.cmd_buffer = command_buffer_vk;
  barrier.new_layout = layout;
  barrier.src_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                      impeller::vk::PipelineStageFlagBits::eFragmentShader;
  barrier.src_access = vk::AccessFlagBits::eColorAttachmentWrite |
                       vk::AccessFlagBits::eShaderRead;
  barrier.dst_stage = impeller::vk::PipelineStageFlagBits::eFragmentShader;
  barrier.dst_access = vk::AccessFlagBits::eShaderRead;

  if (!texture->SetLayout(barrier).ok()) {
    VALIDATION_LOG << "Could not encoder layout transition.";
    return false;
  }

  encoder->EndCommandBuffer();

  vk::SubmitInfo submit_info;
  submit_info.setCommandBuffers(command_buffer_vk);

  // There is no need to track the fence in the encoder since we are going to do
  // a sync wait for completion.
  auto fence = context.GetDevice().createFenceUnique(vk::FenceCreateFlags{});
  if (fence.result != impeller::vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not create fence.";
    return false;
  }

  if (context.GetGraphicsQueue()->Submit(submit_info, fence.value.get()) !=
      impeller::vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not submit layout transition fence.";
    return false;
  }

  using namespace std::chrono_literals;

  if (context.GetDevice().waitForFences(
          fence.value.get(), VK_TRUE,
          std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count()) !=
      impeller::vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not perform sync wait on fence.";
    return false;
  }

  return true;
}

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
  const auto& surface_context =
      SurfaceContextVK::Cast(*context.aiks_context->GetContext());
  const auto& context_vk = ContextVK::Cast(*surface_context.GetParent());

  if (!egl_context_->MakeCurrent(*egl_surface_)) {
    VALIDATION_LOG
        << "Could not make the context current for external texture interop.";
    return;
  }

  auto texture = GetCachedTexture(
      context_vk, ISize::MakeWH(bounds.width(), bounds.height()));

  fml::ScopedCleanupClosure clear_context(
      [&]() { egl_context_->ClearCurrent(); });

  GLuint external_texture = {};
  glGenTextures(1u, &external_texture);
  Attach(external_texture);
  Update();

  SetTextureLayoutSync(context_vk, texture.get(),
                       vk::ImageLayout::eColorAttachmentOptimal);

  trampoline_->CopyTexture(external_texture, texture->GetGLTextureHandle());

  SetTextureLayoutSync(context_vk, texture.get(),
                       vk::ImageLayout::eShaderReadOnlyOptimal);

  glDeleteTextures(1u, &external_texture);

  dl_image_ = DlImageImpeller::Make(
      std::make_shared<TextureVK>(surface_context.GetParent(), texture));
}

// |SurfaceTextureExternalTexture|
void SurfaceTextureExternalTextureVKImpeller::Detach() {
  SurfaceTextureExternalTexture::Detach();
  cached_texture_vk_.reset();
}

std::shared_ptr<impeller::glvk::TextureSourceGLVK>
SurfaceTextureExternalTextureVKImpeller::GetCachedTexture(
    const ContextVK& context,
    const impeller::ISize& size) {
  if (cached_texture_vk_ &&
      cached_texture_vk_->GetTextureDescriptor().size == size) {
    return cached_texture_vk_;
  }
  cached_texture_vk_ = nullptr;
  auto texture = std::make_shared<glvk::TextureSourceGLVK>(context,      //
                                                           trampoline_,  //
                                                           size          //
  );
  if (!texture->IsValid()) {
    VALIDATION_LOG << "Could not create trampoline texture.";
    return nullptr;
  }
  cached_texture_vk_ = std::move(texture);
  return cached_texture_vk_;
}

}  // namespace flutter
