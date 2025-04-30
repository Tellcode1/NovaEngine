#ifndef __NOVA_GPU_MEMORY_ALLOCATOR_H__
#define __NOVA_GPU_MEMORY_ALLOCATOR_H__

#include "../std/stdafx.h"
#include "_freelist.h"
#include "newmemory.h"
#include "types.h"

NOVA_HEADER_START

struct nvvk_driver_t;

/**
 * The core memory struct.
 */
typedef struct nv_gpu_memory_block_s nv_gpu_memory_block_t;

typedef struct nv_gpu_allocator_stack_s         nv_gpu_allocator_stack_t;
typedef struct nv_gpu_memory_pool_s             nv_gpu_memory_pool_t;
typedef struct nv_gpu_memory_pool_create_info_s nv_gpu_memory_pool_create_info_t;

typedef VkResult (*nv_gpu_alloc_fn)(vk_size_t size, vk_size_t alignment);
typedef VkResult (*nv_gpu_free_fn)(nv_gpu_memory_block_t* block);

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

struct nv_gpu_allocator_stack_s
{
  /* Offset of the next allocation */
  vk_size_t bumper;

  /* The offset of the previous allocation, used to pop elements */
  vk_size_t last_allocation_bumper;
};

struct nv_gpu_memory_block_s
{
  nv_gpu_memory_pool_t* pool;

  nv_gpu_memory_new_flags flags;

  vk_size_t size;
  vk_size_t offset;
  vk_size_t alignment;

  u64 user_data;
};

struct nv_gpu_memory_pool_s
{
  u32 canary; // = 0xDEADBEEF

  struct nvvk_driver_t* driver;

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

struct nv_gpu_memory_pool_create_info_s
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

extern nv_error nv_gpu_memory_pool_init(struct nvvk_driver_t* driver, const nv_gpu_memory_pool_create_info_t* info, nv_gpu_memory_pool_t* dst);
extern void     nv_gpu_memory_pool_destroy(nv_gpu_memory_pool_t* pool);

/**
 * returns NV_ERROR_MALLOC_FAILED on pool exhaustion.
 * Also returns NV_ERROR_MALLOC_FAILED if a CPU side allocator returned NULL.
 */
extern nv_error nv_gpu_memory_pool_allocate(nv_gpu_memory_pool_t* pool, vk_size_t size, vk_size_t alignment, nv_gpu_memory_block_t* dst_block);
extern nv_error nv_gpu_memory_pool_free(nv_gpu_memory_pool_t* pool, nv_gpu_memory_block_t* block);

/**
 * Not to be called by the user
 */
extern nv_error nv_gpu_memory_allocator_stack_init(vk_size_t aligned_size, nv_gpu_allocator_stack_t* stack);

/**
 * Not to be called by the user
 */
extern void nv_gpu_memory_allocator_stack_destroy(nv_gpu_allocator_stack_t* stack);

NOVA_HEADER_END

#endif //__NOVA_GPU_MEMORY_ALLOCATOR_H__
