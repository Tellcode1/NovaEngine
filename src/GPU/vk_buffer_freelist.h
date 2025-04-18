#ifndef __NOVA_VK_BUFFER_FREELIST_H__
#define __NOVA_VK_BUFFER_FREELIST_H__

#include "../std/stdafx.h"
#include "types.h"

NOVA_HEADER_START

typedef struct nv_gpu_buffer_freelist_block_t
{
  vk_size_t                              offset;
  vk_size_t                              size;
  struct nv_gpu_buffer_freelist_block_t* next;
} nv_gpu_buffer_freelist_block_t;

typedef struct nv_gpu_buffer_freelist_t
{
  nv_gpu_buffer_freelist_block_t* head;
} nv_gpu_buffer_freelist_t;

extern nv_errorc nv_gpu_buffer_freelist_init(vk_size_t capacity, nv_gpu_buffer_freelist_t* dst);
extern void      nv_gpu_buffer_freelist_destroy(nv_gpu_buffer_freelist_t* list);

/**
 * Returns true if allocation succeeded.
 */
bool nv_gpu_buffer_freelist__allocate(nv_gpu_buffer_freelist_t* list, vk_size_t size, vk_size_t* out_offset);

/**
 * Sorry that you need to track the size of the block..
 */
void nv_gpu_buffer_freelist_free(nv_gpu_buffer_freelist_t* list, vk_size_t offset, vk_size_t size);

NOVA_HEADER_END

#endif //__NOVA_VK_BUFFER_FREELIST_H__