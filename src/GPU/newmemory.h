#ifdef __NOVA_GPU_NEW_MEMORY_H__
#  define __NOVA_GPU_NEW_MEMORY_H__

#  include "../external/volk/volk.h"
#  include "types.h"

typedef struct nv_gpu_memory_t nv_gpu_memory_t;
struct nvvk_driver_t;

typedef u32 nv_gpu_memory_flags;
typedef enum nv_gpu_memory_flag_bits
{
  /**
   * Memory that is typically located in the GPU's VRAM (or in system/shared RAM for iGPUs)
   * If used in conjunction with _MAPPABLE_BIT, then memory transfers
   * go through the PCIe bus.
   */
  NV_GPU_MEMORY_GPU_LOCAL_BIT = 1 << 0,

  /* Memory can be mapped using nv_gpu_map_memory() */
  NV_GPU_MEMORY_MAPPABLE_BIT = 1 << 1,

  /**
   * Accesses of the memory will go through the cache line  so reads and writes are fast but the GPU read/writes are slow
   * Must not be used in conjunction with device local memory
   * Implies that memory is mappable, as the CPU obviously has access to the memory.
   */
  NV_GPU_MEMORY_CPU_CACHED_BIT = (1 << 2) | (NV_GPU_MEMORY_MAPPABLE_BIT),

  /**
   * Continuous flow of data from the CPU to the GPU.
   * Use this when you're transferring data from the CPU to the GPU each frame
   */
  NV_GPU_MEMORY_STREAMING_BIT = 1 << 5,
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

/* somehow ask the user if the memory is read heavy, write heavy or transfer heavy or something?  */

struct nv_gpu_memory_t
{
  struct nvvk_driver_t* driver;

  /**
   * If non NULL, then this memory derives from a parent
   * memory and owns_memory will be set to false.
   */
  struct nv_gpu_memory_t* parent; // readonly

  nv_gpu_memory_flags flags;

  VkDeviceMemory memory;

  vk_size_t size;
  vk_size_t alignment;

  /* The offset of this memory into a larger memory region. Only applicable if owns_memory is false */
  vk_size_t offset;

  /* DRIVER INFORMATION : DO NOT MODIFY */
  void*     drv_mapped;
  vk_size_t drv_mapped_size;
  vk_size_t drv_mapped_offset;
  bool      drv_owns_memory; /* Whether this struct owns the VkDeviceMemory or not. */
};

extern nv_errorc nv_gpu_memory_allocate();

extern nv_gpu_memory_flags   nv_gpu_vk_memory_flags_to_nv_flags(VkMemoryPropertyFlags flags);
extern VkMemoryPropertyFlags nv_gpu_nv_memory_flags_to_vk_flags(nv_gpu_memory_flags flags);

#endif //__NOVA_GPU_MEMORY_H__