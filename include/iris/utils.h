
#ifndef NOVA_VK_H
#define NOVA_VK_H

#include "../../external/volk/volk.h"
#include "../engine/format.h"
#include "../std/include/types.h"
#include "driver.h"
#include "types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * THIS LIBRARY IS DEPRECATED. DO NOT USE.
   * All functions will be ported over. Until then, just shut up and sit down in the corner.
   */

  struct iris_driver;

  static inline iris_size_t
  max_elem(const iris_size_t* arr, iris_size_t count)
  {
    /* some vulkan implementations have some values as 0, so we can't use 0 here */
    iris_size_t max = 1;
    for (iris_size_t i = 0; i < count; i++)
    {
      if (arr[i] > max)
      {
        max = arr[i];
      }
    }
    return max;
  }

  static inline iris_size_t
  get_preferred_alignment(const nvvk_ctx_t* vkctx)
  {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vkctx->phys_device, &props);

    const iris_size_t alignment_list[] = {
      props.limits.nonCoherentAtomSize,             // for host-visible memory alignment (especially if not host coherent)
      props.limits.bufferImageGranularity,          // required if buffers and images are in same memory
      props.limits.minUniformBufferOffsetAlignment, // for dynamic UBOs
      props.limits.minStorageBufferOffsetAlignment, // for dynamic SSBOs

      // not necessarily needed, just optimal
      // these raise the required alignment on my pc to 128 from 64
      // so we don't use them
      // props.limits.optimalBufferCopyOffsetAlignment,   // for copy src/dst offsets
      // props.limits.optimalBufferCopyRowPitchAlignment, // for row pitch in image copies
    };

    return max_elem(alignment_list, nv_arrlen(alignment_list));
  }

  extern const char* nvvk_vk_result_to_string(VkResult r);

  // YOU SAW NOTHING

  extern u32 nv_vk_get_mem_type(nvvk_ctx_t* vkctx, const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties);

  /* externallyAllocated = true asserts *dstMemory will not be written to by this function */
  extern void nv_vk_create_buffer(
      nvvk_ctx_t*           ctx,
      size_t                size,
      VkBufferUsageFlags    usageFlags,
      VkMemoryPropertyFlags propertyFlags,
      VkBuffer*             dstBuffer,
      VkDeviceMemory*       dstMemory,
      bool                  externallyAllocated);

  /* src Must be a valid VkCommandBuffer */
  extern VkCommandBuffer nv_vk_begin_command_buffer_from(VkCommandBuffer src);

  /* BeginSingleTimeCommands(new CommandBuffer) */
  extern VkCommandBuffer nv_vk_begin_command_buffer(iris_driver_t* driver);

  /* WARNING: waitForExecution = false implies you take responsibility of freeing the commandBuffer! */
  extern VkResult nv_vk_end_command_buffer(iris_driver_t* driver, VkCommandBuffer cmd, VkQueue queue, bool waitForExecution);

  extern void nv_vk_stage_image_transfer(iris_driver_t* driver, VkImage dst, const void* data, size_t width, size_t height, size_t image_size);

  extern void nv_vk_create_texture_from_memory(iris_driver_t* driver, u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem);

  extern u8* nv_vk_create_texture_from_disk(iris_driver_t* driver, const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem);

  extern void nv_vk_create_texture_empty(
      nvvk_ctx_t*           ctx,
      u32                   width,
      u32                   height,
      nv_format             format,
      VkSampleCountFlagBits samples,
      VkImageUsageFlags     usage,
      size_t*               image_size,
      VkImage*              dst,
      VkDeviceMemory*       dstMem);

  extern void nv_vk_insert_texture_layout_transition(
      VkCommandBuffer       cmd,
      VkImage               image,
      u32                   mipLevels,
      VkImageAspectFlagBits aspect,
      VkImageLayout         oldLayout,
      VkImageLayout         newLayout,
      VkAccessFlags         srcAccessMask,
      VkAccessFlags         dstAccessMask,
      VkPipelineStageFlags  sourceStage,
      VkPipelineStageFlags  destinationStage);

  extern nv_format nv_vk_get_supported_format_for_draw(nvvk_ctx_t* vkctx, nv_format fmt);

  extern bool nv_vk_get_supported_format(nvvk_ctx_t* vkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dstFormat, VkColorSpaceKHR* dstColorSpace);

  extern u32 nv_vk_get_surface_image_count(nvvk_ctx_t* vkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface);

  /**
   * What the fuck is this doing here?
   */
  extern void nv_vk_load_binary_file(const char* path, u8* dst, u32* dstSize);

#ifdef __cplusplus
}
#endif

#endif // NOVA_VK_H
