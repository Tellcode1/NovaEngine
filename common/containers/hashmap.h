#ifndef __NOVA_HASHMAP_H__
#define __NOVA_HASHMAP_H__

#include "../../common/mem.h"
#include <stdbool.h>
#include <stdio.h>

NOVA_HEADER_START;

typedef struct nv_hashmap_t nv_hashmap_t;
typedef unsigned (*nv_hashmap_hash_fn)(const void* bytes, int nbytes);
typedef bool (*nv_hashmap_key_equal_fn)(const void* key1, const void* key2, unsigned long keysize);

typedef struct ch_node_t
{
  void* key;
  void* value;
  bool  is_occupied;
} ch_node_t;

struct nv_hashmap_t
{
  unsigned                m_canary;
  ch_node_t**             m_nodes;
  nv_hashmap_hash_fn      m_hash_fn;
  nv_hashmap_key_equal_fn m_equal_fn;
  int                     m_entries, m_size;
  int                     m_key_size, m_value_size;
  pthread_rwlock_t        m_rwlock;
  nv_allocator*          allocator;
};

extern bool     nv_hashmap_std_key_eq(const void* key1, const void* key2, unsigned long nbytes);
extern unsigned nv_hashmap_std_hash(const void* bytes, int nbytes);

/*
    hash_fn may be NULL for the standard FNV-1A function.
    equal_fn may also be NULL for standard memcmp == 0
*/
extern nv_hashmap_t* nv_hashmap_init(int init_size, int keysize, int valuesize, nv_hashmap_hash_fn hash_fn, nv_hashmap_key_equal_fn equal_fn, nv_allocator *allocator);

extern void          nv_hashmap_destroy(nv_hashmap_t* map);

extern void          nv_hashmap_resize(nv_hashmap_t* map, int new_size);

extern void          nv_hashmap_clear(nv_hashmap_t* map);

extern int           nv_hashmap_size(const nv_hashmap_t* map);

extern int           nv_hashmap_capacity(const nv_hashmap_t* map);

extern int           nv_hashmap_keysize(const nv_hashmap_t* map);

extern int           nv_hashmap_valuesize(const nv_hashmap_t* map);

// __i needs to point to an integer initialized to 0
extern ch_node_t*  nv_hashmap_iterate(const nv_hashmap_t* map, int* __i);

extern ch_node_t** nv_hashmap_root_node(const nv_hashmap_t* map);

/*
    WARNING: Doesn't replace the value if a key already exists!! Use nv_hashmap_insert_or_replace()
    also, if key or value is a string (const char *, not a nv_string_t or something),
    just pass in the const char *, not a pointer to it!!!
*/
extern void nv_hashmap_insert(nv_hashmap_t* map, const void* key, const void* value);

extern void nv_hashmap_insert_or_replace(nv_hashmap_t* map, const void* key, void* value);

/* returns NULL on no find */
extern void* nv_hashmap_find(const nv_hashmap_t* map, const void* key);

extern void  nv_hashmap_serialize(nv_hashmap_t* map, FILE* f);

extern void  nv_hashmap_deserialize(nv_hashmap_t* map, FILE* f);

NOVA_HEADER_END;

#endif //__NOVA_HASHMAP_H__