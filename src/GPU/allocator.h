#ifndef __NOVA_GPU_MEMORY_ALLOCATOR_H__
#define __NOVA_GPU_MEMORY_ALLOCATOR_H__

#include "../std/stdafx.h"
#include "newmemory.h"
#include "types.h"

NOVA_HEADER_START

struct nvvk_driver_t;

typedef struct nv_gpu_allocator_linear_s         nv_gpu_allocator_linear_t;
typedef struct nv_gpu_allocator_freelist_block_s nv_gpu_allocator_freelist_block_t;
typedef struct nv_gpu_allocator_freelist_s       nv_gpu_allocator_freelist_t;
typedef struct nv_gpu_memory_block_s             nv_gpu_memory_block_t;
typedef struct nv_gpu_memory_pool_s              nv_gpu_memory_pool_t;
typedef struct nv_gpu_memory_pool_create_info_s  nv_gpu_memory_pool_create_info_t;

typedef VkResult (*nv_gpu_alloc_fn)(vk_size_t size, vk_size_t alignment);
typedef VkResult (*nv_gpu_free_fn)(nv_gpu_memory_block_t* block);

/* https://www.ijtsrd.com/papers/ijtsrd26731.pdf */

/**
 * Note that linear allocators do not use a policy.
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
   * Can be used if memory utilization is linear-ey.
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
  NV_GPU_ALLOCATOR_LINEAR = 0,

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
  NV_GPU_MEMORY_POOL_CREATE_FLAGS_,
  NV_GPU_MEMORY_POOL_CREATE_FLAGS_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT = 1 << 16,
} nv_gpu_memory_pool_create_flag_bits;

struct nv_gpu_allocator_linear_s
{
  /* Offset of the next allocation */
  vk_size_t bumper;
};

struct nv_gpu_allocator_freelist_block_s
{
  // The offset of this block/region in the total buffer.
  vk_size_t offset;

  // The aligned size of this block.
  vk_size_t size;

  struct nv_gpu_allocator_freelist_block_s* next;
};

struct nv_gpu_allocator_freelist_s
{
  nv_gpu_allocator_freelist_block_t* root;
};

struct nv_gpu_memory_block_s
{
  nv_gpu_memory_pool_t* pool;

  vk_size_t size;
  vk_size_t alignment;

  bool in_use;
};

struct nv_gpu_memory_pool_s
{
  struct nvvk_driver_t* driver;

  VkDeviceMemory memory;
  vk_size_t      allocated_size;

  nv_gpu_allocator_type   type;
  nv_gpu_allocator_policy policy;

  union
  {
    nv_gpu_allocator_linear_t   linear;
    nv_gpu_allocator_freelist_t freelist;
  } backing_allocator;
};

struct nv_gpu_memory_pool_create_info_s
{
  vk_size_t                       size;
  nv_gpu_memory_flags             memory_flags;
  nv_gpu_allocator_type           type;
  nv_gpu_allocator_policy         policy;
  nv_gpu_memory_pool_create_flags flags;
};

extern nv_error nv_gpu_memory_pool_init(const nv_gpu_memory_pool_create_info_t* info, nv_gpu_memory_pool_t* dst);

extern void nv_gpu_memory_pool_destroy(nv_gpu_memory_pool_t* pool);

NOVA_HEADER_END

#endif //__NOVA_GPU_MEMORY_ALLOCATOR_H__
