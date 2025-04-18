#ifndef __NOVA_GPU_DYNAMIC_BUFFER_H__
#define __NOVA_GPU_DYNAMIC_BUFFER_H__

#include "../std/stdafx.h"
#include "types.h"

NOVA_HEADER_START

struct nvvk_driver_t;

#ifndef NOVA_VK_DRIVER_BUFFER_REGION_SAMPLE_TIME_INTERVAL_SECONDS
/**
 * The time interval that the driver uses to measure the average reads and writes to a region.
 */
#  define NOVA_VK_DRIVER_BUFFER_REGION_SAMPLE_TIME_INTERVAL_SECONDS 10.0F
#endif

/**
 * A buffer from which 'regions' can be allocated.
 * These regions can contain similar data which changes on similar frequencies.
 * Or to allow for multiple smaller buffers to take host on a single VkBuffer instance.
 *
 * This is an interface over a simple memory allocator.
 */
typedef struct nv_gpu_dynamic_buffer_t nv_gpu_dynamic_buffer_t;

/**
 * A region of a buffer. Note that regions are modified a lot and so to combat that:
 * You have to lock the dynamic buffer for a small time to modify a region.
 */
typedef struct nv_gpu_dynamic_buffer_region_t nv_gpu_dynamic_buffer_region_t;

typedef u32 nv_gpu_dynamic_buffer_flags;
typedef enum nv_gpu_dynamic_buffer_flags_bits
{
  /**
   * The data stored in the buffer is REQUIRED.
   * This typically means that the buffer is accessed externally (not by the nv_gpu API) and the driver
   * won't skip or delay unneeded transfers.
   * Disables many optimizations by the driver.
   */
  NV_GPU_DYNAMIC_BUFFER_VOLATILE_BIT = 1 << 0,

  /**
   * The data in the buffer does not matter, it is only used as an intermediary
   * So, the driver can use this buffer when the user isn't using it to provide for
   * other buffers without creating new ones.
   * NOTE: Volatility doesn't apply to other buffers that are swapped out to this buffer. Only this buffer is affected.
   */
  NV_GPU_DYNAMIC_BUFFER_TRANSIENT_BIT = 1 << 1,

  /**
   * The buffer is resizable as needed.
   * If the buffer is non transient, then the data from the old buffer is copied over
   * If the buffer is non volatile, transfers may be outright avoided or delayed until necessary.
   * Note that this does not mean that regions in the buffer may resize, only that the parent buffer can resize or not.
   */
  NV_GPU_DYNAMIC_BUFFER_RESIZABLE_BIT = 1 << 2,

  /**
   * The memory can be read back to the CPU side from the GPU.
   */
  NV_GPU_DYNAMIC_BUFFER_READBACK_CAPABLE_BIT = 1 << 4,

  NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED = 1 << 5,

  /**
   * The buffer is *cabable* of providing vertices to the GPU
   * Note that this doesn't mean that the buffer is necessarily a providing vertices to the GPU currently.
   */
  NV_GPU_DYNAMIC_BUFFER_VERTEX_BUFFER_BIT = 1 << 6,

  NV_GPU_DYNAMIC_BUFFER_INDEX_BUFFER_BIT = 1 << 7,

  /**
   * Shader storage buffer. Generally used for compute workloads and output
   */
  NV_GPU_DYNAMIC_BUFFER_SS_BUFFER_BIT = 1 << 8,

  /**
   * A uniform buffer. Support has yet to be added for dynamic uniform buffers.
   * Uniform buffers imply persistent mapping.
   */
  NV_GPU_DYNAMIC_BUFFER_UNIFORM_BUFFER_BIT = (1 << 9) | NV_GPU_DYNAMIC_BUFFER_PERSISTENT_MAPPED,

  /**
   * The buffer is visible to the CPU, i.e. the CPU has fast writing access to the buffer.
   */
  NV_GPU_DYNAMIC_BUFFER_CPU_VISIBLE = 1 << 10,

} nv_gpu_dynamic_buffer_flags_bits;

typedef u32 nv_gpu_dynamic_buffer_region_flags;
typedef enum nv_gpu_dynamic_buffer_region_flags_bits
{
  /**
   * The region pointed to is only written once and read multiple times
   * This isn't necessary, the driver tracks the amount of times you've
   * wrote and read data and operates by that. But best to let the driver
   * know your intentions.
   */
  NV_GPU_DYNAMIC_BUFFER_REGION_CONST = 1 << 0,

  /**
   * The region is asynchronously transferring data to the GPU.
   * Note that this applies to the *only* a region, not the entire buffer.
   * This does mean that the region will be thrown here and there by the driver for fastest writing access.
   * Also note that the driver might swap out the backing memory entirely. no guarantees.
   */
  NV_GPU_DYNAMIC_BUFFER_REGION_STREAMING_DATA_BIT = 1 << 1,

  /**
   * Simple: The region is aligned
   */
  NV_GPU_DYNAMIC_BUFFER_REGION_ALIGNED_BIT = 1 << 2,
} nv_gpu_dynamic_buffer_region_flags_bits;

struct nv_gpu_dynamic_buffer_region_t
{
  nv_gpu_dynamic_buffer_t* parent;

  /* The (aligned) size of this region in the buffer */
  vk_size_t size;

  /* The (aligned) offset of this region into the buffer */
  vk_size_t offset;

  /* This will be 1 when not set, not 0!! */
  size_t alignment;

  /* user data. Anything like an index or an id/type that the user might want to store in a region */
  u64 ud;

  /* The info used by the driver. Do not modify yourselves */
  size_t drv_avg_writes; // average number of writes across the time interval specified as a define.
  size_t drv_avg_reads;
  real_t drv_last_sample_time;
};

struct nv_gpu_dynamic_buffer_t
{
  /* everything in this struct is readonly! */

  struct nvvk_driver_t* driver;

  nv_gpu_dynamic_buffer_flags flags;

  /* The total (aligned) size of this buffer */
  vk_size_t size;

  size_t alignment;

  /* The VkBuffer handle */
  VkBuffer buffer;

  /**
   * Note that the buffer owns this memory.
   * The freelist operates on this memory.
   */
  VkDeviceMemory memory;

  // nv_gpu_buffer_freelist_t freelist;

  /* Driver stored information. Do not modify! */

  // whether the buffer has been detroyed or not
  bool drv_destroyed;

  /**
   * if the buffer is in use by anything.
   * note that this isn't really accurate, its set even if the buffer is just in a recording
   * that hasn't been submit.
   * Note that this is always set if the buffer is volatile.
   */
  bool drv_in_use;

  /**
   * Whether the buffer can ONLY be used for a transfer.
   * So, a VkBuffer is almost never created and instead the driver will just use this flag
   * and use another buffer for transfers when needed. However, if no buffer is free for
   * transfers when it was needed, then a new buffer is created and this flag is NOT SET.
   */
  bool drv_transfer_only;

  /**
   * If the buffer is a transfer only buffer, then this contains a pointer to the actual
   * buffer being used.
   */
  void* drv_payload;

  /**
   * Really only used for uniform buffers || Persistent mapped buffers
   */
  void*     drv_mapped;
  vk_size_t drv_mapped_size;   // the size of the mapping
  vk_size_t drv_mapped_offset; // the offset of the mapping
};

/**
 * The contents of the buffer will NOT be initialized
 */
extern nv_errorc nv_gpu_dynamic_buffer_init(struct nvvk_driver_t* driver, vk_size_t size, size_t alignment, nv_gpu_dynamic_buffer_flags flags, nv_gpu_dynamic_buffer_t* dst);
extern void      nv_gpu_dynamic_buffer_destroy(nv_gpu_dynamic_buffer_t* buffer);

/**
 * Note that writes to the buffer aren't visible immediately.
 * This is more so a limitation of every graphics API.
 */
extern nv_errorc nv_gpu_dynamic_buffer_write_data(nv_gpu_dynamic_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset);

/**
 * It's perfectly valid to try to map persisten buffers
 * The mapping will just contain the buffer's mapping with the offset
 */
extern nv_errorc nv_gpu_dynamic_buffer_map_memory(nv_gpu_dynamic_buffer_t* buffer, vk_size_t size, vk_size_t offset, void** mapping);

/**
 * It is illegal to try to unmap persistent buffers
 */
extern nv_errorc nv_gpu_dynamic_buffer_unmap_memory(nv_gpu_dynamic_buffer_t* buffer);

/**
 * Note that dst must have been allocated with atleast 'size' bytes of memory.
 * Also, it is legal to read back a non readback optimized buffer. It'll just be slow.
 * And no, the driver won't notice you're reading back a non optimized buffer and replace it.
 * By defualt, this function will wait for the readback to finish. An asynchronous function is yet to be implemented.
 */
extern nv_errorc nv_gpu_dynamic_buffer_readback(nv_gpu_dynamic_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst);

NOVA_HEADER_END

#endif //__NOVA_GPU_DYNAMIC_BUFFER_H__
