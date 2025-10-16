#include "../../include/iris/texture.h"
#include "../../external/volk/volk.h"
#include "../../include/engine/format.h"
#include "../../include/engine/renderer.h"
#include "../../include/engine/sprite.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>

static inline VkImageAspectFlags
get_aspect_flags_for_format(nv_format fmt)
{
  VkImageAspectFlags aspect_flags = 0;
  if (nv_format_has_stencil_channel(fmt))
  {
    aspect_flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }
  if (nv_format_has_depth_channel(fmt))
  {
    aspect_flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  // If aspect_flags is still 0, we didn't select anything
  if (aspect_flags == 0)
  {
    // Assume color aspect then
    aspect_flags |= VK_IMAGE_ASPECT_COLOR_BIT;
  }
  return aspect_flags;
}

static inline VkResult
create_image(
    nvvk_ctx_t*        nvvkctx,
    VkImageUsageFlags  usage,
    VkImageType        image_type,
    VkImageLayout      initial_layout,
    VkSampleCountFlags samples,
    nv_extent3         extent,
    u32                array_layers,
    nv_format          format,
    bool               linear_tiling,
    VkImage*           dst)
{
  VkImageCreateInfo imageCreateInfo = nv_zero_init(VkImageCreateInfo);
  imageCreateInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType         = image_type;
  imageCreateInfo.extent            = (VkExtent3D){ extent.width, extent.height, extent.depth };
  imageCreateInfo.mipLevels         = 1;
  imageCreateInfo.arrayLayers       = array_layers;
  imageCreateInfo.format            = (VkFormat)nv_format_to_vk_format(format);
  imageCreateInfo.tiling            = !linear_tiling ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR;
  imageCreateInfo.initialLayout     = initial_layout;
  imageCreateInfo.usage             = usage;
  imageCreateInfo.samples           = (VkSampleCountFlagBits)samples;
  imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
  return nvvk_result_check(*nvvkctx, vkCreateImage(nvvkctx->device, &imageCreateInfo, &nvvkctx->vkalloc, dst));
}

nv_error
iris_texture_init(struct iris_driver* driver, const iris_texture_create_info_t* info, iris_texture_t* dst)
{
  nv_assert_else_return(iris_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->extent.width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->extent.height != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->extent.depth != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(info->flags != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(iris_texture_t));

  nvvk_ctx_t* nvvkctx = driver->vkctx;

  nv_error code = NV_SUCCESS;

  size_t alignment = info->alignment;
  alignment        = NV_MAX(alignment, IRIS_BUFFER_MINIMUM_ALIGNMENT);
  alignment        = NV_MAX(alignment, get_preferred_alignment(driver->vkctx));

  nv_format fmt = info->format;
  if (info->flags & IRIS_TEXTURE_SAMPLED_BIT)
  {
    nv_format new_fmt = nv_vk_get_supported_format_for_draw(nvvkctx, fmt);
    if (new_fmt != fmt)
      nv_log_warning("Format %s cannot be used for rendering. %s will be used", nv_format_to_string(fmt), nv_format_to_string(new_fmt));
    fmt = new_fmt;
  }

  VkImageType image_type = VK_IMAGE_TYPE_1D;
  if (info->extent.height > 1)
  {
    image_type = VK_IMAGE_TYPE_2D;
  }
  if (info->extent.depth > 1)
  {
    image_type = VK_IMAGE_TYPE_3D;
  }

  VkImageUsageFlags usage          = iris_texture_flags_to_vk_flags(info->flags);
  VkImageLayout     initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (info->extra && info->extra->initial_layout != VK_IMAGE_LAYOUT_UNDEFINED)
  {
    initial_layout = info->extra->initial_layout;
  }
  VkResult result = create_image(
      nvvkctx,
      usage,
      image_type,
      initial_layout,
      (VkSampleCountFlags)info->samples,
      info->extent,
      info->array_layers > 1 ? info->array_layers : 1,
      info->format,
      info->linear_tiling,
      &dst->handle);
  if (result != VK_SUCCESS)
  {
    return NV_ERROR_EXTERNAL;
  }

  iris_memory_flags memory_flags = IRIS_MEMORY_FLAGS_DEFAULT_BIT;
  if (info->extra != NULL && info->extra->custom_memory_flags != IRIS_MEMORY_FLAGS_DEFAULT_BIT)
  {
    memory_flags = info->extra->custom_memory_flags;
  }

  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(driver->vkctx->device, dst->handle, &memory_requirements);

  if (info->extra != NULL && info->extra->custom_memory_pool != NULL)
  {
    iris_memory_allocate(info->extra->custom_memory_pool, memory_requirements.size, memory_requirements.alignment, &dst->memory);
  }
  else
  {
    iris_memory_allocate_dedicated(driver, memory_requirements.memoryTypeBits, memory_flags, memory_requirements.size, memory_requirements.alignment, &dst->memory);
  }
  vkBindImageMemory(driver->vkctx->device, dst->handle, iris_memory_get_backing(&dst->memory), dst->memory.pool_offset);

  VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_1D;
  switch (image_type)
  {
    case VK_IMAGE_TYPE_1D: view_type = VK_IMAGE_VIEW_TYPE_1D; break;
    case VK_IMAGE_TYPE_2D: view_type = VK_IMAGE_VIEW_TYPE_2D; break;
    case VK_IMAGE_TYPE_3D: view_type = VK_IMAGE_VIEW_TYPE_3D; break;
    default:
      if ((info->flags & IRIS_TEXTURE_CUBEMAP_BIT) != 0)
      {
        view_type = VK_IMAGE_VIEW_TYPE_CUBE;
      }
      break;
  }

  VkImageViewCreateInfo imageViewCreateInfo           = nv_zero_init(VkImageViewCreateInfo);
  imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCreateInfo.image                           = dst->handle;
  imageViewCreateInfo.viewType                        = view_type;
  imageViewCreateInfo.format                          = nv_format_to_vk_format(info->format);
  imageViewCreateInfo.subresourceRange.aspectMask     = get_aspect_flags_for_format(info->format);
  imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
  imageViewCreateInfo.subresourceRange.levelCount     = 1;
  imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
  imageViewCreateInfo.subresourceRange.layerCount     = info->array_layers;
  if (nvvk_result_check(*nvvkctx, vkCreateImageView(nvvkctx->device, &imageViewCreateInfo, &nvvkctx->vkalloc, &dst->handle_view)) != VK_SUCCESS)
  {
    return NV_ERROR_EXTERNAL;
  }

  dst->driver         = driver;
  dst->flags          = info->flags;
  dst->extent         = info->extent;
  dst->mip_levels     = 1;
  dst->array_layers   = info->array_layers;
  dst->format         = info->format;
  dst->current_layout = initial_layout;
  dst->drv_destroyed  = false;
  dst->drv_in_use     = false;

  return code;
}

void
iris_texture_destroy(iris_texture_t* tex)
{
  if (!tex || !tex->driver || !tex->driver->vkctx)
  {
    return;
  }

  nvvk_ctx_t* vkctx  = tex->driver->vkctx;
  VkDevice    device = vkctx->device;

  vkDestroyImage(device, tex->handle, &vkctx->vkalloc);
  vkDestroyImageView(device, tex->handle_view, &vkctx->vkalloc);
  iris_memory_free(&tex->memory);

  nv_bzero(tex, sizeof(iris_texture_t));
}

nv_error
iris_texture_write_data(iris_texture_t* tex, const iris_texture_region_t* dst_region, const void* pixels, size_t data_size)
{
  nv_assert_else_return(tex != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(tex->driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_region != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_region->extent.width > 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_region->extent.height > 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst_region->extent.depth > 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((dst_region->offset.x + dst_region->extent.width) <= tex->extent.width, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((dst_region->offset.y + dst_region->extent.height) <= tex->extent.height, NV_ERROR_INVALID_ARG);
  nv_assert_else_return((dst_region->offset.z + dst_region->extent.depth) <= tex->extent.depth, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(pixels != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data_size > 0, NV_ERROR_INVALID_ARG);

  iris_driver_t* driver = tex->driver;
  nvvk_ctx_t*    vkctx  = tex->driver->vkctx;

  iris_buffer_t* transfer_buf = NULL;
  if (data_size < IRIS_SMALL_TRANSFER_BUFFER_SIZE)
  {
    transfer_buf = &driver->small_transfer_buffer;
  }
  else
  {
    if (data_size > iris_buffer_size(&driver->large_transfer_buffer))
    {
      iris_buffer_resize(&driver->large_transfer_buffer, data_size, driver->large_transfer_buffer.alignment, false);
    }
    transfer_buf = &driver->large_transfer_buffer;
  }

  void* mapping = NULL;
  iris_memory_map(&transfer_buf->memory, 0, data_size, &mapping);
  nv_memcpy(mapping, pixels, data_size);
  iris_memory_flush(&transfer_buf->memory);

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (iris_is_upload_batch_active(driver))
  {
    cmd = driver->active_upload_cmd;
  }
  else
  {
    cmd = nv_vk_begin_command_buffer(driver);
  }

  nv_vk_transition_image_auto(cmd, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_region->mip_level, dst_region->array_level);

  VkBufferImageCopy const region = {
    .bufferOffset      = 0,
    .bufferRowLength   = 0,
    .bufferImageHeight = 0,
    .imageSubresource =
        (VkImageSubresourceLayers){
            .aspectMask = get_aspect_flags_for_format(tex->format), .mipLevel = dst_region->mip_level, .baseArrayLayer = dst_region->array_level, .layerCount = 1 },
    .imageOffset = (VkOffset3D){ dst_region->offset.x, dst_region->offset.y, dst_region->offset.z },
    .imageExtent = (VkExtent3D){ (u32)dst_region->extent.width, (u32)dst_region->extent.height, (u32)dst_region->extent.depth },
  };
  vkCmdCopyBufferToImage(cmd, transfer_buf->handle, tex->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  nv_vk_transition_image_auto(cmd, tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, dst_region->mip_level, dst_region->array_level);

  // It's a dedicated command buffer if we haven't started an upload batch
  if (!iris_is_upload_batch_active(driver))
  {
    nv_vk_end_command_buffer(tex->driver, cmd, vkctx->transfer_queue, true);
  }

  return NV_SUCCESS;
}

VkImageUsageFlags
iris_texture_flags_to_vk_flags(iris_texture_flags flags)
{
  VkImageUsageFlags ret = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if ((flags & IRIS_TEXTURE_SAMPLED_BIT) != 0)
  {
    ret |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if ((flags & IRIS_TEXTURE_COLOR_ATTACHMENT_BIT) != 0)
  {
    ret |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  }
  if ((flags & IRIS_TEXTURE_DEPTH_ATTACHMENT_BIT) != 0)
  {
    ret |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  }
  if ((flags & IRIS_TEXTURE_RESOLVE_ATTACHMENT_BIT) != 0)
  {
    ret |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  }
  if ((flags & IRIS_TEXTURE_STORAGE_BIT) != 0)
  {
    ret |= VK_IMAGE_USAGE_STORAGE_BIT;
  }
  // if ((flags & IRIS_TEXTURE_CUBEMAP_BIT) != 0) { /* This is set during image creation time */ }

  return ret;
}

nv_error
iris_texture_copy_from_buffer(iris_texture_t* dst_texture, const iris_texture_region_t* dst_region, const iris_buffer_t* src_buffer, u32 src_buffer_offset)
{
  nv_assert_else_return(dst_texture != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src_buffer != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(dst_texture->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(src_buffer->driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(src_buffer->driver == dst_texture->driver, NV_ERROR_INVALID_ARG);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(dst_texture->driver);

  VkImageAspectFlags aspect = get_aspect_flags_for_format(dst_texture->format);

  nv_vk_transition_image_auto(cmd, dst_texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0);

  VkBufferImageCopy region = {
    .bufferOffset      = (VkDeviceSize)src_buffer_offset,
    .bufferRowLength   = dst_region->extent.width,
    .bufferImageHeight = dst_region->extent.height,
    .imageSubresource  = { .aspectMask = aspect, .mipLevel = dst_region->mip_level, .baseArrayLayer = dst_region->array_level, .layerCount = 1 },
    .imageOffset       = { dst_region->offset.x, dst_region->offset.y, dst_region->offset.z },
    .imageExtent       = { (uint32_t)dst_region->extent.width, (uint32_t)dst_region->extent.height, (uint32_t)dst_region->extent.depth },
  };

  // Do the copy. Use explicit TRANSFER_DST layout rather than relying on current_layout variable
  vkCmdCopyBufferToImage(cmd, src_buffer->handle, dst_texture->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  nv_vk_transition_image_auto(cmd, dst_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 0);

  VkQueue  graphics_queue = dst_texture->driver->vkctx->graphics_queue;
  VkResult result         = nv_vk_end_command_buffer(dst_texture->driver, cmd, graphics_queue, true);
  if (result != VK_SUCCESS)
  {
    return NV_ERROR_UNKNOWN;
  }

  return NV_SUCCESS;
}

uint32_t
nv_format_to_vk_format(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_R8: return VK_FORMAT_R8_UNORM;
    case NOVA_FORMAT_RG8: return VK_FORMAT_R8G8_UNORM;
    case NOVA_FORMAT_RGB8: return VK_FORMAT_R8G8B8_UNORM;
    case NOVA_FORMAT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;

    case NOVA_FORMAT_BGR8: return VK_FORMAT_B8G8R8_UNORM;
    case NOVA_FORMAT_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;

    case NOVA_FORMAT_RGB16: return VK_FORMAT_R16G16B16_UNORM;
    case NOVA_FORMAT_RGBA16: return VK_FORMAT_R16G16B16A16_UNORM;
    case NOVA_FORMAT_RG32: return VK_FORMAT_R32G32_SFLOAT;
    case NOVA_FORMAT_RGB32: return VK_FORMAT_R32G32B32_SFLOAT;
    case NOVA_FORMAT_RGBA32: return VK_FORMAT_R32G32B32A32_SFLOAT;

    case NOVA_FORMAT_R8_SINT: return VK_FORMAT_R8_SINT;
    case NOVA_FORMAT_RG8_SINT: return VK_FORMAT_R8G8_SINT;
    case NOVA_FORMAT_RGB8_SINT: return VK_FORMAT_R8G8B8_SINT;
    case NOVA_FORMAT_RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;

    case NOVA_FORMAT_R8_UINT: return VK_FORMAT_R8_UINT;
    case NOVA_FORMAT_RG8_UINT: return VK_FORMAT_R8G8_UINT;
    case NOVA_FORMAT_RGB8_UINT: return VK_FORMAT_R8G8B8_UINT;
    case NOVA_FORMAT_RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;

    case NOVA_FORMAT_R8_SRGB: return VK_FORMAT_R8_SRGB;
    case NOVA_FORMAT_RG8_SRGB: return VK_FORMAT_R8G8_SRGB;
    case NOVA_FORMAT_RGB8_SRGB: return VK_FORMAT_R8G8B8_SRGB;
    case NOVA_FORMAT_RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;

    case NOVA_FORMAT_BGR8_SRGB: return VK_FORMAT_B8G8R8_SRGB;
    case NOVA_FORMAT_BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;

    case NOVA_FORMAT_D16: return VK_FORMAT_D16_UNORM;
    case NOVA_FORMAT_D24: return VK_FORMAT_D24_UNORM_S8_UINT;
    case NOVA_FORMAT_D32: return VK_FORMAT_D32_SFLOAT;
    case NOVA_FORMAT_D24_S8: return VK_FORMAT_D24_UNORM_S8_UINT;
    case NOVA_FORMAT_D32_S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;

    case NOVA_FORMAT_BC1: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case NOVA_FORMAT_BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case NOVA_FORMAT_BC7: return VK_FORMAT_BC7_UNORM_BLOCK;

    default: return VK_FORMAT_UNDEFINED;
  }
}

nv_format
nv_format_from_vk_format(VkFormat_ format)
{
  switch (format)
  {
    case VK_FORMAT_R8_UNORM: return NOVA_FORMAT_R8;
    case VK_FORMAT_R8G8_UNORM: return NOVA_FORMAT_RG8;
    case VK_FORMAT_R8G8B8_UNORM: return NOVA_FORMAT_RGB8;
    case VK_FORMAT_R8G8B8A8_UNORM: return NOVA_FORMAT_RGBA8;

    case VK_FORMAT_B8G8R8_UNORM: return NOVA_FORMAT_BGR8;
    case VK_FORMAT_B8G8R8A8_UNORM: return NOVA_FORMAT_BGRA8;

    case VK_FORMAT_R16G16B16_UNORM: return NOVA_FORMAT_RGB16;
    case VK_FORMAT_R16G16B16A16_UNORM: return NOVA_FORMAT_RGBA16;
    case VK_FORMAT_R32G32_SFLOAT: return NOVA_FORMAT_RG32;
    case VK_FORMAT_R32G32B32_SFLOAT: return NOVA_FORMAT_RGB32;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return NOVA_FORMAT_RGBA32;

    case VK_FORMAT_R8_SINT: return NOVA_FORMAT_R8_SINT;
    case VK_FORMAT_R8G8_SINT: return NOVA_FORMAT_RG8_SINT;
    case VK_FORMAT_R8G8B8_SINT: return NOVA_FORMAT_RGB8_SINT;
    case VK_FORMAT_R8G8B8A8_SINT: return NOVA_FORMAT_RGBA8_SINT;

    case VK_FORMAT_R8_SRGB: return NOVA_FORMAT_R8_SRGB;
    case VK_FORMAT_R8G8_SRGB: return NOVA_FORMAT_RG8_SRGB;
    case VK_FORMAT_R8G8B8_SRGB: return NOVA_FORMAT_RGB8_SRGB;
    case VK_FORMAT_R8G8B8A8_SRGB: return NOVA_FORMAT_RGBA8_SRGB;

    case VK_FORMAT_B8G8R8_SRGB: return NOVA_FORMAT_BGR8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return NOVA_FORMAT_BGRA8_SRGB;

    case VK_FORMAT_R8_UINT: return NOVA_FORMAT_R8_UINT;
    case VK_FORMAT_R8G8_UINT: return NOVA_FORMAT_RG8_UINT;
    case VK_FORMAT_R8G8B8_UINT: return NOVA_FORMAT_RGB8_UINT;
    case VK_FORMAT_R8G8B8A8_UINT: return NOVA_FORMAT_RGBA8_UINT;

    case VK_FORMAT_D16_UNORM: return NOVA_FORMAT_D16;
    case VK_FORMAT_D32_SFLOAT: return NOVA_FORMAT_D32;
    case VK_FORMAT_D24_UNORM_S8_UINT: return NOVA_FORMAT_D24_S8;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return NOVA_FORMAT_D32_S8;

    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return NOVA_FORMAT_BC1;
    case VK_FORMAT_BC3_UNORM_BLOCK: return NOVA_FORMAT_BC3;
    case VK_FORMAT_BC7_UNORM_BLOCK: return NOVA_FORMAT_BC7;

    default: return NOVA_FORMAT_UNDEFINED;
  }
}

VkImageView
iris_texture_get_image_view(const iris_texture_t* tex)
{
  return (tex) ? tex->handle_view : VK_NULL_HANDLE;
}

VkImage
iris_texture_get_image(const iris_texture_t* tex)
{
  return (tex) ? tex->handle : VK_NULL_HANDLE;
}

nv_format
iris_texture_get_format(const iris_texture_t* tex)
{
  return (tex) ? tex->format : NOVA_FORMAT_UNDEFINED;
}

void
nv_vk_transition_image_auto(VkCommandBuffer cmd, iris_texture_t* tex, VkImageLayout newLayout, uint32_t mip_level, uint32_t array_level)
{
  VkImageMemoryBarrier barrier = { .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                   .oldLayout           = tex->current_layout,
                                   .newLayout           = newLayout,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image               = tex->handle,
                                   .subresourceRange    = {
                                          .aspectMask     = get_aspect_flags_for_format(tex->format),
                                          .baseMipLevel   = mip_level,
                                          .levelCount     = tex->mip_levels,
                                          .baseArrayLayer = array_level,
                                          .layerCount     = tex->array_layers,
                                   }, };

  VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

  // Map oldLayout → srcAccessMask + srcStage
  switch (tex->current_layout)
  {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      barrier.srcAccessMask = 0;
      srcStageMask          = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      srcStageMask          = VK_PIPELINE_STAGE_TRANSFER_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      srcStageMask          = VK_PIPELINE_STAGE_TRANSFER_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      srcStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      srcStageMask          = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
      srcStageMask          = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      break;
    default:
      barrier.srcAccessMask = 0;
      srcStageMask          = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      break;
  }

  // Map newLayout → dstAccessMask + dstStage
  switch (newLayout)
  {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      dstStageMask          = VK_PIPELINE_STAGE_TRANSFER_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      dstStageMask          = VK_PIPELINE_STAGE_TRANSFER_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      dstStageMask          = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
      dstStageMask          = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      dstStageMask          = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
      break;
    default:
      barrier.dstAccessMask = 0;
      dstStageMask          = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
      break;
  }

  vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, 0, 0, NULL, 0, NULL, 1, &barrier);

  tex->current_layout = newLayout;
}