
#ifndef IRIS_BUFFER_H
#define IRIS_BUFFER_H

#include "../std/include/errorcodes.h"
#include "../std/include/types.h"
#include "memory.h"
#include "types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct iris_driver;

/**
 * A minimum alignment qualifier for ALL buffers
 */
#ifndef IRIS_BUFFER_MINIMUM_ALIGNMENT
#  define IRIS_BUFFER_MINIMUM_ALIGNMENT 1
#endif

  /**
   *
   */
  typedef struct iris_buffer iris_buffer_t;

  typedef u32 iris_buffer_flags;
  typedef enum iris_buffer_flags_bits
  {
    /**
     * Whether the buffer can ONLY be used for a transfer (CPU to GPU).
     * So, a VkBuffer is almost never created and instead the driver will just use this flag
     * and use another buffer for transfers when needed. However, if no buffer is free for
     * transfers when it was needed, then a new buffer is created and this flag is NOT SET.
     */
    IRIS_BUFFER_FLAGS_TRANSFER_ONLY_BIT = 1 << 0,

    /**
     * The buffer can be used for nothing but reading back
     * data from the GPU. This involves copying data to this buffer,
     * and then using iris_buffer_readback.
     */
    IRIS_BUFFER_FLAGS_READBACK_ONLY_BIT = 1 << 1,

    /**
     * The buffer is *cabable* of providing vertices to the GPU
     * Note that this doesn't mean that the buffer is necessarily a providing vertices to the GPU currently.
     */
    IRIS_BUFFER_FLAGS_VERTEX_BUFFER_BIT = 1 << 4,

    IRIS_BUFFER_FLAGS_INDEX_BUFFER_BIT = 1 << 5,

    /**
     * Shader storage buffer. Generally used for compute workloads and output
     */
    IRIS_BUFFER_FLAGS_SS_BUFFER_BIT = 1 << 6,

    /**
     * A uniform buffer.
     * Uniform buffers imply persistent mapping.
     */
    IRIS_BUFFER_FLAGS_UNIFORM_BUFFER_BIT = 1 << 9,
  } iris_buffer_flags_bits;

  typedef struct iris_buffer_extra_create_info
  {
    size_t              multibuffering_frames;
    bool                multibuffering_enable;
    iris_memory_flags   custom_memory_flags;
    iris_memory_pool_t* custom_memory_pool;
  } iris_buffer_extra_create_info_t;

  /**
   * The contents of the buffer will NOT be initialized
   * memory may not be NULL.
   */
  extern nv_error
  iris_buffer_init(struct iris_driver* driver, iris_size_t size, size_t alignment, iris_buffer_extra_create_info_t* extra_info, iris_buffer_flags flags, iris_buffer_t* dst);
  extern void iris_buffer_destroy(iris_buffer_t* buffer);

  extern size_t iris_buffer_size(const iris_buffer_t* buffer);

  /**
   * @brief Write data from a CPU side buffer to a GPU buffer using a staging buffer or by mapping it.
   * Note that writes to the buffer aren't visible immediately.
   * This is more so a limitation of every graphics API.
   * Note that this may map the memory and you will need to flush the writes
   * TODO: write only when needed.
   * @see iris_memory_flush
   */
  extern nv_error iris_buffer_write_data(iris_buffer_t* buffer, const void* data, iris_size_t data_size, iris_size_t offset);

  /**
   * @brief Read the data from a buffer to a CPU side buffer;
   * @param dst Must be allocated with atleast 'size' bytes of memory
   * @param size The number of bytes to read back
   * It is legal to read back a non readback optimized buffer. It'll just be slow.
   * And no, the driver won't notice you're reading back a non optimized buffer and replace it.
   * This function will wait for the readback to finish.
   * An asynchronous function is yet to be implemented.
   * It is illegal to read back a transient buffer.
   * Writes are automatically flushed when unmapping.
   */
  extern nv_error iris_buffer_readback(iris_buffer_t* buffer, iris_size_t size, iris_size_t offset, void* dst);

  /**
   * @brief Copy the data from one buffer to another
   * If this function fails, the affected data in dst will be undefined.
   * By affected data we mean the data in the range of the write.
   * @note The two copy regions must not overlap.
   * @param dst Buffer to write to
   * @param src Buffer to read from
   */
  extern nv_error iris_buffer_copy(iris_buffer_t* dst, iris_buffer_t* src, iris_size_t num_bytes, iris_size_t dst_offset, iris_size_t src_offset);

  /**
   * @brief Resize a buffer, reallocating the memory
   * If buffer is transient, copy_old_data is ignored.
   * new_size may be larger or smaller than the buffers size.
   * A new block of memory is allocated from the same pool where the original buffer was allocated from.
   * If copy_old_data is true, the block is freed AFTER the new allocation, else it is always freed first
   * @param new_alignment Must be a power of 2
   * @warning The memory block assigned to the buffer is overwritten when this function is called. Beware.
   */
  extern nv_error iris_buffer_resize(iris_buffer_t* buffer, size_t new_size, size_t new_alignment, bool copy_old_data);

  /**
   * @brief Get the vulkan handle of a buffer
   * Note that if the buffer is transient, this returns VK_NULL_HANDLE.
   * This is because transient buffers are expected to have rapidly changing backings
   * and the driver can't provide you with a single backing.
   */
  extern VkBuffer iris_buffer_get_backing(const iris_buffer_t* buffer);

  extern void _iris_buffer_insert_read_barrier(const iris_buffer_t* buffer, VkCommandBuffer cmd);

  struct iris_buffer
  {
    struct iris_driver* driver;

    u64 user_data; // read/write

    iris_buffer_flags flags;

    /* The total (aligned) size of this buffer */
    iris_size_t size;

    iris_size_t alignment;

    /* The VkBuffer handle */
    VkBuffer handle;

    /**
     * The memory block owned and created by the buffer
     * for itself.
     */
    iris_memory_t memory;

    /* Driver stored information. Do not modify! */

    // whether the buffer has been detroyed or not
    bool drv_destroyed;

    /**
     * if the buffer is in use by anything.
     * note that this isn't really accurate, its set even if the buffer is just in a recording
     * that hasn't been submit. It's really leniant, to avoid using this buffer accidentally.
     */
    bool drv_in_use;
  };

#ifdef __cplusplus
}
#endif

#endif // IRIS_BUFFER_H
