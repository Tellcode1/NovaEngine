#ifndef __NOVA_GPU_BUFFER_H__
#define __NOVA_GPU_BUFFER_H__

#include "../std/stdafx.h"
#include "../std/types.h"
#include "gpumemory.hpp"
#include "types.hpp"

struct nvvk_driver;
struct nv_gpu_memory_new;

/**
 * Nova uses two buffers for most transfers, a small one and a large one. The small one has a constant size but the large buffer will resize
 * to transfer larger amounts of data
 */
#ifndef NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE
#  define NOVA_GPU_SMALL_TRANSFER_BUFFER_SIZE 256
#endif

#ifndef NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE
#  define NOVA_GPU_LARGE_TRANSFER_BUFFER_INITIAL_SIZE 3000 // 3 MB
#endif

#ifndef NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS
#  define NOVA_GPU_SMALL_TRANSFER_BUFFER_CREATE_FLAGS (NV_GPU_BUFFER_TRANSFER_ONLY_BIT)
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

/**
 * A minimum alignment qualifier for ALL buffers
 */
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
   * The buffer can not be used for anything but transfers.
   * By 'transfers' we mean a transfer from the GPU to the CPU.
   */
  NV_GPU_BUFFER_TRANSFER_ONLY_BIT = 1 << 0,

  /**
   * The buffer can be used for nothing but reading back
   * data from the GPU. This involves copying data to this buffer,
   * and then using nv_gpu_buffer_readback.
   */
  NV_GPU_BUFFER_READBACK_ONLY_BIT = 1 << 1,

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
   * A uniform buffer. Support has yet to be added for dynamic uniform buffers.
   * Uniform buffers imply persistent mapping.
   */
  NV_GPU_BUFFER_UNIFORM_BUFFER_BIT = (1 << 9) | NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT,

} nv_gpu_buffer_flags_bits;

/**
 * The contents of the buffer will NOT be initialized
 * memory may not be NULL.
 * WARNING: For buffers with multiple backings, only the size of ONE must be specified.
 */
extern nv_error
            nv_gpu_buffer_init(struct nvvk_driver* driver, nv_gpu_memory_block_t* memory, vk_size_t size, size_t alignment, nv_gpu_buffer_flags flags, nv_gpu_buffer_t* dst);
extern void nv_gpu_buffer_destroy(nv_gpu_buffer_t* buffer);

extern size_t nv_gpu_buffer_size(const nv_gpu_buffer_t* buffer);

/**
 * Note that writes to the buffer aren't visible immediately.
 * This is more so a limitation of every graphics API.
 * Note that this may map the memory and you will need to flush the writes
 * TODO: write only when needed.
 */
extern nv_error nv_gpu_buffer_write_data(nv_gpu_buffer_t* buffer, const void* data, vk_size_t data_size, vk_size_t offset);

/**
 * It's perfectly valid to try to map persisten buffers
 * The mapping will just contain the buffer's mapping with the offset
 * Reading from a transient buffer is undefined. Only writing is valid.
 * TODO: Add optimizations to mapping like asynchronous-ty
 * It is illegal to try to unmap persistent buffers
 * Writes are automatically flushed when unmapping.
 */

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

struct nv_gpu_buffer
{
  struct nvvk_driver* driver;

  u64 user_data; // read/write

  nv_gpu_buffer_flags flags;

  /* The total (aligned) size of this buffer */
  vk_size_t size;

  vk_size_t alignment;

  /* The VkBuffer handle */
  VkBuffer buffer;

  nv_gpu_memory_block_t* block;

  /* Driver stored information. Do not modify! */

  // whether the buffer has been detroyed or not
  bool drv_destroyed;

  /**
   * if the buffer is in use by anything.
   * note that this isn't really accurate, its set even if the buffer is just in a recording
   * that hasn't been submit. It's really leniant, to avoid using this buffer accidentally.
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
};

#endif //__NOVA_GPU_BUFFER_H__
