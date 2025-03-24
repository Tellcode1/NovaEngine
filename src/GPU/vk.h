#ifndef __NOVA_VK_H__
#define __NOVA_VK_H__

// implementation: vk.c

#include "../../external/volk/volk.h"
#include "../common/format.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

// pointer to allocator
#ifndef NOVA_VK_ALLOCATOR
#  define NOVA_VK_ALLOCATOR NULL
#endif

typedef struct nvvk_context_t
{
  VkInstance               instance;
  VkDevice                 device;
  VkPhysicalDevice         phys_device;
  VkSurfaceKHR             surface;
  struct SDL_Window*       window;
  VkDebugUtilsMessengerEXT debug_messenger;

  nv_format swap_chain_image_format;
  u32       swap_chain_color_space;
  u32       swap_chain_image_count;
  u32       samples;

  u32 graphics_family_index;
  u32 present_family_index;
  u32 compute_family_index;
  u32 transfer_family_index;
  u32 graphics_and_compute_family_index;

  VkQueue graphics_queue;
  VkQueue graphics_and_compute_queue;
  VkQueue present_queue;
  VkQueue compute_queue;
  VkQueue transfer_queue;

  u32           MAX_SAMPLES;
  unsigned char SUPPORTS_MULTISAMPLING;
  flt_t         MAX_ANISOTROPY;
} nvvk_context_t;

extern nvvk_context_t nvvk_context;

extern void nvvk_context_initialize(nvvk_context_t* ctx);

extern const char* nvvk_vk_result_to_string(VkResult r);

// YOU SAW NOTHING

extern u32 nv_vk_get_mem_type(const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties);

/* externallyAllocated = true asserts *dstMemory will not be written to by this function */
extern void
nv_vk_create_buffer(size_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer* dstBuffer, VkDeviceMemory* dstMemory, bool externallyAllocated);

/*  */
extern void nv_vk_stage_buffer_transfer(VkBuffer dst, void* data, size_t size);

/* src Must be a valid VkCommandBuffer */
extern VkCommandBuffer nv_vk_begin_command_buffer_from(VkCommandBuffer src);

/* BeginSingleTimeCommands(new CommandBuffer) */
extern VkCommandBuffer nv_vk_begin_command_buffer(void);

/* WARNING: waitForExecution = false implies you take responsibility of freeing the commandBuffer! */
extern VkResult nv_vk_end_command_buffer(VkCommandBuffer cmd, VkQueue queue, bool waitForExecution);

extern void nv_vk_stage_image_transfer(VkImage dst, const void* data, size_t width, size_t height, size_t image_size);

extern void nv_vk_create_texture_from_memory(u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem);

extern u8* nv_vk_create_texture_from_disk(const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem);

extern void nv_vk_create_texture_empty(
    u32 width, u32 height, nv_format format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, size_t* image_size, VkImage* dst, VkDeviceMemory* dstMem);

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

extern nv_format nv_vk_get_supported_format_for_draw(nv_format fmt);

extern bool nv_vk_get_supported_format(VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dstFormat, VkColorSpaceKHR* dstColorSpace);

extern u32 nv_vk_get_surface_image_count(VkPhysicalDevice phys_device, VkSurfaceKHR surface);

extern void nv_vk_load_binary_file(const char* path, u8* dst, u32* dstSize);

NOVA_HEADER_END

#endif //__NOVA_VK_H__
