#ifndef __NOVA_GPU_NEW_MEMORY_H__
#define __NOVA_GPU_NEW_MEMORY_H__

#include "../external/volk/volk.h"
#include "types.h"

typedef struct nv_gpu_memory_new_s nv_gpu_memory_new_t;
struct nvvk_driver_t;

typedef u32 nv_gpu_memory_new_flags;
typedef enum nv_gpu_memory_new_flag_bits
{
  /* See buffer.h for descriptions of all memory types. */
  NV_GPU_MEMORY_GPU_LOCAL_BIT         = 1 << 0,
  NV_GPU_MEMORY_VOLATILE_BIT          = 1 << 1,
  NV_GPU_MEMORY_TRANSIENT_BIT         = 1 << 2,
  NV_GPU_MEMORY_RESIZABLE_BIT         = 1 << 3,
  NV_GPU_MEMORY_READBACK_OPTIMAL_BIT  = 1 << 4,
  NV_GPU_MEMORY_MAPPABLE_BIT          = 1 << 5,
  NV_GPU_MEMORY_PERSISTENT_MAPPED_BIT = 1 << 6,
  NV_GPU_MEMORY_CPU_CACHED_BIT        = 1 << 7,

  /* hints for the allocator interface */

  /**
   * The VkDeviceMemory is owned by only this handle
   * and none other. id est the memory is not pooled.
   */
  NV_GPU_MEMORY_DEDICATED_BIT = 1 << 16,
} nv_gpu_memory_flag_bits;

typedef enum nv_gpu_memory_pattern
{
  // data not modified much after upload
  NV_GPU_MEMORY_PATTERN_STATIC_BIT = 0,

  // updated occasionally
  NV_GPU_MEMORY_PATTERN_DYNAMIC_BIT = 1,

  // updated every frame or very frequently
  NV_GPU_MEMORY_PATTERN_STREAMING_BIT = 2,
} nv_gpu_memory_pattern;

struct nv_gpu_memory_new_s
{
  struct nvvk_driver_t* driver;

  /**
   * If non NULL, then this memory derives from a parent
   * memory and owns_memory will be set to false.
   */
  struct nv_gpu_memory_new_s* parent; // readonly

  nv_gpu_memory_new_flags flags;

  VkDeviceMemory memory;

  vk_size_t size;
  vk_size_t alignment;

  /**
   * The offset of this memory into its parent pool/allocator.
   * May be non zero even for allocations that this memory block owns.
   */
  vk_size_t pool_offset;

  /* DRIVER INFORMATION : DO NOT MODIFY */
  void*     drv_mapped;
  vk_size_t drv_mapped_size;
  vk_size_t drv_mapped_offset;
  bool      drv_owns_memory; /* Whether this struct owns the VkDeviceMemory or not. */
};

extern nv_error nv_gpu_memory_allocate();

extern nv_gpu_memory_new_flags nv_gpu_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags);
extern VkMemoryPropertyFlags   nv_gpu_nv_memory_flags_to_vk_flags(nv_gpu_memory_new_flags flags);

#endif //__NOVA_GPU_MEMORY_H__