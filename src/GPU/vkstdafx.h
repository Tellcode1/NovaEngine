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
#include "../std/format.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

NOVA_HEADER_END

#endif //__VK_STDAFX_H__
