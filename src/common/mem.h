#ifndef __NOVA_MEM_H__
#define __NOVA_MEM_H__

// implementation: core.c

#include "../containers/freelist.h"
#include "../std/stdafx.h"
#include "string.h"
#include <stddef.h>

NOVA_HEADER_START

#define NOVA_ALLOCATION_CANARY (0xBEEFDEAD)

// The smallest size of memory that the allocator will take from a page
// i.e. The smallest amount of memory handled by lmalloc internally, this chunk will be broken down into subchunks and returned.
#ifndef LMALLOC_DEFAULT_CHUNK_SIZE
#  define LMALLOC_DEFAULT_CHUNK_SIZE 512
#endif

#ifndef LMALLOC_DEFAULT_PAGE_SIZE
#  define LMALLOC_DEFAULT_PAGE_SIZE 4096
#endif

typedef struct nv_allocator_stack nv_allocator_stack;
typedef struct nv_allocator_heap  nv_allocator_heap;

// stack allocator functions.
extern void nv_allocator_stack_init(nv_allocator_stack* allocator, unsigned char* buf, size_t available);

extern void* nv_stack_alloc(nv_allocator_t* parent, size_t alignment, size_t size);
extern void* nv_stack_calloc(nv_allocator_t* parent, size_t alignment, size_t size);
extern void* nv_stack_realloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size);
extern void  nv_stack_free(nv_allocator_t* parent, void* block);

// malloc, calloc, realloc, free
extern void* nv_heap_alloc(nv_allocator_t* parent, size_t alignment, size_t size);
extern void* nv_heap_calloc(nv_allocator_t* parent, size_t alignment, size_t size);
extern void* nv_heap_realloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size);
extern void  nv_heap_free(nv_allocator_t* parent, void* block);

// custom mmap based heap allocator.
extern void nv_allocator_heap_init(nv_allocator_heap* pool);

// extern void* nv_pool_malloc(nv_allocator_t* allocator, size_t alignment, size_t size);
// extern void* nv_pool_calloc(nv_allocator_t* allocator, size_t alignment, size_t size);
// extern void* nv_pool_realloc(nv_allocator_t* allocator, void* prevblock, size_t alignment, size_t size);
// extern void  nv_pool_free(nv_allocator_t* allocator, void* block);

typedef void* (*nv_allocator_alloc_fn)(nv_allocator_t* allocator, size_t alignment, size_t size);
typedef void* (*nv_allocator_calloc_fn)(nv_allocator_t* allocator, size_t alignment, size_t size);
typedef void* (*nv_allocator_realloc_fn)(nv_allocator_t* allocator, void* prevblock, size_t alignment, size_t size);
typedef void (*nv_allocator_free_fn)(nv_allocator_t* allocator, void* block);

struct nv_allocator_t
{
  nv_allocator_alloc_fn   alloc;
  nv_allocator_calloc_fn  calloc;
  nv_allocator_realloc_fn realloc;
  nv_allocator_free_fn    free;
  void*                   context;
  void*                   user_data;
};

// malloc, realloc, free
extern struct nv_allocator_t* nv_allocator_get_default(void);

struct nv_allocator_stack
{
  unsigned char* buf;
  size_t         bufsiz;
  size_t         bufoffset;
};

struct nv_allocator_heap
{
  nv_freelist_t freelist;
};

static inline void
nv_allocator_bind_stack_allocator(struct nv_allocator_t* allocator, nv_allocator_stack* stack)
{
  allocator->alloc   = nv_stack_alloc;
  allocator->calloc  = nv_stack_calloc;
  allocator->realloc = nv_stack_realloc;
  allocator->free    = nv_stack_free;
  allocator->context = stack;
}

// static inline void
// nv_allocator_bind_heap_allocator(nv_allocator_t* allocator, nv_freelist_t* list)
// {
//   allocator->alloc   = nv_pool_malloc;
//   allocator->calloc  = nv_pool_calloc;
//   allocator->realloc = nv_pool_realloc;
//   allocator->free    = nv_pool_free;
//   allocator->context = list;
// }

NOVA_HEADER_END

#endif
