#ifndef __NOVA_BUFFER_H__
#define __NOVA_BUFFER_H__

// implementation: vk.c

#include "../std/stdafx.h"
#include "memory.h"

NOVA_HEADER_START

// This is mainly here to bar the user from using unsupported buffer types
// Their values correspond to the Vulkan counterparts, for now.
// A convert function will be added to allow Nova to have things like Single use buffers
// for temporary data parsing that will be managed internally.
typedef enum nv_gpu_buffer_type_bits
{
  NOVA_GPU_BUFFER_USAGE_VERTEX_BUFFER        = 128,
  NOVA_GPU_BUFFER_USAGE_INDEX_BUFFER         = 64,
  NOVA_GPU_BUFFER_USAGE_UNIFORM_BUFFER       = 16,
  NOVA_GPU_BUFFER_USAGE_STORAGE_BUFFER       = 32,
  NOVA_GPU_BUFFER_USAGE_TRANSFER_SOURCE      = 1,
  NOVA_GPU_BUFFER_USAGE_TRANSFER_DESTINATION = 2,
  NOVA_GPU_BUFFER_USAGE_INDIRECT_BUFFER      = 256,
} nv_gpu_buffer_type_bits;
typedef uint32_t nv_gpu_buffer_usage;

typedef struct nv_gpu_buffer_t
{
  struct VkBuffer_T* buffer;
  void*              mapping; // For nv_gpu_write_to_buffer()
  bool               is_mapped;
  // The size of the buffer
  // Even if there are multiple children, this gives only the size of ONE buffer
  size_t              size, offset;
  size_t              alignment;
  nv_gpu_memory_t*    memory;
  nv_gpu_buffer_usage usage;
} nv_gpu_buffer_t;

extern void nv_gpu_create_buffer(size_t size, size_t alignment, nv_gpu_buffer_usage usage, nv_gpu_buffer_t* dst);
extern void nv_gpu_destroy_buffer(nv_gpu_buffer_t* buffer);

// Open the buffer for writing.
// Writing must still be done through the nv_gpu_write_to_buffer() function
// However, mapped writes will be much faster as nv_gpu_write_to_buffer() will map the buffer memory multiple times
// When only once to write is needed
extern void nv_gpu_map_buffer(nv_gpu_buffer_t* buffer);

extern void nv_gpu_unmap_buffer(nv_gpu_buffer_t* buffer);

extern void nv_gpu_write_to_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset);

extern void nv_gpu_resize_buffer(nv_gpu_buffer_t* buffer, nv_gpu_memory_t* memory, size_t new_size);

/**
 * @brief Copy the contents of a buffer to the other
 */
extern void nv_gpu_copy_buffer(nv_gpu_buffer_t* dst, const nv_gpu_buffer_t* src);

// Note: Memory must be able to hold all the buffers!
// You can get the size of the memory by just looking up the size of one buffer
// and then multiplying it with the count.
extern void nv_gpu_bind_buffer_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_buffer_t* buffer);

extern size_t nv_gpu_get_buffer_size(const nv_gpu_buffer_t* buffer);

// src must be atleast the size of the buffer
extern void nv_gpu_buffer_readback(const nv_gpu_buffer_t* buffer, void* src);

// extern NVAsync_Context nv_GPU_BufferReadbackAsync(const nv_GPU_Buffer *buffer, void *src);

NOVA_HEADER_END

#endif //__NOVA_BUFFER_H__
