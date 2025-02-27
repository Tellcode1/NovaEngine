#ifndef __NOVA_BUFFER_H__
#define __NOVA_BUFFER_H__

// implementation: vk.c

#include "../../std/stdafx.h"
#include "memory.h"

NOVA_HEADER_START

// This is mainly here to bar the user from using unsupported buffer types
typedef enum nv_gpu_buffer_type_bits
{
  NOVA_GPU_BUFFER_TYPE_VERTEX_BUFFER        = 128,
  NOVA_GPU_BUFFER_TYPE_INDEX_BUFFER         = 64,
  NOVA_GPU_BUFFER_TYPE_UNIFORM_BUFFER       = 16,
  NOVA_GPU_BUFFER_TYPE_STORAGE_BUFFER       = 32,
  NOVA_GPU_BUFFER_TYPE_TRANSFER_SOURCE      = 1,
  NOVA_GPU_BUFFER_TYPE_TRANSFER_DESTINATION = 2,
  NOVA_GPU_BUFFER_TYPE_INDIRECT_BUFFER      = 256,
} nv_gpu_buffer_type_bits;
typedef uint32_t nv_gpu_buffer_type;

typedef struct nv_gpu_buffer_t
{
  struct VkBuffer_T* m_buffer;
  void*              m_mapping; // For nv_gpu_write_to_buffer()
  bool               m_is_mapped;
  // The size of the buffer
  // Even if there are multiple children, this gives only the size of ONE buffer
  size_t             m_size, m_offset;
  size_t             m_alignment;
  nv_gpu_memory_t*   m_memory;
  nv_gpu_buffer_type m_type;
} nv_gpu_buffer_t;

extern void nv_gpu_create_buffer(size_t size, size_t alignment, uint32_t usage, nv_gpu_buffer_t* dst);
extern void nv_gpu_destroy_buffer(nv_gpu_buffer_t* buffer);

// Open the buffer for writing.
// Writing must still be done through the nv_gpu_write_to_buffer() function
// However, mapped writes will be much faster as nv_gpu_write_to_buffer() will map the buffer memory multiple times
// When only once to write is needed
extern void nv_gpu_map_buffer(nv_gpu_buffer_t* buffer);

extern void nv_gpu_unmap_buffer(nv_gpu_buffer_t* buffer);

extern void nv_gpu_write_to_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset);

// Note: Memory must be able to hold all the buffers!
// You can get the size of the memory by just looking up the size of one buffer
// and then multiplying it with the count.
extern void nv_gpu_bind_buffer_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_buffer_t* buffer);

extern size_t nv_gpu_get_buffer_size(const nv_gpu_buffer_t* buffer);

// dest must be atleast the size of the buffer
extern void nv_gpu_buffer_readback(const nv_gpu_buffer_t* buffer, void* dest);

// extern NVAsync_Context nv_GPU_BufferReadbackAsync(const nv_GPU_Buffer *buffer, void *dest);

NOVA_HEADER_END

#endif //__NOVA_BUFFER_H__
