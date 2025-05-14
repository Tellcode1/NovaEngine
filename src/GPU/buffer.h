#ifndef __NOVA_GPU_BUFFER_H__
#define __NOVA_GPU_BUFFER_H__

#include "../std/stdafx.h"
#include "allocator.h"
#include "newmemory.h"
#include "types.h"

NOVA_HEADER_START

struct nvvk_driver;
struct nv_gpu_memory_new;

#ifndef NOVA_VK_DRIVER_BUFFER_REGION_SAMPLE_TIME_INTERVAL_SECONDS
/**
 * The time interval that the driver uses to measure the average reads and writes to a region.
 */
#  define NOVA_VK_DRIVER_BUFFER_REGION_SAMPLE_TIME_INTERVAL_SECONDS 10.0F
#endif

#ifndef NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE
#  define NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE 256
#endif

#ifndef NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE
#  define NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE 3000 // 3 MB
#endif

#ifndef NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS
#  define NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS (NV_GPU_BUFFER_PERSISTENT_MAPPED | NV_GPU_BUFFER_MAPPABLE | NV_GPU_BUFFER_TRANSIENT_BIT)
#endif

#ifndef NOVA_GPU_LARGE_TRANSFER_BUFFER_CREATE_FLAGS
#  define NOVA_GPU_LARGE_TRANSFER_BUFFER_CREATE_FLAGS NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS
#endif

#ifndef NOVA_GPU_SMALL_TRANSFER_BUFFER_ALIGNMENT
#  define NOVA_GPU_SMALL_TRANSFER_BUFFER_ALIGNMENT 8
#endif

#ifndef NOVA_GPU_LARGE_TRANSFER_BUFFER_ALIGNMENT
#  define NOVA_GPU_LARGE_TRANSFER_BUFFER_ALIGNMENT 16
#endif

#ifndef NOVA_GPU_BUFFER_MINIMUM_ALIGNMENT
#  define NOVA_GPU_BUFFER_MINIMUM_ALIGNMENT 1
#endif

/**
 *
 */
typedef struct nv_gpu_buffer nv_gpu_buffer_t;

typedef u32 nv_gpu_buffer_flags;
typedef enum nv_gpu_buffer_flags_bits
{
  /**
   * The data stored in the buffer is REQUIRED.
   * This typically means that the buffer is accessed externally (not by the nv_gpu API) and the driver
   * won't skip or delay unneeded transfers.
   * Disables many optimizations by the driver.
   */
  NV_GPU_BUFFER_VOLATILE_BIT = 1 << 0,

  /**
   * The data in the buffer does not matter, it is only used as an intermediary
   * So, the driver can use this buffer when the user isn't using it to provide for
   * other buffers without creating new ones.
   * NOTE: Volatility doesn't apply to other buffers that are swapped out to this buffer. Only this buffer is affected.
   */
  NV_GPU_BUFFER_TRANSIENT_BIT = 1 << 1,

  /**
   * The buffer is resizable as needed.
   * If the buffer is non transient, then the data from the old buffer is copied over
   * If the buffer is non volatile, transfers may be outright avoided or delayed until necessary.
   * Note that this does not mean that regions in the buffer may resize, only that the parent buffer can resize or not.
   */
  NV_GPU_BUFFER_RESIZABLE_BIT = 1 << 2,

  /**
   * The memory can be read back to the CPU side from the GPU.
   * The buffer must not be transient.
   */
  NV_GPU_BUFFER_READBACK_OPTIMAL_BIT = 1 << 3,

  /**
   * The buffer is *cabable* of providing vertices to the GPU
   * Note that this doesn't mean that the buffer is necessarily a providing vertices to the GPU currently.
   */
  NV_GPU_BUFFER_VERTEX_BUFFER_BIT = 1 << 4,

  NV_GPU_BUFFER_INDEX_BUFFER_BIT = 1 << 5,

  /**
   * Shader storage buffer. Generally used for compute workloads and output
   */
  NV_GPU_BUFFER_SS_BUFFER_BIT = 1 << 6,

  /**
   * The buffer is visible to the CPU, i.e. the CPU has fast writing access to the buffer.
   */
  NV_GPU_BUFFER_MAPPABLE = 1 << 7,

  /**
   * Implies CPU visiblity (obviously.)
   */
  NV_GPU_BUFFER_PERSISTENT_MAPPED = (1 << 8) | NV_GPU_BUFFER_MAPPABLE,

  /**
   * A uniform buffer. Support has yet to be added for dynamic uniform buffers.
   * Uniform buffers imply persistent mapping.
   */
  NV_GPU_BUFFER_UNIFORM_BUFFER_BIT = (1 << 9) | NV_GPU_BUFFER_PERSISTENT_MAPPED,

} nv_gpu_buffer_flags_bits;

struct nv_gpu_buffer
{
  /* everything in this struct is readonly! */

  struct nvvk_driver* driver;

  u64 user_data; // read/write

  nv_gpu_buffer_flags flags;

  /* The total (aligned) size of this buffer */
  vk_size_t size;

  vk_size_t alignment;

  /* The VkBuffer handle */
  VkBuffer buffer;

  nv_gpu_memory_block_t block;

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

  char padding_cl2x3[5];

  /**
   * Really only used for uniform buffers || Persistent mapped buffers
   */
  void*     drv_mapped;
  vk_size_t drv_mapped_size;   // the size of the mapping
  vk_size_t drv_mapped_offset; // the offset of the mapping
};

/**
 * The contents of the buffer will NOT be initialized
 * WARNING: For buffers with multiple backings, only the size of ONE must be specified.
 */
extern nv_error nv_gpu_buffer_init(struct nvvk_driver* driver, vk_size_t size, size_t alignment, nv_gpu_buffer_flags flags, nv_gpu_buffer_t* dst);
extern void     nv_gpu_buffer_destroy(nv_gpu_buffer_t* buffer);

/**
 * Note that writes to the buffer aren't visible immediately.
 * This is more so a limitation of every graphics API.
 * Note that this may map the memory and you will need to flush the writes
 * TODO: write only when needed.
 */
extern nv_error nv_gpu_buffer_write_data(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset);

/**
 * Make all CPU writes visible to the GPU
 * Always call this after you're done writing data to the GPU and before
 * rendering or buffer reads.
 * Note that if no writes had been performed, this function will
 * simply exit and do nothing. So, it is safe to call this on a per frame
 * basis.
 */
extern nv_error nv_gpu_buffer_flush_writes(nv_gpu_buffer_t* buffer);

/**
 * It's perfectly valid to try to map persisten buffers
 * The mapping will just contain the buffer's mapping with the offset
 * Reading from a transient buffer is undefined. Only writing is valid.
 * TODO: Add optimizations to mapping like asynchronous-ty
 */
extern nv_error nv_gpu_buffer_map_memory(nv_gpu_buffer_t* buffer, vk_size_t size, vk_size_t offset, void** mapping);

/**
 * It is illegal to try to unmap persistent buffers
 * Writes are automatically flushed when unmapping.
 */
extern nv_error nv_gpu_buffer_unmap_memory(nv_gpu_buffer_t* buffer);

/**
 * Make CPU writes to GPU memory visible to the GPU.
 * Note that you do not need to call this if you called _flush_writes()
 * because it is called internally by _flush_writes()
 */
extern nv_error nv_gpu_buffer_flush_mapped_memory(nv_gpu_buffer_t* buffer);

/**
 * Note that dst must have been allocated with atleast 'size' bytes of memory.
 * Also, it is legal to read back a non readback optimized buffer. It'll just be slow.
 * And no, the driver won't notice you're reading back a non optimized buffer and replace it.
 * By defualt, this function will wait for the readback to finish. An asynchronous function is yet to be implemented.
 * It is illegal to read back a transient buffer.
 * TODO: cache heavily read back buffers and just return that.
 */
extern nv_error nv_gpu_buffer_readback(nv_gpu_buffer_t* buffer, vk_size_t size, vk_size_t offset, void* dst);

/**
 * If this function fails, the affected data in dst will be undefined.
 * By affected data we mean the data in the range of the write.
 */
extern nv_error nv_gpu_buffer_copy(nv_gpu_buffer_t* dst, nv_gpu_buffer_t* src, vk_size_t num_bytes, vk_size_t dst_offset, vk_size_t src_offset);

/**
 * Resize a buffer
 * If buffer is transient, copy_old_data is ignored.
 * new_size may be larger or smaller than the buffers size.
 */
extern nv_error nv_gpu_buffer_resize(nv_gpu_buffer_t* buffer, size_t new_size, size_t new_alignment, bool copy_old_data);

/**
 * Note that if the buffer is transient, this returns VK_NULL_HANDLE.
 * This is because transient buffers are expected to have rapidly changing backings
 * and the driver can't provide you with a single backing.
 */
extern VkBuffer nv_gpu_buffer_get_backing(const nv_gpu_buffer_t* buffer);

extern void _nv_gpu_buffer_insert_read_barrier(const nv_gpu_buffer_t* buffer, VkCommandBuffer cmd);

NOVA_HEADER_END

#endif //__NOVA_GPU_BUFFER_H__
