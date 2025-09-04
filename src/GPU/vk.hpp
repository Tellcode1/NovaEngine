#ifndef __NOVA_VK_H__
#define __NOVA_VK_H__

// implementation: vk.c

#include "../external/volk/volk.h"

#include "../engine/format.hpp"
#include "../std/stdafx.h"
#include "../std/types.h"
#include "driver.hpp"
#include "types.hpp"

#include <SDL3/SDL_vulkan.h>

/**
 * THIS LIBRARY IS DEPRECATED. DO NOT USE.
 * All functions will be ported over. Until then, just shut up and sit down in the corner.
 */

struct nvvk_driver;

namespace lr
{
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
extern VkCommandBuffer nv_vk_begin_command_buffer(nvvk_driver* driver);

/* WARNING: waitForExecution = false implies you take responsibility of freeing the commandBuffer! */
extern VkResult nv_vk_end_command_buffer(nvvk_driver* driver, VkCommandBuffer cmd, VkQueue queue, bool waitForExecution);

extern void nv_vk_stage_image_transfer(nvvk_driver* driver, VkImage dst, const void* data, size_t width, size_t height, size_t image_size);

extern void nv_vk_create_texture_from_memory(nvvk_driver* driver, u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem);

extern u8* nv_vk_create_texture_from_disk(nvvk_driver* driver, const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem);

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

}

#endif //__NOVA_VK_H__
