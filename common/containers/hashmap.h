#ifndef __NOVA_HASHMAP_H__
#define __NOVA_HASHMAP_H__

#include "../../common/mem.h"
#include "../../std/string.h"
#include <stdbool.h>
#include <stdio.h>

NOVA_HEADER_START

typedef struct nv_hashmap_t      nv_hashmap_t;
typedef struct nv_hashmap_node_t nv_hashmap_node_t;

typedef unsigned (*nv_hashmap_hash_fn)(const void* bytes, int nbytes);
typedef bool (*nv_hashmap_key_equal_fn)(const void* NV_RESTRICT key1, const void* NV_RESTRICT key2, unsigned long keysize);

static inline unsigned
nv_hashmap_std_hash(const void* bytes, int nbytes)
{
  const unsigned       FNV_PRIME    = 16777619;
  const unsigned       OFFSET_BASIS = 2166136261;
  const unsigned char* read         = (unsigned char*)bytes;
  unsigned             hash         = OFFSET_BASIS;
  for (int byte = 0; byte < nbytes; byte++)
  {
    hash ^= read[byte]; // xor
    hash *= FNV_PRIME;
  }
  return hash;
}

static inline bool
nv_hashmap_std_key_eq(const void* NV_RESTRICT key1, const void* NV_RESTRICT key2, unsigned long nbytes)
{
  return nv_memcmp(key1, key2, nbytes) == 0;
}

/*
    hash_fn may be NULL for the standard FNV-1A function.
    equal_fn may also be NULL for standard memcmp == 0
*/
extern void
nv_hashmap_init(int init_size, int keysize, int valuesize, nv_hashmap_hash_fn hash_fn, nv_hashmap_key_equal_fn equal_fn, nv_allocator_t* allocator, nv_hashmap_t* dst);

extern void nv_hashmap_destroy(nv_hashmap_t* map);

extern void nv_hashmap_resize(nv_hashmap_t* map, int new_size);

extern void nv_hashmap_clear(nv_hashmap_t* map);

extern size_t nv_hashmap_size(const nv_hashmap_t* map);

extern size_t nv_hashmap_capacity(const nv_hashmap_t* map);

extern size_t nv_hashmap_keysize(const nv_hashmap_t* map);

extern size_t nv_hashmap_valuesize(const nv_hashmap_t* map);

// __i needs to point to an integer initialized to 0
extern nv_hashmap_node_t* nv_hashmap_iterate(const nv_hashmap_t* NV_RESTRICT map, size_t* NV_RESTRICT __i);

extern nv_hashmap_node_t** nv_hashmap_root_node(const nv_hashmap_t* map);

/*
    WARNING: Doesn't replace the value if a key already exists!! Use nv_hashmap_insert_or_replace()
    also, if key or value is a string (const char *, not a nv_string_t or something),
    just pass in the const char *, not a pointer to it!!!
*/
extern void nv_hashmap_insert(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value);

extern void nv_hashmap_insert_or_replace(nv_hashmap_t* map, const void* NV_RESTRICT key, void* NV_RESTRICT value);

/* returns NULL on no find */
extern void* nv_hashmap_find(const nv_hashmap_t* NV_RESTRICT map, const void* NV_RESTRICT key);

// Write to the file containing each key-value pair
// Does not close or open the file
extern void nv_hashmap_serialize(nv_hashmap_t* NV_RESTRICT map, FILE* NV_RESTRICT f);

// map must have been initialized
// Does not close or open the file
extern void nv_hashmap_deserialize(nv_hashmap_t* NV_RESTRICT map, FILE* NV_RESTRICT f);

struct nv_hashmap_node_t
{
  void* key;
  void* value;
  bool  is_occupied;
  char  padding[7];
};

struct nv_hashmap_t
{
  nv_hashmap_node_t**     m_nodes;
  nv_hashmap_hash_fn      m_hash_fn;
  nv_hashmap_key_equal_fn m_equal_fn;
  size_t                  m_entries, m_size;
  size_t                  m_key_size, m_value_size;
  pthread_mutex_t         m_mutex;
  nv_allocator_t*         allocator;
  unsigned                m_canary;
  char                    padding[4];
};

NOVA_HEADER_END

#endif //__NOVA_HASHMAP_H__