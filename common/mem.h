#ifndef __NOVA_MEM_H__
#define __NOVA_MEM_H__

#include "../common/containers/freelist.h"
#include "stdafx.h"
#include "string.h"
#include <stddef.h>

NOVA_HEADER_START;

#define NOVA_ALLOCATION_CANARY (0xBEEFDEAD)

// The smallest size of memory that the allocator will take from a page
// i.e. The smallest amount of memory handled by lmalloc internally, this chunk will be broken down into subchunks and returned.
#ifndef LMALLOC_DEFAULT_CHUNK_SIZE
#define LMALLOC_DEFAULT_CHUNK_SIZE 512
#endif

#ifndef LMALLOC_DEFAULT_PAGE_SIZE
#define LMALLOC_DEFAULT_PAGE_SIZE 4096
#endif

typedef struct nv_allocator      nv_allocator;
typedef struct nv_allocator_stack nv_allocator_stack;
typedef struct nv_allocator_heap  nv_allocator_heap;

// stack allocator functions.
extern void  nv_allocator_stack_init(nv_allocator_stack* allocator, unsigned char* buf, size_t available);

extern void* saalloc(nv_allocator* parent, size_t alignment, size_t size);
extern void* sacalloc(nv_allocator* parent, size_t alignment, size_t size);
extern void* sarealloc(nv_allocator* parent, void* prevblock, size_t alignment, size_t size);
extern void  safree(nv_allocator* parent, void* block);

// malloc, calloc, realloc, free
extern void* heapalloc(nv_allocator* parent, size_t alignment, size_t size);
extern void* heapcalloc(nv_allocator* parent, size_t alignment, size_t size);
extern void* heaprealloc(nv_allocator* parent, void* prevblock, size_t alignment, size_t size);
extern void  heapfree(nv_allocator* parent, void* block);

// custom mmap based heap allocator.
extern void  nv_allocator_heap_init(nv_allocator_heap* pool);

extern void* poolmalloc(nv_allocator* allocator, size_t alignment, size_t size);
extern void* poolcalloc(nv_allocator* allocator, size_t alignment, size_t size);
extern void* poolrealloc(nv_allocator* allocator, void* prevblock, size_t alignment, size_t size);
extern void  poolfree(nv_allocator* allocator, void* block);

typedef void* (*nv_allocator_alloc_fn)(nv_allocator* allocator, size_t alignment, size_t size);
typedef nv_allocator_alloc_fn nv_allocator_calloc_fn;
typedef void* (*nv_allocator_realloc_fn)(nv_allocator* allocator, void* prevblock, size_t alignment, size_t size);
typedef void (*nv_allocator_free_fn)(nv_allocator* allocator, void* block);

struct nv_allocator
{
  nv_allocator_alloc_fn   alloc;
  nv_allocator_calloc_fn  calloc;
  nv_allocator_realloc_fn realloc;
  nv_allocator_free_fn    free;
  void*                context;
  void*                user_data;
};

// malloc, realloc, free
extern nv_allocator nv_allocator_default;

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
nv_allocator_bind_stack_allocator(nv_allocator* allocator, nv_allocator_stack* stack)
{
  allocator->alloc   = saalloc;
  allocator->calloc  = sacalloc;
  allocator->realloc = sarealloc;
  allocator->free    = safree;
  allocator->context = stack;
}

static inline void
nv_allocator_bind_heap_allocator(nv_allocator* allocator, nv_freelist_t* list)
{
  allocator->alloc   = poolmalloc;
  allocator->calloc  = poolcalloc;
  allocator->realloc = poolrealloc;
  allocator->free    = poolfree;
  allocator->context = list;
}

NOVA_HEADER_END;

#endif