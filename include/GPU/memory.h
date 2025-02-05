#ifndef __NOVA_GPU_MEMORY_H__
#define __NOVA_GPU_MEMORY_H__

// implementation: vk.c

// ! doesn't take into account alignment, etc.
// * This is so when we eventually have to switch to an allocator it's much easier

// This header should be fragmented into multiple, each for their own object.

#include "../../common/stdafx.h"
#include "vkstdafx.h"

NOVA_HEADER_START;

#define NOVA_GPU_ALIGNMENT_UNNECESSARY (1)

typedef struct nv_gpu_memory_t nv_gpu_memory_t;

typedef enum nv_gpu_memory_usage_bits
{
  NOVA_GPU_MEMORY_USAGE_GPU_LOCAL        = 1,
  NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE      = 2,  // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
  NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE    = 4,  // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  NOVA_GPU_MEMORY_USAGE_LAZILY_ALLOCATED = 16, // VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT
} nv_gpu_memory_usage_bits;
typedef unsigned nv_gpu_memory_usage;

// I think we should make like a cgfx_err_t enum
extern void nv_gpu_allocate_memory(size_t size, nv_gpu_memory_usage usage, nv_gpu_memory_t** dst);
extern void nv_gpu_free_memory(nv_gpu_memory_t* mem);

extern void nv_gpu_map_memory(nv_gpu_memory_t* memory, size_t size, size_t offset, void** out);
extern void nv_gpu_unmap_memory(nv_gpu_memory_t* memory);

NOVA_HEADER_END;

#endif //__NOVA_MEMORY_H__