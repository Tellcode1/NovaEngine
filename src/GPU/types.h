#ifndef __NOVA_GPU_TYPES_H__
#define __NOVA_GPU_TYPES_H__

#include "../external/volk/volk.h"
#include "../std/errorcodes.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

#ifndef NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT
#  define NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT 8
#endif

typedef uint64_t vk_size_t;

/* Return the result, but if it was handled, return whatever you want. */
typedef VkResult (*nv_gpu_result_check_fn)(const VkResult result, const char* FILE, const char* FUNC, unsigned long LINE);

struct nv_ctx;

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

typedef struct nv_extent2d_s
{
  size_t width, height;
} nv_extent2d;

typedef struct nv_extent3D_s
{
  size_t width, height, depth;
} nv_extent3D;

typedef struct nvvk_allocator_block_s
{
  vk_size_t size;
  vk_size_t offset;
} nvvk_allocator_block_t;

#ifndef NOVA_VK_ALLOCATOR_L1_CACHE_LENGTH
#  define NOVA_VK_ALLOCATOR_L1_CACHE_LENGTH 1024
#endif

#ifndef NOVA_VK_ALLOCATOR_L1_CACHE_BLOCK_LENGTH
#  define NOVA_VK_ALLOCATOR_L1_CACHE_BLOCK_LENGTH 128
#endif

#define NOVA_VK_ALLOCATOR_L1_CACHE_NUM_BLOCKS (NOVA_VK_ALLOCATOR_L1_CACHE_LENGTH / NOVA_VK_ALLOCATOR_L1_CACHE_BLOCK_LENGTH)

#ifndef NOVA_VK_ALLOCATOR_L2_CACHE_PAGE_SIZE
#  define NOVA_VK_ALLOCATOR_L2_CACHE_PAGE_SIZE 16384
#endif

#ifndef NOVA_VK_ALLOCATOR_L2_CACHE_MAX_PAGES_ALLOCATED
#  define NOVA_VK_ALLOCATOR_L2_CACHE_MAX_PAGES_ALLOCATED 32
#endif

#ifndef NOVA_VK_ALLOCATOR_COMMAND_PAGE_SIZE
#  define NOVA_VK_ALLOCATOR_COMMAND_PAGE_SIZE 1024
#endif

#ifndef NOVA_VK_ALLOCATOR_COMMAND_PAGE_ALIGNMENT
#  define NOVA_VK_ALLOCATOR_COMMAND_PAGE_ALIGNMENT 128
#endif

typedef uchar nvvk_allocator_l1_cache_block_t[NOVA_VK_ALLOCATOR_L1_CACHE_BLOCK_LENGTH];

typedef uchar nvvk_allocator_command_page_t[NOVA_VK_ALLOCATOR_COMMAND_PAGE_SIZE];

typedef struct nvvk_allocator_s
{
  // I think this can be compressed down to a single byte but ok
  bool l1_cache_blocks_in_use[NOVA_VK_ALLOCATOR_L1_CACHE_NUM_BLOCKS];

  /**
   * 8 blocks of 128 bytes each for fast access
   * Note that allocations may NOT use multiple blocks
   */
  nvvk_allocator_l1_cache_block_t l1_cache_blocks[NOVA_VK_ALLOCATOR_L1_CACHE_NUM_BLOCKS];

  uchar* command_page;
  size_t command_page_num_allocations;
  size_t command_page_bumper;

  char padding_x8sdf[7];

  /**
   * 16KiB pages of memory that's stack allocated out of
   * These pages are routinely cleared
   * If non-NULL, points to the page
   */
  uchar* l2_pages[NOVA_VK_ALLOCATOR_L2_CACHE_MAX_PAGES_ALLOCATED];

  /**
   * When this reaches 0 for any page, it's cleared
   */
  size_t l2_pages_num_allocations[NOVA_VK_ALLOCATOR_L2_CACHE_MAX_PAGES_ALLOCATED];

  size_t l2_page_bumpers[NOVA_VK_ALLOCATOR_L2_CACHE_MAX_PAGES_ALLOCATED];

  /**
   * The L3 cache is the slowest, but most spacious heap available to the allocator
   * It's allocated in 1MiB increments and has a freelist operating on top of it
   */
  void*  l3_cache;
  size_t l3_cache_size;
} nvvk_allocator_t;

/* did you notice that the vulkan context is entirely independant of the global context? */
/* beauty. */
typedef struct nvvk_ctx
{
  VkInstance               instance;
  VkDevice                 device;
  VkPhysicalDevice         phys_device;
  VkSurfaceKHR             surface;
  VkDebugUtilsMessengerEXT debug_messenger;

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

  VkCommandPool cmd_pool;

  VkCommandBuffer cmd_buffers[NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT];
  VkFence         cmd_buffer_fences[NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT];
  bool            cmd_buffers_in_use[NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT];

  nv_gpu_result_check_fn result_fn;
  u32                    flag_register;

  VkAllocationCallbacks vkalloc;
  nvvk_allocator_t      allocator;
} nvvk_ctx_t;

extern nv_error nvvk_ctx_init(struct nv_ctx* nvctx, nvvk_ctx_t* ctx);
extern void     nvvk_ctx_destroy(nvvk_ctx_t* ctx);

static inline bool
nvvk_ctx_is_valid(const nvvk_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, false);
  nv_assert_else_return(ctx->instance != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->device != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->phys_device != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->surface != VK_NULL_HANDLE, false);

#ifdef DEBUG
  nv_assert_else_return(ctx->debug_messenger != VK_NULL_HANDLE, false);
#endif

  nv_assert_else_return(ctx->graphics_queue != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->graphics_and_compute_queue != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->present_queue != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->compute_queue != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->transfer_queue != VK_NULL_HANDLE, false);
  nv_assert_else_return(ctx->max_samples != 0, false);

  nv_assert_else_return(ctx->result_fn != NULL, false);
  return true;
}

extern nv_error nvvk_allocator_init(nvvk_allocator_t* dst);
extern void     nvvk_allocator_destroy(nvvk_allocator_t* alloc);

extern void* nvvk_alloc(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
extern void* nvvk_realloc(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope);
extern void  nvvk_free(void* pUserData, void* pMemory);
extern void  nvvk_internal_allocation(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);
extern void  nvvk_internal_free(void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope);

NOVA_HEADER_END

#endif //__NOVA_GPU_TYPES_H__
