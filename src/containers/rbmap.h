#ifndef __NOVA_REDBLACK_MAP_H__
#define __NOVA_REDBLACK_MAP_H__

#include "../std/hash.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

/*
  Red black trees are a threat to god and must not be implemented by hand
*/

typedef enum nv_rbnode_color
{
  NOVA_RBNODE_COLOR_RED = 0,
  NOVA_RBNODE_COLOR_BLK = 1
} nv_rbnode_color;

typedef struct nv_rbmap_node_t
{
  struct nv_rbmap_node_t* m_parent;
  struct nv_rbmap_node_t* m_children[2];
  nv_rbnode_color         m_color;
  void*                   m_key;
  void*                   m_val;
} nv_rbmap_node_t;

typedef struct nv_rbmap_iterator_t
{
  nv_rbmap_node_t*  m_current;
  size_t            m_top;
  size_t            m_capacity;
  nv_rbmap_node_t** m_stack;
} nv_rbmap_iterator_t;

typedef struct nv_rbmap_t
{
  unsigned               m_canary;
  size_t                 m_key_size;
  size_t                 m_val_size;
  nv_compare_fn          m_compare_fn;
  nv_rbmap_node_t*       m_root;
  nv_rbmap_node_t**      m_nodes;
  struct nv_allocator_t* m_alloc;
} nv_rbmap_t;

extern void nv_rbmap_init(size_t key_size, size_t val_size, nv_compare_fn compare_fn, struct nv_allocator_t* alloc, nv_rbmap_t* dst);

extern void nv_rbmap_destroy(nv_rbmap_t* map);

extern void nv_rbmap_iterator_init(nv_rbmap_t* map, nv_rbmap_iterator_t* dst);

extern void nv_rbmap_iterator_reserve(nv_rbmap_iterator_t* itr, size_t num_elems);

/**
 * Get the next node pointer in an iteration
 * Returns NULL when there are no more nodes to iterate left
 */
extern nv_rbmap_node_t* nv_rbmap_iterator_next(nv_rbmap_iterator_t* itr);

extern void nv_rbmap_iterator_destroy(nv_rbmap_iterator_t* itr);

extern void nv_rbmap_left_rotate(nv_rbmap_t* map, nv_rbmap_node_t* x);
extern void nv_rbmap_right_rotate(nv_rbmap_t* map, nv_rbmap_node_t* y);

/**
 * After insertion/deletion operations, these functions will
 * Fix all violations of the color rules in the map
 * Use insert_fixup() if there was an insertion, delete fixup otherwise, simple.
 */
extern void nv_rbmap_insert_fixup(nv_rbmap_t* map, nv_rbmap_node_t* z);
extern void nv_rbmap_delete_fixup(nv_rbmap_t* map, nv_rbmap_node_t* x);

/* Find the minimum node in a subtree */
extern nv_rbmap_node_t* nv_rbmap_minimum(nv_rbmap_node_t* node);

/* Replaces one subtree as a child of its parent with another subtree */
/* Do not ask me what that means. I will get agressive. */
extern void nv_rbmap_transplant(nv_rbmap_t* map, nv_rbmap_node_t* u, nv_rbmap_node_t* v);

/**
 * @param value value to be inserted. Must not be NULL.
 * @param user_hash_data An argument passed to the comparision function. May be NULL.
 */
extern void nv_rbmap_insert(nv_rbmap_t* map, const void* key, const void* value, void* user_hash_data);

/**
 * Warning: big
 */
extern void nv_rbmap_delete(nv_rbmap_t* map, nv_rbmap_node_t* z);

extern nv_rbmap_node_t* nv_rbmap_root(nv_rbmap_t* map);

/**
 * @param user_hash_data An argument passed to the comparision function. May be NULL.
 */
extern void* nv_rbmap_find(const nv_rbmap_t* NV_RESTRICT map, const void* NV_RESTRICT key, void* user_hash_data);

NOVA_HEADER_END

#endif // __NOVA_REDBLACK_MAP_H__
