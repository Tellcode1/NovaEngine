#ifndef __VK_STDAFX_H__
#define __VK_STDAFX_H__

// implementation: none,vk.c

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux) || defined(__unix)
#  define VK_USE_PLATFORM_XCB_KHR
#else
#  error implement
#endif

#include "../external/volk/volk.h"
#include "../std/stdafx.h"
#include "types.h"

NOVA_HEADER_START

static inline vk_size_t
_align_up_size(vk_size_t sz, vk_size_t align)
{
  /* The previous implementation was technically */
  /* next power of two, so it was causing errors. This is the closest next power of two. */
  if ((sz % align) != 0)
  {
    sz += align - sz % align;
  }
  nv_assert_else_return((sz % align) == 0, 0);
  return sz;
}

static inline void*
_align_up_ptr(void* ptr, vk_size_t align)
{
  /* The previous implementation was technically */
  /* next power of two, so it was causing errors. This is the closest next power of two. */
  if (((uintptr_t)ptr % align) != 0)
  {
    ptr = (void*)((uintptr_t)ptr + (align - ((uintptr_t)ptr) % align));
  }
  nv_assert_else_return(((uintptr_t)ptr % align) == 0, NULL);
  return ptr;
}

NOVA_HEADER_END

#endif //__VK_STDAFX_H__
