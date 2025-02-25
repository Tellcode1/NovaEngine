#ifndef __NOVA_REDBLACK_MAP_H__
#define __NOVA_REDBLACK_MAP_H__

#include "../../std/stdafx.h"
#include "freelist.h"

typedef enum nv_rbnode_color
{
  NOVA_RBNODE_COLOR_RED = 0,
  NOVA_RBNODE_COLOR_BLK = 1
} nv_rbnode_color;

typedef struct nv_rbmap_node_t
{
  nv_rbmap_node_t* m_parent;
  nv_rbmap_node_t* m_children[2];
  nv_rbnode_color  m_color;
  void*            m_key; // key size in rbmap
  void*            m_val;
} nv_rbmap_node_t;

typedef struct nv_rbmap_t
{
  size_t m_key_size;
  size_t m_val_size;

  nv_rbmap_node_t* m_root;
  nv_allocator_t*  m_alloc;
} nv_rbmap_t;

static inline void
nv_rbmap_init(size_t key_size, size_t val_size, nv_allocator_t* alloc, nv_rbmap_t* dst)
{
  *dst = nv_zero_init(nv_rbmap_t);

  dst->m_key_size = key_size;
  dst->m_val_size = val_size;

  dst->m_root  = NULL;
  dst->m_alloc = alloc;
}

#endif //__NOVA_REDBLACK_MAP_H__