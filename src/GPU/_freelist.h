#ifndef __NOVA_GPU_FREELIST_H__
#define __NOVA_GPU_FREELIST_H__

#include "../std/errorcodes.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

/**
 * A lot of this code is stolen from places.
 */

typedef struct nv_gpu_freelist_s       nv_gpu_freelist_t;
typedef struct nv_gpu_freelist_block_s nv_gpu_freelist_block_t;

struct nv_gpu_freelist_block_s
{
  size_t                   size;
  size_t                   offset;
  nv_gpu_freelist_block_t* next;
};

struct nv_gpu_freelist_s
{
  nv_gpu_freelist_block_t* root;
  size_t                   num_nodes;
};

extern nv_error nv_gpu_freelist_init(size_t initial_capacity, nv_gpu_freelist_t* dst);

extern void nv_gpu_freelist_destroy(nv_gpu_freelist_t* flist);

/* first fit */
extern bool nv_gpu_freelist_alloc(nv_gpu_freelist_t* flist, size_t size, size_t alignment, size_t* offset_out);

extern nv_error nv_gpu_freelist_free(nv_gpu_freelist_t* flist, size_t offset, size_t size);

extern nv_error nv_gpu_freelist_defrag(nv_gpu_freelist_t* flist);

NOVA_HEADER_END

#endif