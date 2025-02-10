#ifndef __VK_STDAFX_H__
#define __VK_STDAFX_H__

// implementation: none,vk.c

#include "../../common/format.h"
#include "../../std/stdafx.h"

NOVA_HEADER_START;

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux) || defined(__unix)
#  define VK_USE_PLATFORM_XCB_KHR
#else
#  error implement
#endif

#define NVVK_FORWARD_DECLARE(s)                                                                                                                                               \
  typedef struct s##_T s##_T;                                                                                                                                                 \
  typedef s##_T*       s;

NVVK_FORWARD_DECLARE(VkInstance);
NVVK_FORWARD_DECLARE(VkDevice);
NVVK_FORWARD_DECLARE(VkPhysicalDevice);
NVVK_FORWARD_DECLARE(VkSurfaceKHR);
NVVK_FORWARD_DECLARE(VkDebugUtilsMessengerEXT);
NVVK_FORWARD_DECLARE(VkQueue);

extern VkInstance               instance;
extern VkDevice                 device;
extern VkPhysicalDevice         phys_device;
extern VkSurfaceKHR             surface;
extern struct SDL_Window*       window;
extern VkDebugUtilsMessengerEXT debug_messenger;

extern nv_format swap_chain_image_format;
extern u32       swap_chain_color_space;
extern u32       swap_chain_image_count;
extern u32       samples;

extern u32 graphics_family_index;
extern u32 present_family_index;
extern u32 compute_family_index;
extern u32 transfer_queue_index;
extern u32 graphics_and_compute_family_index;

extern VkQueue graphics_queue;
extern VkQueue graphics_and_compute_queue;
extern VkQueue present_queue;
extern VkQueue compute_queue;
extern VkQueue transfer_queue;

extern u32           MAX_SAMPLES;
extern unsigned char SUPPORTS_MULTISAMPLING;
extern flt_t         MAX_ANISOTROPY;

NOVA_HEADER_END;

#endif //__VK_STDAFX_H__