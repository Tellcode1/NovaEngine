#ifndef VK_STDAFX_H
#define VK_STDAFX_H

#include <stddef.h>

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux) || defined(__unix)
#  define VK_USE_PLATFORM_XCB_KHR
#else
#  error implement
#endif

#include "../../external/volk/volk.h"
#include "../std/include/stdafx.h"
#include "types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  static inline iris_size_t
  align_up_size(iris_size_t sz, iris_size_t align)
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
  align_up_ptr(void* ptr, iris_size_t align)
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

#ifdef __cplusplus
}
#endif

#endif // VK_STDAFX_H
