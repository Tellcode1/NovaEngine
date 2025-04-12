#ifndef __NOVA_GPU_TYPES_H__
#define __NOVA_GPU_TYPES_H__

#include "../external/volk/volk.h"
#include "../std/errorcodes.h"
#include "../std/format.h"
#include "../std/stdafx.h"
#include <vulkan/vulkan_core.h>

NOVA_HEADER_START

/* Return the result, but if it was handled, return whatever you want. */
typedef VkResult (*nv_gpu_result_check_fn)(const VkResult result, const char* FILE, const char* FUNC, unsigned long LINE);

struct nv_ctx_t;

/* Why the fuck are the three enums in GPU/????? */
typedef enum nv_window_flag_bits
{
  NOVA_WINDOW_VSYNC         = 1 << 0,
  NOVA_WINDOW_RESIZABLE     = 1 << 1,
  NOVA_WINDOW_BORDERLESS    = 1 << 2,
  NOVA_WINDOW_FULLSCREEN    = 1 << 3,
  NOVA_WINDOW_MAXIMIZED     = 1 << 4,
  NOVA_WINDOW_MINIMIZED     = 1 << 5,
  NOVA_WINDOW_HIDDEN        = 1 << 6,
  NOVA_WINDOW_HIGH_DPI      = 1 << 7,
  NOVA_WINDOW_ALWAYS_ON_TOP = 1 << 8,
} nv_window_flag_bits;

typedef enum nv_window_v_sync_bits
{
  NOVA_WINDOW_VSYNC_DISABLED = 0,
  NOVA_WINDOW_VSYNC_ENABLED  = 1
} nv_window_v_sync_bits;
typedef bool nv_window_vsync;

typedef enum nv_buffer_mode_bits
{
  NOVA_BUFFER_MODE_SINGLE_BUFFERED = 0,
  NOVA_BUFFER_MODE_DOUBLE_BUFFERED = 1,
  NOVA_BUFFER_MODE_TRIPLE_BUFFERED = 2,
} nv_buffer_mode_bits;
typedef unsigned nv_buffer_mode;

typedef enum nv_sample_count_bits
{
  NOVA_SAMPLE_COUNT_MAX_SUPPORTED    = 0x7FFFFFFF,
  NOVA_SAMPLE_COUNT_NO_EXTRA_SAMPLES = 1,
  NOVA_SAMPLE_COUNT_1_SAMPLES        = 1,
  NOVA_SAMPLE_COUNT_2_SAMPLES        = 2,
  NOVA_SAMPLE_COUNT_4_SAMPLES        = 4,
  NOVA_SAMPLE_COUNT_8_SAMPLES        = 8,
  NOVA_SAMPLE_COUNT_16_SAMPLES       = 16,
  NOVA_SAMPLE_COUNT_32_SAMPLES       = 32,
} nv_sample_count_bits;
typedef unsigned nv_sample_count;

typedef struct nv_extent2d
{
  size_t width, height;
} nv_extent2d;

typedef struct nv_extent3D
{
  size_t width, height, depth;
} nv_extent3D;

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

  u32   max_samples;
  bool  supports_multisampling;
  flt_t max_anisotropy;

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

static inline bool
nvvk_ctx_is_valid(const nvvk_ctx_t* ctx)
{
  nv_assert_and_ret(ctx != NULL, false);
  nv_assert_and_ret(ctx->instance != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->device != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->phys_device != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->surface != VK_NULL_HANDLE, false);

#ifdef DEBUG
  nv_assert_and_ret(ctx->debug_messenger != VK_NULL_HANDLE, false);
#endif

  nv_assert_and_ret(ctx->swap_chain_image_format != NOVA_FORMAT_UNDEFINED, false);
  nv_assert_and_ret(ctx->swap_chain_color_space != VK_COLOR_SPACE_MAX_ENUM_KHR, false);
  nv_assert_and_ret(ctx->graphics_queue != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->graphics_and_compute_queue != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->present_queue != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->compute_queue != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->transfer_queue != VK_NULL_HANDLE, false);
  nv_assert_and_ret(ctx->max_samples != 0, false);
  /* these two are not initialized when this function is first called */
  /*
    nv_assert_and_ret(ctx->cmd_pool != VK_NULL_HANDLE, false);
    nv_assert_and_ret(ctx->buffer != VK_NULL_HANDLE, false);
  */
  nv_assert_and_ret(ctx->result_fn != NULL, false);
  return true;
}

NOVA_HEADER_END

#endif //__NOVA_GPU_TYPES_H__
