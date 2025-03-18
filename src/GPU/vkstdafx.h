#ifndef __VK_STDAFX_H__
#define __VK_STDAFX_H__

// implementation: none,vk.c

#include "../../external/volk/volk.h"
#include "../common/format.h"
#include "../std/stdafx.h"
#include "fwdefs.h"

NOVA_HEADER_START

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux) || defined(__unix)
#  define VK_USE_PLATFORM_XCB_KHR
#else
#  error implement
#endif

NOVA_HEADER_END

#endif //__VK_STDAFX_H__
