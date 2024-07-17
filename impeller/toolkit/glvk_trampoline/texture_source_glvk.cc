// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/glvk_trampoline/texture_source_glvk.h"

#include "impeller/base/config.h"
#include "impeller/renderer/backend/vulkan/allocator_vk.h"
#include "impeller/renderer/backend/vulkan/capabilities_vk.h"
#include "impeller/renderer/backend/vulkan/formats_vk.h"

namespace impeller::glvk {

static TextureDescriptor CreateTextureDescriptor(ISize size) {
  TextureDescriptor desc;
  desc.storage_mode = StorageMode::kDevicePrivate;
  desc.type = TextureType::kTexture2D;
  desc.format = PixelFormat::kR8G8B8A8UNormInt;
  desc.size = size;
  desc.mip_count = 1u;
  desc.usage = TextureUsage::kRenderTarget;
  desc.sample_count = SampleCount::kCount1;
  desc.compression_type = CompressionType::kLossless;
  return desc;
}

TextureSourceGLVK::TextureSourceGLVK(const ContextVK& context,
                                     std::shared_ptr<TrampolineGLVK> trampoline,
                                     ISize size)
    : TextureSourceVK(CreateTextureDescriptor(size)),
      trampoline_(std::move(trampoline)) {
  if (!trampoline_ || !trampoline_->IsValid()) {
    VALIDATION_LOG << "Invalid trampoline.";
    return;
  }

  if (!CapabilitiesVK::Cast(*context.GetCapabilities())
           .HasExtension(OptionalDeviceExtensionVK::kKHRExternalMemoryFd)) {
    VALIDATION_LOG << "External FD extension not supported/enabled.";
    return;
  }

  const auto& device = context.GetDevice();
  const auto& physical_device = context.GetPhysicalDevice();

  //----------------------------------------------------------------------------
  // Step 1: Create an unbound image whose handle can be exported.
  //

  vk::StructureChain<vk::ImageCreateInfo,               //
                     vk::ExternalMemoryImageCreateInfo  //
                     >
      image_chain;

  auto& image_info = image_chain.get();
  image_info.flags = {};
  image_info.imageType = vk::ImageType::e2D;
  image_info.format = ToVKImageFormat(desc_.format);
  image_info.extent = vk::Extent3D{
      static_cast<uint32_t>(size.width),   // width
      static_cast<uint32_t>(size.height),  // height
      1u                                   // depth
  };
  image_info.mipLevels = 1u;
  image_info.arrayLayers = 1u;
  image_info.samples = vk::SampleCountFlagBits::e1;
  image_info.tiling = vk::ImageTiling::eLinear;
  image_info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                     vk::ImageUsageFlagBits::eSampled;
  image_info.sharingMode = vk::SharingMode::eExclusive;
  image_info.initialLayout = vk::ImageLayout::eUndefined;

  auto& external_memory_info =
      image_chain.get<vk::ExternalMemoryImageCreateInfo>();
  external_memory_info.handleTypes =
      vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;

  auto image = device.createImageUnique(image_chain.get());
  if (image.result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not create image.";
    return;
  }

  //----------------------------------------------------------------------------
  // Step 2: Get the image memory requirements and allocate the memory directly.
  // Due to the extensions use, VMA may not be used.
  //

  auto available_device_properties = physical_device.getMemoryProperties();
  auto memory_requirements =
      device.getImageMemoryRequirements(image.value.get());
  auto memory_type_index = AllocatorVK::FindMemoryTypeIndex(
      memory_requirements.memoryTypeBits, available_device_properties);
  if (memory_type_index < 0) {
    VALIDATION_LOG << "Could not find the memory type of external image.";
    return;
  }

  vk::StructureChain<vk::MemoryAllocateInfo,          //
                     vk::ExportMemoryAllocateInfo,    //
                     vk::MemoryDedicatedAllocateInfo  //
                     >
      memory_chain;

  auto& memory_info = memory_chain.get<vk::MemoryAllocateInfo>();
  memory_info.allocationSize = memory_requirements.size;
  memory_info.memoryTypeIndex = memory_type_index;

  auto& export_memory_info = memory_chain.get<vk::ExportMemoryAllocateInfo>();
  export_memory_info.handleTypes =
      vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;

  auto& dedicated_memory_info =
      memory_chain.get<vk::MemoryDedicatedAllocateInfo>();
  dedicated_memory_info.image = image.value.get();

  auto device_memory = device.allocateMemoryUnique(memory_chain.get());

  if (device_memory.result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not create handle exportable device memory.";
    return;
  }

  //----------------------------------------------------------------------------
  // Step 3: Bind the device memory we just created to the newly created image.
  //

  auto bind_result =
      device.bindImageMemory(image.value.get(), device_memory.value.get(), 0u);
  if (bind_result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not bind external device memory to image.";
    return;
  }

  //----------------------------------------------------------------------------
  // Step 4: Create an image view. This is just regular Vulkan.
  //

  vk::ImageViewCreateInfo view_info;
  view_info.image = image.value.get();
  view_info.viewType = vk::ImageViewType::e2D;
  view_info.format = ToVKImageFormat(desc_.format);
  view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  view_info.subresourceRange.levelCount = 1u;
  view_info.subresourceRange.layerCount = 1u;

  auto image_view = device.createImageViewUnique(view_info);
  if (image_view.result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not create image view for GLVK texture source: "
                   << vk::to_string(image_view.result);
    return;
  }

  //----------------------------------------------------------------------------
  // Step 5: Export a file descriptor from the exportable memory.
  //
  // @warning   In the Vulkan and OpenGL extensions, Get/Import/Export
  //            transport ownership of the file descriptor.
  //

  vk::MemoryGetFdInfoKHR fd_info;
  fd_info.handleType = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
  fd_info.memory = device_memory.value.get();

  // On success, the FD will be owned by the application and it is its
  // responsibility to call ::close on it. But we are going to give it
  // immediately to the GL counterpart.
  auto fd = device.getMemoryFdKHR(fd_info);
  if (fd.result != vk::Result::eSuccess) {
    VALIDATION_LOG << "Could not fetch FD from texture memory for GL import: "
                   << vk::to_string(fd.result);
    return;
  }

  //----------------------------------------------------------------------------
  // Step 6: Create an OpenGL handle from the exported Vulkan file handle.
  //
  // @warning    After this point is spooky GL land with non-RAII handles and
  //             dodgy GL error era error handling. There may be no early
  //             returns till success.
  //

  const auto& gl = trampoline_->GetProcTable();

  GLuint gl_memory = GL_NONE;
  gl.CreateMemoryObjectsEXT(1u, &gl_memory);
  // From the spec: "A successful import operation transfers ownership of `fd`
  // to the GL implementation."

  gl.ImportMemoryFdEXT(gl_memory,                     //
                       memory_requirements.size,      //
                       GL_HANDLE_TYPE_OPAQUE_FD_EXT,  //
                       fd.value                       //
  );

  GLuint gl_texture = GL_NONE;
  gl.GenTextures(1u, &gl_texture);
  gl.BindTexture(GL_TEXTURE_2D, gl_texture);
  gl.TexStorageMem2DEXT(GL_TEXTURE_2D,  // target
                        1u,             // levels
                        GL_RGBA8,       // internal format
                        size.width,     // width
                        size.height,    // height
                        gl_memory,      // memory
                        0u              // offset
  );

  gl.BindTexture(GL_TEXTURE_2D, GL_NONE);

  device_memory_ = std::move(device_memory.value);
  image_ = std::move(image.value);
  image_view_ = std::move(image_view.value);
  gl_texture_ = gl_texture;
  gl_memory_ = gl_memory;
  is_valid_ = true;

#ifdef IMPELLER_DEBUG
  context.SetDebugName(device_memory_.get(), "GLVK Device Memory");
  context.SetDebugName(image_.get(), "GLVK Image");
  context.SetDebugName(image_view_.get(), "GLVK ImageView");
#endif  // IMPELLER_DEBUG
}

TextureSourceGLVK::~TextureSourceGLVK() {
  if (is_valid_) {
    const auto& gl = trampoline_->GetProcTable();
    gl.DeleteTextures(1u, &gl_texture_);
    gl.DeleteMemoryObjectsEXT(1u, &gl_memory_);
    gl_texture_ = GL_NONE;
    gl_memory_ = GL_NONE;
  }
}

vk::Image TextureSourceGLVK::GetImage() const {
  return image_.get();
}

vk::ImageView TextureSourceGLVK::GetImageView() const {
  return image_view_.get();
}

vk::ImageView TextureSourceGLVK::GetRenderTargetView() const {
  return GetImageView();
}

bool TextureSourceGLVK::IsSwapchainImage() const {
  return false;
}

bool TextureSourceGLVK::IsValid() const {
  return is_valid_;
}

GLuint TextureSourceGLVK::GetGLTextureHandle() const {
  return gl_texture_;
}

}  // namespace impeller::glvk
