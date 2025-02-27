#ifndef __NOVA_VK_FWD_DEFS_H__
#define __NOVA_VK_FWD_DEFS_H__

#define NVVK_FORWARD_DECLARE(s)                                                                                                                                               \
  typedef struct s##_T s##_T;                                                                                                                                                 \
  typedef s##_T*(s);

NVVK_FORWARD_DECLARE(VkInstance)
NVVK_FORWARD_DECLARE(VkDevice)
NVVK_FORWARD_DECLARE(VkPhysicalDevice)
NVVK_FORWARD_DECLARE(VkSurfaceKHR)
NVVK_FORWARD_DECLARE(VkDebugUtilsMessengerEXT)
NVVK_FORWARD_DECLARE(VkQueue)

#endif //__NOVA_VK_FWD_DEFS_H__
