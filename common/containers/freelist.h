#ifndef __NOVA_FREELIST_H__
#define __NOVA_FREELIST_H__

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../../std/stdafx.h"
#include "../../std/string.h"

NOVA_HEADER_START

typedef struct nv_chunk_t     nv_chunk_t;
typedef struct nv_node_t      nv_node_t;
typedef struct nv_freelist_t  nv_freelist_t;
typedef struct nv_allocator_t nv_allocator_t;

typedef nv_node_t* (*nv_freelist_alloc_fn)(size_t alignment, size_t size); // allocate the payload and set the size
typedef void (*nv_freelist_free_fn)(nv_node_t* node);                      // you need to free the node's data and the node!!!

extern nv_chunk_t* nv_freelist_make_chunk(const nv_freelist_t* list, size_t alignment, size_t size);
extern nv_node_t*  nv_freelist_mknode(const nv_freelist_t* list, size_t alignment, size_t size);

extern void nv_freelist_init(size_t init_size, nv_freelist_alloc_fn alloc_fn, nv_freelist_free_fn free_fn, nv_allocator_t* allocator, nv_freelist_t* list);

extern void nv_freelist_destroy(nv_freelist_t* list);

// allocate memory from the list. will allocate a new node if there is no space left!!!
extern void* nv_freelist_alloc(nv_freelist_t* list, size_t alignment, size_t size);

// add a new node with guaranteed extra space.
extern nv_node_t* nv_freelist_expand(nv_freelist_t* list, size_t alignment, size_t expand_by);

// frees the block and its node
extern void nv_freelist_free(nv_freelist_t* list, void* block);

// find the node that owns the block
extern nv_node_t* nv_freelist_find(nv_freelist_t* list, void* alloc);

extern void nv_freelist_check_circle(const nv_freelist_t* list);

struct nv_chunk_t
{
  void* m_mapping;
  // available is here for easy querying by the freelist
  size_t     m_mapping_size, m_mapping_offset, m_available;
  nv_node_t* m_root;
};

struct nv_node_t
{
  struct nv_node_t* m_next;
  nv_chunk_t*       m_chunk;
  void*             m_payload;
  void*             m_mapping;
  size_t            m_mapping_size;
  size_t            m_size; // allocated size
  unsigned          m_canary;
  bool              m_in_use;
};

struct nv_freelist_t
{
  unsigned             m_canary;
  nv_node_t*           m_root;
  nv_freelist_alloc_fn m_alloc_fn;
  nv_freelist_free_fn  m_free_fn;
  pthread_mutex_t      m_mutex;
};

NOVA_HEADER_END

#endif //__NOVA_FREELIST_H__
