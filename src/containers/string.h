#ifndef __NOVA_CONTAINER_STRING_H__
#define __NOVA_CONTAINER_STRING_H__

/* TODO: Remake. This library is way behind. */

#include "../common/mem.h"

NOVA_HEADER_START

typedef struct nv_string_t nv_string_t;

struct nv_string_t
{
  unsigned        m_canary;
  char*           m_data;
  size_t          m_size;
  size_t          m_capacity;
  SDL_mutex*      m_mutex;
  nv_allocator_t* m_alloc;
};

/*
    initial_size may be 0
*/
extern nv_string_t nv_string_init(size_t initial_size, nv_allocator_t* allocator);
extern nv_string_t nv_string_init_str(const char* init, nv_allocator_t* allocator);
extern nv_string_t nv_string_init_ptr(const char* begin, const char* end, nv_allocator_t* allocator);
extern nv_string_t nv_string_substring(const nv_string_t* str, size_t start, size_t length, nv_allocator_t* new_allocator);
extern void        nv_string_destroy(nv_string_t* str);

extern void        nv_string_clear(nv_string_t* str);
extern size_t      nv_string_length(const nv_string_t* str);
extern size_t      nv_string_capacity(const nv_string_t* str);
extern const char* nv_string_data(const nv_string_t* str);

extern void nv_string_append(nv_string_t* str, const char* suffix);
extern void nv_string_append_char(nv_string_t* str, char suffix);
extern void nv_string_prepend(nv_string_t* str, const char* prefix);
extern void nv_string_set(nv_string_t* str, const char* new_str);

/* returns -1 on no find */
extern size_t nv_string_find(const nv_string_t* str, const char* substr);
extern void   nv_string_remove(nv_string_t* str, size_t index, size_t length);

extern void nv_string_copy_from(const nv_string_t* src, nv_string_t* dst);
// src is destroyed and unusable after this call!
extern void nv_string_move_from(nv_string_t* src, nv_string_t* dst);

NOVA_HEADER_END

#endif // __NOVA_STRING_H__
