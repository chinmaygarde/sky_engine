// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_ANDROID_SURFACE_TEXTURE_EXTERNAL_TEXTURE_VK_IMPELLER_H_
#define FLUTTER_SHELL_PLATFORM_ANDROID_SURFACE_TEXTURE_EXTERNAL_TEXTURE_VK_IMPELLER_H_

#include <memory>

#include "flutter/impeller/renderer/backend/vulkan/context_vk.h"
#include "flutter/impeller/toolkit/egl/context.h"
#include "flutter/impeller/toolkit/egl/display.h"
#include "flutter/impeller/toolkit/egl/surface.h"
#include "flutter/impeller/toolkit/glvk_trampoline/texture_source_glvk.h"
#include "flutter/impeller/toolkit/glvk_trampoline/trampoline_glvk.h"
#include "flutter/shell/platform/android/surface_texture_external_texture.h"

namespace flutter {

class SurfaceTextureExternalTextureVKImpeller final
    : public SurfaceTextureExternalTexture {
 public:
  SurfaceTextureExternalTextureVKImpeller(
      std::shared_ptr<impeller::ContextVK> context,
      int64_t id,
      const fml::jni::ScopedJavaGlobalRef<jobject>& surface_texture,
      const std::shared_ptr<PlatformViewAndroidJNI>& jni_facade);

  ~SurfaceTextureExternalTextureVKImpeller() override;

  SurfaceTextureExternalTextureVKImpeller(
      const SurfaceTextureExternalTextureVKImpeller&) = delete;

  SurfaceTextureExternalTextureVKImpeller& operator=(
      const SurfaceTextureExternalTextureVKImpeller&) = delete;

 private:
  std::shared_ptr<impeller::ContextVK> context_;
  std::unique_ptr<impeller::egl::Display> egl_display_;
  std::unique_ptr<impeller::egl::Context> egl_context_;
  std::unique_ptr<impeller::egl::Surface> egl_surface_;
  std::shared_ptr<impeller::glvk::TrampolineGLVK> trampoline_;
  std::shared_ptr<impeller::glvk::TextureSourceGLVK> cached_texture_vk_;
  bool is_valid_ = false;

  // |SurfaceTextureExternalTexture|
  void ProcessFrame(PaintContext& context, const SkRect& bounds) override;

  // |SurfaceTextureExternalTexture|
  void Detach() override;

  std::shared_ptr<impeller::glvk::TextureSourceGLVK> GetCachedTexture(
      const impeller::ContextVK& context,
      const impeller::ISize& size);
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_ANDROID_SURFACE_TEXTURE_EXTERNAL_TEXTURE_VK_IMPELLER_H_
