#ifndef __NOVA_HASHMAP_H__
#define __NOVA_HASHMAP_H__

#include "../common/mem.h"
#include "../std/errorcodes.h"
#include "../std/hash.h"

NOVA_HEADER_START

#ifndef NV_HASHMAP_LOAD_FACTOR
/* If the size of the hashmap grows to more than this, it will resize */
#  define NV_HASHMAP_LOAD_FACTOR (3.0f / 4.0f)
#endif

typedef struct nv_hashmap_t      nv_hashmap_t;
typedef struct nv_hashmap_node_t nv_hashmap_node_t;

/*
  hash_fn may be NULL for the standard FNV-1A function.
  equal_fn may also be NULL for standard memcmp == 0
*/
extern nv_errorc nv_hashmap_init(size_t init_size, size_t keysize, size_t valuesize, nv_hash_fn hash_fn, nv_allocator_t* allocator, nv_hashmap_t* dst);

extern void nv_hashmap_destroy(nv_hashmap_t* map);

extern void nv_hashmap_resize(nv_hashmap_t* map, size_t new_size, void* hash_fn_arg);

extern void nv_hashmap_clear(nv_hashmap_t* map);

extern size_t nv_hashmap_size(const nv_hashmap_t* map);

extern size_t nv_hashmap_capacity(const nv_hashmap_t* map);

extern size_t nv_hashmap_keysize(const nv_hashmap_t* map);

extern size_t nv_hashmap_valuesize(const nv_hashmap_t* map);

// __i needs to point to an integer initialized to 0
extern nv_hashmap_node_t* nv_hashmap_iterate(const nv_hashmap_t* NV_RESTRICT map, size_t* NV_RESTRICT __i);

extern nv_hashmap_node_t* nv_hashmap_root_node(const nv_hashmap_t* map);

/**
  WARNING: Doesn't replace the value if a key already exists!! Use nv_hashmap_insert_or_replace()
    also, if key or value is a string (const char *, not a nv_string_t or something),
    just pass in the const char *, not a pointer to it!!!
  @param hash_fn_arg The argument provided to the hash function as "user_data"
    The hash function argument will be passed on to resize() too if it needs to be
*/
extern void nv_hashmap_insert(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value, void* hash_fn_arg);

/**
 * @param hash_fn_arg The argument provided to the hash function as "user_data"
 */
extern void nv_hashmap_insert_or_replace(nv_hashmap_t* map, const void* NV_RESTRICT key, void* NV_RESTRICT value, void* hash_fn_arg);

/**
 * @return NULL on no find
 * @param hash_fn_arg The argument provided to the hash function as "user_data"
 */
extern void* nv_hashmap_find(const nv_hashmap_t* NV_RESTRICT map, const void* NV_RESTRICT key, void* hash_fn_arg);

// Write to the file containing each key-value pair
// Does not close or open the file
extern void nv_hashmap_serialize(nv_hashmap_t* NV_RESTRICT map, FILE* NV_RESTRICT f);

// map must have been initialized
// Does not close or open the file
extern void nv_hashmap_deserialize(nv_hashmap_t* NV_RESTRICT map, FILE* NV_RESTRICT f, void* hash_fn_arg);

struct nv_hashmap_node_t
{
  void* key;
  void* value;
  u32   hash;
};

struct nv_hashmap_t
{
  unsigned           canary;
  SDL_mutex*         mutex;
  nv_hashmap_node_t* nodes;
  nv_hash_fn         hash_fn;
  size_t             entries;
  size_t             size;
  size_t             key_size;
  size_t             value_size;
  nv_allocator_t*    alloc;
};

NOVA_HEADER_END

#endif //__NOVA_HASHMAP_H__
