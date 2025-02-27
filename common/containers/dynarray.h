#ifndef __NOVA_VECTOR_H__
#define __NOVA_VECTOR_H__

#include "../mem.h"
#include <pthread.h>

NOVA_HEADER_START

#define CONT_CANARY 0xFEEF

typedef struct nv_dynarray_t
{
  unsigned               m_canary;
  size_t                 m_size;
  size_t                 m_capacity;
  size_t                 m_typesize;
  void*                  m_data;
  pthread_mutex_t        m_mutex;
  struct nv_allocator_t* m_alloc;
} nv_dynarray_t;
typedef int (*nv_dynarray_compare_fn)(const void* obj1, const void* obj2);

/*
    initial_size may be 0
*/
extern void nv_dynarray_init(size_t typesize, size_t init_size, nv_allocator_t* allocator, nv_dynarray_t* vec);
extern void nv_dynarray_destroy(nv_dynarray_t* vec);

/*
  Returns 0 if the dynamic array is valid and anything else if it's not
*/
static inline int
nv_dynarray_is_initialized(const nv_dynarray_t* arr)
{
  if (!arr)
  {
    return -1;
  }
  if (arr->m_canary != CONT_CANARY)
  {
    return -1;
  }
  if (arr->m_capacity > 0 && !arr->m_data)
  {
    return -1;
  }
  if (arr->m_typesize <= 0)
  {
    return -1;
  }
  return 0;
}

extern void nv_dynarray_resize(nv_dynarray_t* vec, size_t new_size);
extern void nv_dynarray_clear(nv_dynarray_t* vec);

extern size_t nv_dynarray_size(const nv_dynarray_t* vec);
extern size_t nv_dynarray_capacity(const nv_dynarray_t* vec);
extern size_t nv_dynarray_typesize(const nv_dynarray_t* vec);
extern void*  nv_dynarray_data(const nv_dynarray_t* vec);

extern void* nv_dynarray_back(nv_dynarray_t* vec);

extern void* nv_dynarray_get(const nv_dynarray_t* vec, size_t i);
extern void  nv_dynarray_set(nv_dynarray_t* vec, size_t i, void* elem);

// Overrides contents
extern void nv_dynarray_copy_from(const nv_dynarray_t* NV_RESTRICT src, nv_dynarray_t* NV_RESTRICT dst);
// src is destroyed and unusable after this call!
extern void nv_dynarray_move_from(nv_dynarray_t* NV_RESTRICT src, nv_dynarray_t* NV_RESTRICT dst);

extern bool nv_dynarray_empty(const nv_dynarray_t* vec);

// Doesn't mean that the two vectors ptrs are pointing to the same vector
// This'll check the size and the data only, not the capacity (why would you?)
extern bool nv_dynarray_equal(const nv_dynarray_t* vec1, const nv_dynarray_t* vec2);

// WARNING: sizeof(*elem) != vec->typesize is UNDEFINED!
extern void nv_dynarray_push_back(nv_dynarray_t* NV_RESTRICT vec, const void* NV_RESTRICT elem);

// Push a zero initialized member to the vec
// Returns a pointer to the newly added element
extern void* nv_dynarray_push_empty(nv_dynarray_t* NV_RESTRICT vec);

extern void nv_dynarray_push_set(nv_dynarray_t* NV_RESTRICT vec, const void* NV_RESTRICT arr, size_t count);

extern void nv_dynarray_pop_back(nv_dynarray_t* vec);
extern void nv_dynarray_pop_front(nv_dynarray_t* vec); // expensive

extern void nv_dynarray_insert(nv_dynarray_t* NV_RESTRICT vec, size_t index, const void* NV_RESTRICT elem);
extern void nv_dynarray_remove(nv_dynarray_t* vec, size_t index);

extern int nv_dynarray_find(const nv_dynarray_t* NV_RESTRICT vec, const void* NV_RESTRICT elem);

extern void nv_dynarray_sort(nv_dynarray_t* vec, nv_dynarray_compare_fn compare);

NOVA_HEADER_END

#endif //__NOVA_VECTOR_H__
