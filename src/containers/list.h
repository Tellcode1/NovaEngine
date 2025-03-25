#ifndef __NOVA_VECTOR_H__
#define __NOVA_VECTOR_H__

#include "../common/mem.h"
#include <SDL2/SDL_mutex.h>

NOVA_HEADER_START

#define CONT_CANARY 0xFEEF

typedef struct nv_list_t
{
  unsigned               canary;
  size_t                 size;
  size_t                 capacity;
  size_t                 typesize;
  void*                  data;
  SDL_mutex*             mutex;
  struct nv_allocator_t* alloc;
} nv_list_t;
typedef int (*nv_list_compare_fn)(const void* obj1, const void* obj2);

/*
    init_capacity may be 0
*/
extern void nv_list_init(size_t typesize, size_t init_capacity, nv_allocator_t* allocator, nv_list_t* vec);
extern void nv_list_destroy(nv_list_t* vec);

/*
  Returns 0 if the dynamic array is valid and anything else if it's not
*/
static inline int
nv_list_is_initialized(const nv_list_t* arr)
{
  if (arr == NULL)
  {
    return -1;
  }
  if (arr->canary != CONT_CANARY)
  {
    return -1;
  }
  if (arr->capacity > 0 && arr->data == NULL)
  {
    return -1;
  }
  if (arr->typesize <= 0)
  {
    return -1;
  }
  return 0;
}

extern void nv_list_resize(nv_list_t* vec, size_t new_size);
extern void nv_list_clear(nv_list_t* vec);

extern size_t nv_list_size(const nv_list_t* vec);
extern size_t nv_list_capacity(const nv_list_t* vec);
extern size_t nv_list_typesize(const nv_list_t* vec);
extern void*  nv_list_data(const nv_list_t* vec);

extern void* nv_list_back(nv_list_t* vec);

extern void* nv_list_get(const nv_list_t* vec, size_t i);
extern void  nv_list_set(nv_list_t* vec, size_t i, void* elem);

// Overrides contents
extern void nv_list_copy_from(const nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst);
// src is destroyed and unusable after this call!
extern void nv_list_move_from(nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst);

extern bool nv_list_empty(const nv_list_t* vec);

// Doesn't mean that the two vectors ptrs are pointing to the same vector
// This'll check the size and the data only, not the capacity (why would you?)
extern bool nv_list_equal(const nv_list_t* vec1, const nv_list_t* vec2);

// WARNING: sizeof(*elem) != vec->typesize is UNDEFINED!
extern void nv_list_push_back(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem);

// Push a zero initialized member to the vec
// Returns a pointer to the newly added element
extern void* nv_list_push_empty(nv_list_t* NV_RESTRICT vec);

extern void nv_list_push_set(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT arr, size_t count);

extern void nv_list_pop_back(nv_list_t* vec);
extern void nv_list_pop_front(nv_list_t* vec); // expensive

extern void nv_list_insert(nv_list_t* NV_RESTRICT vec, size_t index, const void* NV_RESTRICT elem);
extern void nv_list_remove(nv_list_t* vec, size_t index);

extern int nv_list_find(const nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem);

extern void nv_list_sort(nv_list_t* vec, nv_list_compare_fn compare);

NOVA_HEADER_END

#endif //__NOVA_VECTOR_H__
