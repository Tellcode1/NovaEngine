#ifndef __NOVA_POOL_H__
#define __NOVA_POOL_H__

#include "../std/stdafx.h"
#include "../std/string.h"

NOVA_HEADER_START

typedef struct nv_pool_t nv_pool_t;

extern int nv_pool_init(nv_pool_t* pool, size_t type_size, size_t capacity);

extern void nv_pool_destroy(nv_pool_t* pool);

/**
 * WARNING: RETURNS NULL IF THERE ARE NO MORE FREE OBEJCTS
 * You may need to make a new pool!!!
 */
extern void* nv_pool_alloc(nv_pool_t* pool);

/**
 * Sanity checks are **NOT** performed on whether the object is a part of the pool or not.
 * You're supposed to do that ;D
 */
extern void nv_pool_free(nv_pool_t* pool, void* object);

struct nv_pool_t
{
  size_t     m_type_size;
  size_t     m_capacity;
  size_t     m_free_count;
  void*      m_free_list;
  void*      m_allocation;
  SDL_mutex* m_mutex;
};

NOVA_HEADER_END

#endif //__NOVA_POOL_H__
