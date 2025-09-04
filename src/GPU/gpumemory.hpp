#ifndef __NOVA_GPU_MEMORY_ALLOCATOR_H__
#define __NOVA_GPU_MEMORY_ALLOCATOR_H__

#include "../std/types.h"
#include "types.hpp"

struct nvvk_driver;

/**
 * The core memory struct.
 */
typedef struct nv_gpu_memory_block nv_gpu_memory_block_t;

typedef struct nv_gpu_allocator_stack         nv_gpu_allocator_stack_t;
typedef struct nv_gpu_memory_pool             nv_gpu_memory_pool_t;
typedef struct nv_gpu_memory_pool_create_info nv_gpu_memory_pool_create_info_t;

typedef VkResult (*nv_gpu_alloc_fn)(vk_size_t size, vk_size_t alignment);
typedef VkResult (*nv_gpu_free_fn)(nv_gpu_memory_block_t* block);

typedef u32 nv_gpu_memory_new_flags;
typedef enum nv_gpu_memory_new_flag_bits
{
  /**
   * The data stored in the buffer is REQUIRED.
   * This typically means that the buffer is accessed externally (not by the nv_gpu API) and the driver
   * won't skip or delay unneeded transfers.
   * Disables many optimizations by the driver.
   */
  NV_GPU_MEMORY_VOLATILE_BIT = 1 << 1,

  /**
   * The data in the buffer does not matter, it is only used as an intermediary
   * So, the driver can use this buffer when the user isn't using it to provide for
   * other buffers without creating new ones.
   * NOTE: Volatility doesn't apply to other buffers that are swapped out to this buffer. Only this buffer is affected.
   */
  NV_GPU_MEMORY_TRANSIENT_BIT = 1 << 2,

  /**
   * The buffer is resizable as needed.
   * If the buffer is non transient, then the data from the old buffer is copied over
   * If the buffer is non volatile, transfers may be outright avoided or delayed until necessary.
   * Note that this does not mean that regions in the buffer may resize, only that the parent buffer can resize or not.
   */
  NV_GPU_MEMORY_RESIZABLE_BIT = 1 << 3,

  /**
   * The memory can be read back to the CPU side from the GPU.
   * The buffer must not be transient.
   */
  NV_GPU_MEMORY_READBACK_OPTIMAL_BIT = 1 << 4,

  /**
   * The buffer is visible to the CPU, i.e. the CPU has fast writing access to the buffer.
   */
  NV_GPU_MEMORY_MAPPABLE_BIT = 1 << 5,

  /**
   * Implies CPU visiblity (obviously.)
   */
  NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT = (1 << 6) | NV_GPU_MEMORY_MAPPABLE_BIT,

  NV_GPU_MEMORY_CPU_CACHED_BIT = 1 << 7,

  /**
   * The VkDeviceMemory is owned by only this handle
   * and none other. id est the memory is not pooled.
   */
  NV_GPU_MEMORY_DEDICATED_BIT = 1 << 16,
} nv_gpu_memory_flag_bits;

/* https://www.ijtsrd.com/papers/ijtsrd26731.pdf */

/**
 * Note that stack allocators do not use a policy.
 * Their policy is simply first fit, if you can call it that.
 */
typedef enum nv_gpu_allocator_policy
{
  /**
   * Get the block with the least size that is greater than or equal to the request.
   * Better memory utilization, slightly slower, tends to make tiny unusable holes in the memory.
   */
  NV_GPU_ALLOCATOR_POLICY_BEST_FIT = 0,

  /**
   * Gives the highest sized block available.
   * Garbage memory utilization
   * Ensures large memory allocations get their memory.
   * Can be used if memory utilization is stack-ey.
   * id est you allocate some memory, use it, free it, on and on.
   */
  NV_GPU_ALLOCATOR_POLICY_WORST_FIT = 1,

  /**
   * The first block that is big enough is chosen and given.
   * Fast, but really garbage memory utilization.
   * Consider picking best fit.
   */
  NV_GPU_ALLOCATOR_POLICY_FIRST_FIT = 2,
} nv_gpu_allocator_policy;

typedef enum nv_gpu_allocator_type
{
  /**
   * The most primitive style of allocator.
   * Only allows freeing the last block allocated (LIFO).
   */
  NV_GPU_ALLOCATOR_STACK = 0,

  /**
   * A far more complex, freelist backed allocator
   * Use this when memory usage is very random such as using this
   * singly across an entire program.
   * Allows random allocation and freeing.
   */
  NV_GPU_ALLOCATOR_FREELIST = 1,
} nv_gpu_allocator_type;

typedef u32 nv_gpu_memory_pool_create_flags;
typedef enum nv_gpu_memory_pool_create_flag_bits
{
  /**
   * TODO: Implement
   */
  NV_GPU_MEMORY_POOL_CREATE_FLAGS_,
  NV_GPU_MEMORY_POOL_CREATE_FLAGS_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT = 1 << 16,
} nv_gpu_memory_pool_create_flag_bits;

struct nv_gpu_allocator_stack
{
  /* Offset of the next allocation */
  vk_size_t bumper;

  /* The offset of the previous allocation, used to pop elements */
  vk_size_t last_allocation_bumper;
};
/**
 * A lot of this code is stolen from places.
 */

typedef struct nv_gpu_freelist       nv_gpu_freelist_t;
typedef struct nv_gpu_freelist_block nv_gpu_freelist_block_t;

struct nv_gpu_freelist_block
{
  size_t                   size;
  size_t                   offset;
  nv_gpu_freelist_block_t* next;
};

struct nv_gpu_freelist
{
  nv_gpu_freelist_block_t* root;
  nv_gpu_freelist_block_t* free_nodes;
  size_t                   num_nodes;
};

struct nv_gpu_memory_block
{
  struct nvvk_driver* driver;

  nv_gpu_memory_pool_t* pool;

  /**
   * Only if NV_GPU_MEMORY_DEDICATED_BIT is set,
   * VK_NULL_HANDLE for non dedicated allocations
   */
  VkDeviceMemory dedicated_allocation;

  nv_gpu_memory_new_flags flags;

  vk_size_t size;
  vk_size_t offset;
  vk_size_t alignment;

  u64 user_data;

  /**
   * Really only used for uniform buffers || Persistent mapped buffers
   */
  void*     drv_mapped;
  vk_size_t drv_mapped_size;   // the size of the mapping
  vk_size_t drv_mapped_offset; // the offset of the mapping
};

struct nv_gpu_memory_pool
{
  u32 canary; // = 0xDEADBEEF

  struct nvvk_driver* driver;

  u64 user_data;

  VkDeviceMemory memory;
  vk_size_t      allocated_size;
  vk_size_t      alignment;

  nv_gpu_memory_new_flags         memory_flags;
  nv_gpu_memory_pool_create_flags flags;

  nv_gpu_allocator_type   type;
  nv_gpu_allocator_policy policy;

  union
  {
    nv_gpu_allocator_stack_t stack;
    nv_gpu_freelist_t        flist;
  } backing_allocator;

  /* DRIVER INFORMATION */

  /* The entire block of memory is mapped at once. */
  void* drv_mapped;
};

struct nv_gpu_memory_pool_create_info
{
  vk_size_t size;

  /**
   * The minimum alignment that the driver will use for this pool.
   */
  vk_size_t                       minimum_alignment;
  nv_gpu_memory_new_flags         memory_flags;
  nv_gpu_allocator_type           type;
  nv_gpu_allocator_policy         policy;
  nv_gpu_memory_pool_create_flags flags;
};

extern VkDeviceMemory nv_gpu_memory_get_backing(const nv_gpu_memory_block_t* block);

extern nv_error nv_gpu_memory_pool_init(struct nvvk_driver* driver, const nv_gpu_memory_pool_create_info_t* info, nv_gpu_memory_pool_t* dst);
extern void     nv_gpu_memory_pool_destroy(nv_gpu_memory_pool_t* pool);

/**
 * returns NV_ERROR_MALLOC_FAILED on pool exhaustion.
 * Also returns NV_ERROR_MALLOC_FAILED if a CPU side allocator returned NULL.
 */
extern nv_error nv_gpu_memory_pool_allocate(nv_gpu_memory_pool_t* pool, vk_size_t size, vk_size_t alignment, nv_gpu_memory_block_t* dst_block);
extern nv_error nv_gpu_memory_pool_free(nv_gpu_memory_block_t* block);

/**
 * NV_GPU_MEMORY_DEDICATED_BIT is automatically set.
 * Allocate a block of memory using vkAllocateMemory and return a block.
 */
extern nv_error nv_gpu_memory_allocate_dedicated(struct nvvk_driver* driver, nv_gpu_memory_new_flags flags, vk_size_t size, vk_size_t alignment, nv_gpu_memory_block_t* dst_block);

/**
 * Map a memory for writing. You can only map memories which have NV_GPU_MEMORY_MAPPABLE_BIT set.
 */
extern nv_error nv_gpu_memory_map(nv_gpu_memory_block_t* memory, size_t offset, size_t size, void** mapping);

extern void nv_gpu_memory_unmap(nv_gpu_memory_block_t* memory);

/**
 * Make all CPU writes visible to the GPU
 * Always call this after you're done writing data to the GPU and before
 * rendering or buffer reads.
 * Note that if no writes had been performed, this function will
 * simply exit and do nothing. So, it is safe to call this on a per frame
 * basis.
 */
extern nv_error nv_gpu_memory_flush(nv_gpu_memory_block_t* memory);

// allocator functions

/**
 * Not to be called by the user
 */
extern nv_error nv_gpu_memory_allocator_stack_init(vk_size_t aligned_size, nv_gpu_allocator_stack_t* stack);

/**
 * Not to be called by the user
 */
extern void nv_gpu_memory_allocator_stack_destroy(nv_gpu_allocator_stack_t* stack);

extern nv_error nv_gpu_freelist_init(size_t initial_capacity, nv_gpu_freelist_t* dst);

extern void nv_gpu_freelist_destroy(nv_gpu_freelist_t* flist);

/* first fit */
extern bool nv_gpu_freelist_alloc(nv_gpu_freelist_t* flist, size_t size, size_t alignment, nv_gpu_allocator_policy policy, size_t* offset_out);

extern nv_error nv_gpu_freelist_free(nv_gpu_freelist_t* flist, size_t offset, size_t size);

extern nv_error nv_gpu_freelist_defrag(nv_gpu_freelist_t* flist);

extern nv_gpu_memory_new_flags nv_gpu_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags);
extern VkMemoryPropertyFlags   nv_gpu_nv_memory_flags_to_vk_flags(nv_gpu_memory_new_flags flags);

/**
 * Get whether two memory blocks overlap or not.
 */
extern bool nv_gpu_memory_does_overlap(const nv_gpu_memory_block_t* mem_1, const nv_gpu_memory_block_t* mem_2);

#endif //__NOVA_GPU_MEMORY_ALLOCATOR_H__
