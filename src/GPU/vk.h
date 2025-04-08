#ifndef __NOVA_VK_H__
#define __NOVA_VK_H__

// implementation: vk.c

#include "../common/format.h"
#include "../external/volk/volk.h"
#include "../std/errorcodes.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

struct nv_ctx_t;

// pointer to allocator
#ifndef NOVA_VK_ALLOCATOR
#  define NOVA_VK_ALLOCATOR NULL
#endif

/* Return the result, but if it was handled, return whatever you want. */
typedef VkResult (*nv_gpu_result_check_fn)(const VkResult result, const char* __restrict__ FILE, const char* __restrict__ FUNC, unsigned long LINE);

/* did you notice that the vulkan context is entirely independant of the global context? */
/* beauty. */
typedef struct nvvk_ctx_t
{
  VkInstance               instance;
  VkDevice                 device;
  VkPhysicalDevice         phys_device;
  VkSurfaceKHR             surface;
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

  u32           max_samples;
  unsigned char supports_multisampling;
  flt_t         max_anisotropy;

  VkCommandPool   cmd_pool;
  VkCommandBuffer buffer;

  // To not cause NULLptr dereference.
  // SetResultCheckFunc also checks for NULLptr
  // and handles it.
  nv_gpu_result_check_fn result_fn;
  u32                    flag_register;
} nvvk_ctx_t;

extern nv_errorc nvvk_ctx_init(struct nv_ctx_t* nvctx, nvvk_ctx_t* ctx);
extern void      nvvk_ctx_destroy(nvvk_ctx_t* ctx);

extern const char* nvvk_vk_result_to_string(VkResult r);

// YOU SAW NOTHING

extern u32 nv_vk_get_mem_type(nvvk_ctx_t* nvvkctx, const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties);

/* externallyAllocated = true asserts *dstMemory will not be written to by this function */
extern void nv_vk_create_buffer(
    nvvk_ctx_t*           ctx,
    size_t                size,
    VkBufferUsageFlags    usageFlags,
    VkMemoryPropertyFlags propertyFlags,
    VkBuffer*             dstBuffer,
    VkDeviceMemory*       dstMemory,
    bool                  externallyAllocated);

/*  */
extern void nv_vk_stage_buffer_transfer(nvvk_ctx_t* ctx, VkBuffer dst, void* data, size_t size);

/* src Must be a valid VkCommandBuffer */
extern VkCommandBuffer nv_vk_begin_command_buffer_from(VkCommandBuffer src);

/* BeginSingleTimeCommands(new CommandBuffer) */
extern VkCommandBuffer nv_vk_begin_command_buffer(nvvk_ctx_t* ctx);

/* WARNING: waitForExecution = false implies you take responsibility of freeing the commandBuffer! */
extern VkResult nv_vk_end_command_buffer(nvvk_ctx_t* ctx, VkCommandBuffer cmd, VkQueue queue, bool waitForExecution);

extern void nv_vk_stage_image_transfer(nvvk_ctx_t* ctx, VkImage dst, const void* data, size_t width, size_t height, size_t image_size);

extern void nv_vk_create_texture_from_memory(nvvk_ctx_t* ctx, u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem);

extern u8* nv_vk_create_texture_from_disk(nvvk_ctx_t* ctx, const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem);

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

extern nv_format nv_vk_get_supported_format_for_draw(nvvk_ctx_t* nvvkctx, nv_format fmt);

extern bool nv_vk_get_supported_format(nvvk_ctx_t* nvvkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dstFormat, VkColorSpaceKHR* dstColorSpace);

extern u32 nv_vk_get_surface_image_count(nvvk_ctx_t* nvvkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface);

extern void nv_vk_load_binary_file(const char* path, u8* dst, u32* dstSize);

NOVA_HEADER_END

#endif //__NOVA_VK_H__
