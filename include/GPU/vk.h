#ifndef __NOVA_VK_H__
#define __NOVA_VK_H__

// implementation: vk.c

#include "../../common/containers/dynarray.h"
#include "../../common/mem.h"
#include "../../std/stdafx.h"
#include "../../external/volk/volk.h"
#include "../../common/format.h"

NOVA_HEADER_START;

// pointer to allocator
#ifndef NOVA_VK_ALLOCATOR
#define NOVA_VK_ALLOCATOR NULL
#endif

extern const char* nvvk_vk_result_to_string(VkResult r);

// YOU SAW NOTHING

extern u32 nv_vk_get_mem_type(const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties);

/* externallyAllocated = true asserts *dstMemory will not be written to by this function */
extern void nv_vk_create_buffer(
    size_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer* dstBuffer, VkDeviceMemory* dstMemory, bool externallyAllocated);

/*  */
extern void nv_vk_stage_buffer_transfer(VkBuffer dst, void* data, size_t size);

/* src Must be a valid VkCommandBuffer */
extern VkCommandBuffer nv_vk_begin_command_buffer_from(VkCommandBuffer src);

/* BeginSingleTimeCommands(new CommandBuffer) */
extern VkCommandBuffer nv_vk_begin_command_buffer();

/* WARNING: waitForExecution = false implies you take responsibility of freeing the commandBuffer! */
extern VkResult nv_vk_end_command_buffer(VkCommandBuffer cmd, VkQueue queue, bool waitForExecution);

extern void     nv_vk_stage_image_transfer(VkImage dst, const void* data, int width, int height, int image_size);

extern void     nv_vk_create_texture_from_memory(u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem);

extern u8*      nv_vk_create_texture_from_disk(const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem);

extern void     nv_vk_create_texture_empty(
        u32 width, u32 height, nv_format format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, int* image_size, VkImage* dst, VkDeviceMemory* dstMem);

extern void nv_vk_transition_texture_layout(VkCommandBuffer cmd, VkImage image, u32 mipLevels, VkImageAspectFlagBits aspect, VkImageLayout oldLayout, VkImageLayout newLayout,
    VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage);

extern nv_format nv_vk_get_supported_format_for_draw(nv_format fmt);

extern bool      nv_vk_get_supported_format(VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dstFormat, VkColorSpaceKHR* dstColorSpace);

extern u32       nv_vk_get_surface_image_count(VkPhysicalDevice phys_device, VkSurfaceKHR surface);

extern void      nv_vk_load_binary_file(const char* path, u8* dst, u32* dstSize);

NOVA_HEADER_END;

#endif //__NOVA_VK_H__