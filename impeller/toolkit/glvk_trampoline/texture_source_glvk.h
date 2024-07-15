// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_GL_TEXTURE_SOURCE_VK_H_
#define FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_GL_TEXTURE_SOURCE_VK_H_

#include "impeller/renderer/backend/vulkan/context_vk.h"
#include "impeller/renderer/backend/vulkan/texture_source_vk.h"
#include "impeller/renderer/backend/vulkan/vk.h"
#include "impeller/toolkit/glvk_trampoline/trampoline_glvk.h"

namespace impeller::glvk {

class TextureSourceGLVK final : public TextureSourceVK {
 public:
  TextureSourceGLVK(const ContextVK& context,
                    std::shared_ptr<TrampolineGLVK> trampoline,
                    ISize size);

  // |TextureSourceVK|
  ~TextureSourceGLVK() override;

  TextureSourceGLVK(const TextureSourceGLVK&) = delete;

  TextureSourceGLVK& operator=(const TextureSourceGLVK&) = delete;

  bool IsValid() const;

  GLuint GetGLTextureHandle() const;

 private:
  std::shared_ptr<TrampolineGLVK> trampoline_;
  vk::UniqueDeviceMemory device_memory_;
  vk::UniqueImage image_;
  vk::UniqueImageView image_view_;
  GLuint gl_texture_ = {};
  GLuint gl_memory_ = {};
  bool is_valid_ = false;

  // |TextureSourceVK|
  vk::Image GetImage() const override;

  // |TextureSourceVK|
  vk::ImageView GetImageView() const override;

  // |TextureSourceVK|
  vk::ImageView GetRenderTargetView() const override;

  // |TextureSourceVK|
  bool IsSwapchainImage() const override;
};

}  // namespace impeller::glvk

#endif  // FLUTTER_IMPELLER_RENDERER_BACKEND_VULKAN_GL_TEXTURE_SOURCE_VK_H_
