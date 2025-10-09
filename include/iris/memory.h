#ifndef IRIS_MEMORY_ALLOCATOR_H
#define IRIS_MEMORY_ALLOCATOR_H

#include "../std/include/errorcodes.h"
#include "../std/include/types.h"
#include "types.h"

#include "../../external/volk/volk.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  struct iris_driver;

  /**
   * The core memory struct.
   */
  typedef struct iris_memory iris_memory_t;

  typedef struct iris_allocator_stack         iris_allocator_stack_t;
  typedef struct iris_memory_pool             iris_memory_pool_t;
  typedef struct iris_memory_pool_create_info iris_memory_pool_create_info_t;

  typedef VkResult (*iris_alloc_fn)(iris_size_t size, iris_size_t alignment);
  typedef VkResult (*iris_free_fn)(iris_memory_t* block);

  typedef u32 iris_memory_flags;
  typedef enum iris_memory_flag_bits
  {
    /**
     * A default GPU local memory block.
     */
    IRIS_MEMORY_FLAGS_DEFAULT_BIT = 1 << 0,

    /**
     * The data stored in the buffer is REQUIRED.
     * This typically means that the buffer is accessed externally (not by the nv_gpu API) and the driver
     * won't skip or delay unneeded transfers.
     * Disables many optimizations by the driver.
     */
    IRIS_MEMORY_FLAGS_VOLATILE_BIT = 1 << 1,

    /**
     * The data in the buffer does not matter, it is only used as an intermediary
     * So, the driver can use this buffer when the user isn't using it to provide for
     * other buffers without creating new ones.
     * NOTE: Volatility doesn't apply to other buffers that are swapped out to this buffer. Only this buffer is affected.
     */
    IRIS_MEMORY_FLAGS_TRANSIENT_BIT = 1 << 2,

    /**
     * The buffer is resizable as needed.
     * If the buffer is non transient, then the data from the old buffer is copied over
     * If the buffer is non volatile, transfers may be outright avoided or delayed until necessary.
     * Note that this does not mean that regions in the buffer may resize, only that the parent buffer can resize or not.
     */
    IRIS_MEMORY_FLAGS__RESIZABLE_BIT = 1 << 3,

    /**
     * The memory can be read back to the CPU side from the GPU.
     * The buffer must not be transient.
     */
    IRIS_MEMORY_FLAGS_READBACK_OPTIMAL_BIT = 1 << 4,

    /**
     * The buffer is visible to the CPU, i.e. the CPU has fast writing access to the buffer.
     */
    IRIS_MEMORY_FLAGS_MAPPABLE_BIT = 1 << 5,

    /**
     * Implies CPU visiblity (obviously.)
     * A pool may not have this bit set. Only individual blocks in it must.
     */
    IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT = (1 << 6) | IRIS_MEMORY_FLAGS_MAPPABLE_BIT,

    IRIS_MEMORY_FLAGS_CPU_CACHED_BIT = 1 << 7,

    IRIS_MEMORY_FLAGS_LAZILY_ALLOCATED_BIT = 1 << 8,

    /**
     * The VkDeviceMemory is owned by only this handle
     * and none other. id est the memory is not pooled.
     */
    IRIS_MEMORY_FLAGS_DEDICATED_BIT = 1 << 16,
  } iris_memory_flag_bits;

  /* https://www.ijtsrd.com/papers/ijtsrd26731.pdf */

  /**
   * Note that stack allocators do not use a policy.
   * Their policy is simply first fit, if you can call it that.
   * TODO: Implement
   */
  typedef enum iris_allocator_policy
  {
    /**
     * Get the block with the least size that is greater than or equal to the request.
     * Better memory utilization, slightly slower, tends to make tiny unusable holes in the memory.
     */
    IRIS_ALLOCATOR_POLICY_BEST_FIT = 0,

    /**
     * Gives the highest sized block available.
     * Garbage memory utilization
     * Ensures large memory allocations get their memory.
     * Can be used if memory utilization is stack-ey.
     * id est you allocate some memory, use it, free it, on and on.
     */
    IRIS_ALLOCATOR_POLICY_WORST_FIT = 1,

    /**
     * The first block that is big enough is chosen and given.
     * Fast, but really garbage memory utilization.
     * Consider picking best fit.
     */
    IRIS_ALLOCATOR_POLICY_FIRST_FIT = 2,
  } iris_allocator_policy;

  typedef enum iris_allocator_type
  {
    /**
     * The most primitive style of allocator.
     * Only allows freeing the last block allocated (LIFO).
     */
    IRIS_ALLOCATOR_STACK = 0,

    /**
     * A far more complex, freelist backed allocator
     * Use this when memory usage is very random such as using this
     * singly across an entire program.
     * Allows random allocation and freeing.
     */
    IRIS_ALLOCATOR_FREELIST = 1,
  } iris_allocator_type;

  typedef u32 iris_memory_pool_create_flags;
  typedef enum iris_memory_pool_create_flag_bits
  {
    /**
     * TODO: Implement
     */
    IRIS_MEMORY_POOL_CREATE_FLAGS_,
    IRIS_MEMORY_POOL_CREATE_FLAGS_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT = 1 << 16,
  } iris_memory_pool_create_flag_bits;

  struct iris_allocator_stack
  {
    /* Offset of the next allocation */
    iris_size_t bumper;

    /* The offset of the previous allocation, used to pop elements */
    iris_size_t last_allocation_bumper;
  };
  /**
   * A lot of this code is stolen from places.
   */

  typedef struct iris_freelist       iris_freelist_t;
  typedef struct iris_freelist_block iris_freelist_block_t;

  struct iris_freelist_block
  {
    size_t                 size;
    size_t                 offset;
    iris_freelist_block_t* next;
  };

  struct iris_freelist
  {
    iris_freelist_block_t* root;
    iris_freelist_block_t* free_nodes;
    size_t                 num_nodes;
  };

  struct iris_memory
  {
    struct iris_driver* driver;
    u64                 user_data;

    /**
     * Pool handle. NULL is block is dedicated allocation.
     */
    iris_memory_pool_t* pool;

    /**
     * Only if IRIS_MEMORY_FLAGS_DEDICATED_BIT is set,
     * VK_NULL_HANDLE for non dedicated allocations
     */
    VkDeviceMemory dedicated_allocation;

    /**
     * This individual memory block's flag. May not always be the same as the pool it was allocated from.
     */
    iris_memory_flags memory_flags;

    /**
     * Allocated size
     */
    iris_size_t size;

    /**
     * Offset in the pool.
     * For dedicated allocations, it must be equal to 0.
     */
    iris_size_t pool_offset;

    /**
     * Alignment of the block. May be greater than the pools alignment but not lesser.
     */
    iris_size_t alignment;

    /**
     * Information about the current mapped region.
     */
    void*       drv_mapped;
    iris_size_t drv_mapped_size;   // the size of the mapping
    iris_size_t drv_mapped_offset; // the offset of the mapping
  };

  struct iris_memory_pool
  {
    u32 canary; // = 0xDEADBEEF

    struct iris_driver* driver;

    u64 user_data;

    VkDeviceMemory memory_handle;
    iris_size_t    allocated_size;
    iris_size_t    alignment;

    iris_memory_flags             memory_flags;
    iris_memory_pool_create_flags flags;

    iris_allocator_type   type;
    iris_allocator_policy policy;

    union
    {
      iris_allocator_stack_t stack;
      iris_freelist_t        flist;
    } backing_allocator;

    /* DRIVER INFORMATION */

    /* The entire block of memory is mapped at once. */
    void* drv_mapped;
  };

  struct iris_memory_pool_create_info
  {
    iris_size_t size;

    /**
     * The minimum alignment that the driver will use for this pool.
     */
    iris_size_t                   minimum_alignment;
    iris_memory_flags             memory_flags;
    iris_allocator_type           type;
    iris_allocator_policy         policy;
    iris_memory_pool_create_flags flags;
  };

  extern VkDeviceMemory iris_memory_get_backing(const iris_memory_t* block);

  extern nv_error iris_memory_pool_init(struct iris_driver* driver, const iris_memory_pool_create_info_t* info, iris_memory_pool_t* dst);
  extern void     iris_memory_pool_destroy(iris_memory_pool_t* pool);

  /**
   * returns NV_ERROR_MALLOC_FAILED on pool exhaustion.
   * Also returns NV_ERROR_MALLOC_FAILED if a CPU side allocator returned NULL.
   */
  extern nv_error iris_memory_allocate(iris_memory_pool_t* pool, iris_size_t size, iris_size_t alignment, iris_memory_t* dst_block);
  extern nv_error iris_memory_free_immediate(iris_memory_t* block);

  /**
   * IRIS_MEMORY_FLAGS_DEDICATED_BIT is automatically set.
   * Allocate a block of memory using vkAllocateMemory and return a block.
   * @param memory_type_bits The vulkan provided memory type bits. If you don't know where to get them, just pass UINT32_MAX
   * @sa vkGetImageMemoryRequirements
   * @sa vkGetBufferMemoryRequirements
   */
  extern nv_error iris_memory_allocate_dedicated(
      struct iris_driver* driver, uint32_t memory_type_bits, iris_memory_flags flags, iris_size_t size, iris_size_t alignment, iris_memory_t* dst_block);

  /**
   * Map a memory for writing. You can only map memories which have IRIS_MEMORY_FLAGS_MAPPABLE_BIT set.
   */
  extern nv_error iris_memory_map(iris_memory_t* memory, size_t offset, size_t size, void** mapping);

  extern void iris_memory_unmap(iris_memory_t* memory);

  /**
   * Make all CPU writes visible to the GPU
   * Always call this after you're done writing data to the GPU and before
   * rendering or buffer reads.
   * Note that if no writes had been performed, this function will
   * simply exit and do nothing. So, it is safe to call this on a per frame
   * basis.
   */
  extern nv_error iris_memory_flush(iris_memory_t* memory);

  // allocator functions

  /**
   * Not to be called by the user
   */
  extern nv_error iris_memory_allocator_stack_init(iris_size_t aligned_size, iris_allocator_stack_t* stack);

  /**
   * Not to be called by the user
   */
  extern void iris_memory_allocator_stack_destroy(iris_allocator_stack_t* stack);

  extern nv_error iris_freelist_init(size_t initial_capacity, iris_freelist_t* dst);

  extern void iris_freelist_destroy(iris_freelist_t* flist);

  /* first fit */
  extern bool iris_freelist_alloc(iris_freelist_t* flist, size_t size, size_t alignment, iris_allocator_policy policy, size_t* offset_out);

  extern nv_error iris_freelist_free(iris_freelist_t* flist, size_t offset, size_t size);

  /**
   * Defragment a freelist. This is called automatically upon freeing a block.
   * But you may call it yourself too.
   */
  extern nv_error iris_freelist_defrag(iris_freelist_t* flist);

  extern iris_memory_flags     iris_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags);
  extern VkMemoryPropertyFlags iris_nv_memory_flags_to_vk_flags(iris_memory_flags flags);

  /**
   * Get whether two memory blocks overlap or not.
   */
  extern bool iris_memory_does_overlap(const iris_memory_t* mem_1, const iris_memory_t* mem_2);

#ifdef __cplusplus
}
#endif

#endif // IRIS_MEMORY_ALLOCATOR_H
