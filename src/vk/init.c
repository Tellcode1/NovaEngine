#include "../../include/iris/pipeline.h"

#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"

#include "../../include/engine/engine.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/attributes.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/print.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"

#include "../../include/std/include/containers/list.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../external/volk/volk.h"

static SDL_UNUSED const char* ValidationLayers[] = {
  "VK_LAYER_KHRONOS_validation",
};

/* TODO: Should these be hard coded? */
/* Configured by a config file maybe? */
/* Add a library to load configs? Hm.. */

const char* REQUIRED_INSTANCE_EXTENSIONS[]   = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_EXTENSION_NAME, NULL };
size_t      NUM_REQUIRED_INSTANCE_EXTENSIONS = (nv_arrlen(REQUIRED_INSTANCE_EXTENSIONS) - 1);

const char* WANTED_INSTANCE_EXTENSIONS[]   = { NULL };
size_t      NUM_WANTED_INSTANCE_EXTENSIONS = nv_arrlen(WANTED_INSTANCE_EXTENSIONS) - 1;

const char* WANTED_DEVICE_EXTENSIONS[]   = { NULL };
size_t      NUM_WANTED_DEVICE_EXTENSIONS = (nv_arrlen(WANTED_DEVICE_EXTENSIONS) - 1);

const char* REQUIRED_DEVICE_EXTENSIONS[]   = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, NULL };
size_t      NUM_REQUIRED_DEVICE_EXTENSIONS = (nv_arrlen(REQUIRED_DEVICE_EXTENSIONS) - 1);

// we'll just request them as needed

static const VkPhysicalDeviceFeatures WantedFeatures = {
  .samplerAnisotropy = VK_TRUE,
};

static inline void NOVA_ATTR_FORMAT(2, 3) VK_DEBUG_LOG(bool is_error_msg, const char* fmt, ...)
{
  const char* preceder = is_error_msg ? " vkerr: " : " vkdebug: ";
  va_list     args;
  va_start(args, fmt);
  nv_log_va(__FILE__, __LINE__, "_", preceder, is_error_msg, fmt, args);
  va_end(args);
}

static inline VKAPI_ATTR VkBool32 VKAPI_CALL
nvvk_debug_messenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData)
{
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;

  VK_DEBUG_LOG(true, "%s\n", pCallbackData->pMessage);

  return VK_FALSE;
}

static inline VkResult
nvvk_default_result_check_fn(const VkResult result, const char* file, const char* func, unsigned long line)
{
  if (result == VK_SUCCESS)
  {
    return result;
  }

  const char* errstr = "vkerr";
  if (result >= 0)
  {
    errstr = "vkwarn";
  }

  const char* result_string = nvvk_vk_result_to_string(result);

  // Non fatal error codes are positive
  // So we just log OK error codes as warnings instead of errors
  nv_printf("[%s:%li] %s: %s returned %s\n", file, line, errstr, func, result_string);

  return result;
}

static inline nv_list_t
setify(u32 i1, u32 i2, u32 i3, u32 i4)
{
  nv_list_t ret;
  nv_list_init(sizeof(u32), 4, nv_allocator_c, NULL, &ret);
  const u32 nums[4] = { i1, i2, i3, i4 };
  for (int j = 0; j < (int)nv_arrlen(nums); j++)
  {
    const u32 e          = nums[j];
    bool      already_in = false;
    for (size_t i = 0; i < nv_list_size(&ret); i++)
    {
      u32* exists_ptr = (u32*)nv_list_get(&ret, i);
      if (exists_ptr == NULL)
      {
        continue;
      }
      if (e == *exists_ptr)
      {
        already_in = true;
      }
    }
    if (!already_in)
    {
      nv_list_push_back(&ret, &e);
    }
  }
  return ret;
}

static inline bool
nvvk_validate_layers()
{
  uchar buffer[2048];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  if (nv_arrlen(ValidationLayers) == 0)
  {
    return true;
  }

  bool validation_layers_available = true;

  uint32_t vk_layer_count = 0;
  vkEnumerateInstanceLayerProperties(&vk_layer_count, NULL);

  nv_assert_else_return(vk_layer_count != 0, true);

  nv_list_t vk_layer_properties;

  /**
   * this doesn't use the stack allocator, intentionally. VkLayerProperties is a whopping 520 bytes
   * and will easily overflow the stack
   */
  nv_error const code = nv_list_init(sizeof(VkLayerProperties), vk_layer_count, nv_allocator_c, NULL, &vk_layer_properties);

  if (code != NV_SUCCESS)
  {
    nv_log_error("list initialization failed : %s\n", nv_error_str(code));
    return false;
  }

  vkEnumerateInstanceLayerProperties(&vk_layer_count, (VkLayerProperties*)nv_list_data(&vk_layer_properties));

  for (int j = 0; j < (int)nv_arrlen(ValidationLayers); j++)
  {
    const char* layer       = ValidationLayers[j];
    bool        layer_found = false;
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      const VkLayerProperties* vk_layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
      nv_assert_and_exec(vk_layer != NULL, continue;);

      if (nv_strcmp(layer, vk_layer->layerName) == 0)
      {
        layer_found = true;
      }
    }
    if (!layer_found)
    {
      validation_layers_available = false;
    }
  }

  if (!validation_layers_available)
  {
    nv_log_error("=====VALIDATION LAYERS FAILED TO LOAD=====\n");
    nv_log_error("Failed to initialize validation layers. Requested layers:\n");
    for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
    {
      nv_log_error("\t%s\n", ValidationLayers[i]);
    }

    nv_log_error("Available Layers:\n");
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      const VkLayerProperties* layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
      nv_assert_and_exec(layer != NULL, continue;);
      nv_log_error("\t%s\n", layer->layerName);
    }

    /* We add the missing layers next */
    nv_log_error("But instance asked for (i.e. are not available):\n");

    nv_list_t missing_layers;
    nv_list_init(sizeof(const char*), 16, nv_allocator_estack, &stack, &missing_layers);

    for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
    {
      const char* layer          = ValidationLayers[i];
      bool        layerAvailable = false;
      for (uint32_t j = 0; j < vk_layer_count; j++)
      {
        const VkLayerProperties* vk_layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
        nv_assert_and_exec(vk_layer != NULL, continue;);

        if (nv_strcmp(layer, vk_layer->layerName) == 0)
        {
          layerAvailable = true;
          break;
        }
      }
      if (!layerAvailable)
      {
        nv_list_push_back(&missing_layers, (const void*)&layer);
      }
    }
    for (size_t i = 0; i < nv_list_size(&missing_layers); i++)
    {
      const char* layer = *(const char**)nv_list_get(&missing_layers, i);
      if (layer == NULL)
      {
        layer = "(NULL)";
      }
      nv_log_error("\t%s\n", layer);
    }

    nv_log_error("Validation layers have NOT been enabled! Do you have the vulkan-validation-layers package installed?\n");

    nv_list_destroy(&missing_layers);
  }

  nv_list_destroy(&vk_layer_properties);

  /* true, as program will exit if we failed validation */
  return validation_layers_available;
}

static inline void
nvvk_setup_debug_messenger(nvvk_ctx_t* vkctx)
{
  VkDebugUtilsMessengerCreateInfoEXT create_info = nv_zero_init(VkDebugUtilsMessengerCreateInfoEXT);
  create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = nvvk_debug_messenger;

  PFN_vkCreateDebugUtilsMessengerEXT CreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vkctx->instance, "vkCreateDebugUtilsMessengerEXT");

  if (CreateDebugUtilsMessenger == NULL)
  {
    nv_log_error(
        "vkCreateDebugUtilsMessengerEXT exported function pointer not found.\n"
        " Either the " VK_EXT_DEBUG_UTILS_EXTENSION_NAME " extension was not loaded or your driver does not support it.");
    nv_log_error("The debug messenger failed to initialize.\n");
    return;
  }

  VkResult r = CreateDebugUtilsMessenger(vkctx->instance, &create_info, &vkctx->vkalloc, &vkctx->debug_messenger);
  if (r != VK_SUCCESS)
  {
    nv_log_error("Vulkan debug messenger could not start. err %i\n", r);
    return;
  }

  VkDebugUtilsMessengerCallbackDataEXT const tmp_message = (VkDebugUtilsMessengerCallbackDataEXT){
    .sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
    .pMessage = "The vulkan debug messenger has been set up",
  };

  /* Submit a message just to notify the user */
  vkSubmitDebugUtilsMessageEXT(vkctx->instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &tmp_message);
}

/* returned_valid_extensions contains a list of valid extensions */
static inline void
nvvk_get_valid_extensions(nv_list_t* returned_valid_extensions)
{
  uint32_t           SDLExtensionCount = 0;
  const char* const* sdl_extensions;

  sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&SDLExtensionCount);
  nv_assert_else_return(sdl_extensions != NULL, );

  u32 ext_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);

  VkExtensionProperties* vk_extensions = nv_calloc(sizeof(VkExtensionProperties) * ext_count);

  for (size_t i = 0; i < NUM_REQUIRED_INSTANCE_EXTENSIONS; i++)
  {
    const char* ext = REQUIRED_INSTANCE_EXTENSIONS[i];
    nv_list_push_back(returned_valid_extensions, (const void*)&ext);
  }

  for (u32 i = 0; i < SDLExtensionCount; i++)
  {
    const char* ext = sdl_extensions[i];
    nv_list_push_back(returned_valid_extensions, (const void*)&ext);
  }

  for (u32 i = 0; i < ext_count; i++)
  {
    const char* name = vk_extensions[i].extensionName;
    for (int j = 0; j < (int)NUM_WANTED_INSTANCE_EXTENSIONS; j++)
    {
      const char* want = WANTED_INSTANCE_EXTENSIONS[j];
      if (nv_strcmp(name, want) == 0)
      {
        nv_list_push_back(returned_valid_extensions, (const void*)&name);
        break;
      }
    }
  }

  nv_free(vk_extensions);
}

static inline VkInstance
nvvk_create_instance(nvvk_ctx_t* vkctx, const char* title)
{
  if (volkInitialize() != VK_SUCCESS)
  {
    nv_log_and_abort(
        "Volk could not initialize. You probably don't have the \n"
        "vulkan loader installed. "
        "I can't do anything about that.");
  }

  VkApplicationInfo const app_info = {
    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = title,
    .applicationVersion = 0,
    .pEngineName        = "NOVA",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_0,
  };

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(&SDLExtensionCount) != NULL);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

  nv_list_t enabled_extensions;
  nv_list_init(
      sizeof(const char*),
      (NUM_REQUIRED_INSTANCE_EXTENSIONS + SDLExtensionCount + extensionCount + NUM_WANTED_INSTANCE_EXTENSIONS),
      nv_allocator_estack,
      &stack,
      &enabled_extensions);

  nvvk_get_valid_extensions(&enabled_extensions);

  VkInstanceCreateInfo instance_create_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledExtensionCount   = (u32)nv_list_size(&enabled_extensions),
    .ppEnabledExtensionNames = (const char**)nv_list_data(&enabled_extensions),
  };

#ifdef DEBUG

  if (nvvk_validate_layers()) // Layesrs validated
  {
    instance_create_info.enabledLayerCount   = nv_arrlen(ValidationLayers);
    instance_create_info.ppEnabledLayerNames = ValidationLayers;
  }
  else
  {
    instance_create_info.enabledLayerCount   = 0;
    instance_create_info.ppEnabledLayerNames = NULL;
  }

#else

  instance_create_info.enabledLayerCount   = 0;
  instance_create_info.ppEnabledLayerNames = NULL;

#endif

  nvvk_result_check(*vkctx, vkCreateInstance(&instance_create_info, &vkctx->vkalloc, &vkctx->instance));

  /* Load the volk functions as soon as available, we need them to set up the debug messengers and stuff. */
  volkLoadInstance(vkctx->instance);

#ifdef DEBUG
  nvvk_setup_debug_messenger(vkctx);

  nv_log_info("Enabled validation layers: [ ");
  for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
  {
    nv_printf("\"%s\", ", ValidationLayers[i]);
  }
  nv_printf(" ]\n");

  nv_log_info("Enabled instance extensions: [ ");
  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    nv_printf("\"%s\", ", *(const char**)nv_list_get(&enabled_extensions, i));
  }
  nv_printf(" ]\n");

#endif

  nv_list_destroy(&enabled_extensions);

  return vkctx->instance;
}

static inline void
nvvk_print_device_info(VkPhysicalDevice device)
{
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  const char* device_type_str;
  const char* device_driver_vendor;
  switch (properties.deviceType)
  {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type_str = "Discrete"; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type_str = "Integrated"; break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type_str = "Software/CPU"; break;
    default: device_type_str = "Unknown"; break;
  }

  switch (properties.vendorID)
  {
    case 0x1002: device_driver_vendor = "AMD"; break;
    case 0x10DE: device_driver_vendor = "NVIDIA"; break;
    case 0x8086: device_driver_vendor = "Intel"; break;
    default: device_driver_vendor = "Unknown Vendor"; break;
  }

  // I think it looks cleaner this way
  nv_log_info("(%s) %s\n", device_type_str, properties.deviceName);
  nv_log_info("Vulkan API Version: %u.%u.%u\n", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
  nv_log_info(
      "Driver Vendor: %s Driver Version: %u.%u.%u Device ID: %#x\n",
      device_driver_vendor,
      VK_VERSION_MAJOR(properties.driverVersion),
      VK_VERSION_MINOR(properties.driverVersion),
      VK_VERSION_PATCH(properties.driverVersion),
      properties.deviceID);
}

static inline VkPhysicalDevice
nvvk_choose_physical_device(nvvk_ctx_t* vkctx, VkInstance instance, VkSurfaceKHR surface)
{
  nv_assert_else_return(vkctx != NULL, VK_NULL_HANDLE);
  nv_assert_else_return(instance != VK_NULL_HANDLE, VK_NULL_HANDLE);
  nv_assert_else_return(surface != VK_NULL_HANDLE, VK_NULL_HANDLE);

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  uint32_t phys_device_count = 0;

  VkResult r = vkEnumeratePhysicalDevices(instance, &phys_device_count, NULL);

  if (r != VK_SUCCESS)
  {
    nv_log_and_abort("Error fetching physical devices. VkResult=%i\n", r);
  }

  if (phys_device_count == 0)
  {
    nv_log_and_abort(
        "Huuuhhh??? No physical devices found? Are you running this"
        "on a banana???\n");
  }

  nv_list_t physical_devices;
  nv_list_init(sizeof(VkPhysicalDevice), phys_device_count, nv_allocator_estack, &stack, &physical_devices);
  vkEnumeratePhysicalDevices(instance, &phys_device_count, (VkPhysicalDevice*)nv_list_data(&physical_devices));

  for (u32 devi = 0; devi < phys_device_count; devi++)
  {
    if (nv_list_get(&physical_devices, devi) == NULL)
    {
      nv_log_error("NULL physical device? Do you have enough memory?\n");
      continue;
    }
    VkPhysicalDevice device = *(VkPhysicalDevice*)nv_list_get(&physical_devices, devi);

    uint32_t format_count = 0;
    nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL));

    uint32_t present_mode_count = 0;
    nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL));

    bool extensionsAvailable = true;

    uint32_t extension_count = 0;
    nvvk_result_check(*vkctx, vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL));
    nv_list_t available_extensions;
    nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_c, NULL, &available_extensions);
    nvvk_result_check(*vkctx, vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&available_extensions)));

    for (size_t i = 0; i < NUM_WANTED_DEVICE_EXTENSIONS; i++)
    {
      const char* extension = REQUIRED_DEVICE_EXTENSIONS[i];
      bool        validated = false;
      for (u32 j = 0; j < extension_count; j++)
      {
        if (nv_strcmp(extension, ((VkExtensionProperties*)nv_list_data(&available_extensions))[j].extensionName) == 0)
        {
          validated = true;
        }
      }
      if (!validated)
      {
        nv_log_error("Failed to validate extension with name: %s\n", extension);
        extensionsAvailable = false;
      }
    }

    nv_list_destroy(&available_extensions);

    if (extensionsAvailable && format_count > 0 && present_mode_count > 0)
    {
      nvvk_print_device_info(device);
      nv_list_destroy(&physical_devices);
      return device;
    }
  }

  if (nv_list_front(&physical_devices) == NULL)
  {
    nv_log_error("No physical devices to run on.\n");
    return NULL;
  }
  VkPhysicalDevice fallback = *(VkPhysicalDevice*)nv_list_front(&physical_devices);

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(fallback, &properties);

  nv_log_error("No device found. Falling back to device(0) \"%s\".", properties.deviceName);

  nvvk_print_device_info(fallback);

  nv_list_destroy(&physical_devices);

  return fallback;
}

// WARNING: does not init available_extensions itself!!!
static inline void
nvvk_get_valid_device_extensions(nvvk_ctx_t* vkctx, nv_list_t* available_extensions)
{
  nv_assert_else_return(vkctx->phys_device != NULL, );

  u32 extension_count = 0;
  vkEnumerateDeviceExtensionProperties(vkctx->phys_device, NULL, &extension_count, NULL);
  nv_list_t extensions;
  nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_c, NULL, &extensions);
  vkEnumerateDeviceExtensionProperties(vkctx->phys_device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&extensions));

  for (size_t i = 0; i < NUM_WANTED_DEVICE_EXTENSIONS; i++)
  {
    const char* wanted = WANTED_DEVICE_EXTENSIONS[i];
    for (u32 j = 0; j < extension_count; j++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[j];
      if (nv_strcmp(wanted, ext.extensionName) == 0)
      {
        const char* ext_name_copy = nv_strdup(nv_allocator_c, NULL, ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
      }
    }
  }

  for (size_t i = 0; i < NUM_REQUIRED_DEVICE_EXTENSIONS; i++)
  {
    const char* required  = REQUIRED_DEVICE_EXTENSIONS[i];
    bool        validated = false;
    for (u32 j = 0; j < extension_count; j++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[j];
      if (nv_strcmp(required, ext.extensionName) == 0)
      {
        char* ext_name_copy = nv_strdup(nv_allocator_c, NULL, ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
        validated = true;
      }
    }

    if (!validated)
    {
      nv_log_error("Failed to validate required extension with name %s\n", required);
    }
  }

  nv_list_destroy(&extensions);
}

static inline void
nvvk_validate_queues(nvvk_ctx_t* vkctx, nv_list_t* queue_create_infos)
{
  nv_assert_else_return(vkctx->phys_device != VK_NULL_HANDLE, );
  nv_assert_else_return(vkctx->surface != VK_NULL_HANDLE, );

  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vkctx->phys_device, &queue_count, NULL);
  nv_list_t queue_families;
  nv_list_init(sizeof(VkQueueFamilyProperties), queue_count, nv_allocator_c, NULL, &queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkctx->phys_device, &queue_count, (VkQueueFamilyProperties*)nv_list_data(&queue_families));

  // Clang loves complaining about these.
  u32 graphics_family = 0, present_family = 0, compute_family = 0, transfer_family = 0;
  (void)graphics_family, (void)present_family, (void)compute_family, (void)transfer_family;

  bool found_graphics_family = false, found_present_family = false, found_compute_family = false, found_transfer_family = false;

  u32 i = 0;
  for (size_t j = 0; j < nv_list_size(&queue_families); j++)
  {
    const VkQueueFamilyProperties queue_family    = ((VkQueueFamilyProperties*)nv_list_data(&queue_families))[j];
    VkBool32                      present_support = 0u;
    nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceSupportKHR(vkctx->phys_device, i, vkctx->surface, &present_support));

    if ((queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u)
    {
      graphics_family       = i;
      found_graphics_family = true;
    }
    if ((queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
    {
      compute_family       = i;
      found_compute_family = true;
    }
    if ((queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u)
    {
      transfer_family       = i;
      found_transfer_family = true;
    }
    if (present_support != 0u)
    {
      present_family       = i;
      found_present_family = true;
    }
    if (found_graphics_family && found_compute_family && found_present_family && found_transfer_family)
    {
      break;
    }

    i++;
  }

  nv_list_t unique_queue_families = setify(graphics_family, present_family, compute_family, transfer_family);

  /**
   * Vulkan gives errores sometimes even though the spec states that if queueCount is 1,
   * Only 1 element of pQueuePriorities may be checked. Seems like an issue with the validation layers
   * Should I submit a bug report? Nah. They can deal with it.
   * It doesn not seem to be a bug with them, but the fact that this float variable is local and it goes out of scope
   * FIXED
   */
  // const float queue_priorities[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

  for (i = 0; i < nv_list_size(&unique_queue_families); i++)
  {
    VkDeviceQueueCreateInfo queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = ((u32*)nv_list_data(&unique_queue_families))[i],
      .queueCount       = 1,
      .pQueuePriorities = NULL, // we set it later to a variable initialized to 1.
    };
    nv_list_push_back(queue_create_infos, &queue_info);
  }

  nv_list_destroy(&queue_families);
  nv_list_destroy(&unique_queue_families);
}

static inline VkDevice
nvvk_create_device(nvvk_ctx_t* vkctx)
{
  nv_assert_else_return(vkctx->phys_device != VK_NULL_HANDLE, NULL);
  nv_assert_else_return(vkctx->surface != VK_NULL_HANDLE, NULL);

  nv_list_t enabled_extensions;
  nv_list_init(sizeof(const char*), NUM_WANTED_DEVICE_EXTENSIONS + NUM_WANTED_DEVICE_EXTENSIONS, nv_allocator_c, NULL, &enabled_extensions);
  nvvk_get_valid_device_extensions(vkctx, &enabled_extensions);

  nv_list_t queue_create_infos;
  nv_list_init(sizeof(VkDeviceQueueCreateInfo), 0, nv_allocator_c, NULL, &queue_create_infos);
  nvvk_validate_queues(vkctx, &queue_create_infos);

  const float queue_priority = 1.0F;
  for (size_t i = 0; i < nv_list_size(&queue_create_infos); i++)
  {
    VkDeviceQueueCreateInfo* info = (VkDeviceQueueCreateInfo*)nv_list_get(&queue_create_infos, i);
    info->pQueuePriorities        = &queue_priority;
  }

  VkDeviceCreateInfo const deviceCreateInfo = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount    = (u32)nv_list_size(&queue_create_infos),
    .pQueueCreateInfos       = (const VkDeviceQueueCreateInfo*)nv_list_data(&queue_create_infos),
    .enabledExtensionCount   = (u32)nv_list_size(&enabled_extensions),
    .ppEnabledExtensionNames = (const char* const*)nv_list_data(&enabled_extensions),
    .pEnabledFeatures        = &WantedFeatures,
  };

  nvvk_result_check(*vkctx, vkCreateDevice(vkctx->phys_device, &deviceCreateInfo, &vkctx->vkalloc, &vkctx->device));

#ifndef NDEBUG
  nv_log_info("Enabled device extensions: [ ");
  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    nv_printf("\"%s\", ", *(const char**)nv_list_get(&enabled_extensions, i));
  }
  nv_printf(" ]\n");
#endif

  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    const char* ext_name_allocated = *(const char**)nv_list_get(&enabled_extensions, i);
    nv_free((void*)ext_name_allocated);
  }
  nv_list_destroy(&enabled_extensions);
  nv_list_destroy(&queue_create_infos);

  return vkctx->device;
}

static inline nv_error
nvvk_ctx_setup_queues(nvvk_ctx_t* vkctx)
{
  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(vkctx->phys_device, &queue_count, NULL);
  nv_assert_else_return(queue_count != 0, NV_ERROR_EXTERNAL);

  nv_list_t queue_families;
  nv_list_init(sizeof(VkQueueFamilyProperties), queue_count, nv_allocator_c, NULL, &queue_families);
  nv_assert_else_return(nv_list_data(&queue_families) != NULL, NV_ERROR_EXTERNAL);

  vkGetPhysicalDeviceQueueFamilyProperties(vkctx->phys_device, &queue_count, (VkQueueFamilyProperties*)nv_list_data(&queue_families));

  u32 graphics_family             = 0;
  u32 graphics_and_compute_family = 0;
  u32 present_family              = 0;
  u32 compute_family              = 0;
  u32 transfer_family             = 0;

  bool found_graphics_family             = false;
  bool found_graphics_and_compute_family = false;
  bool found_present_family              = false;
  bool found_compute_family              = false;
  bool found_transfer_family             = false;

  u32 family_index = 0;
  for (u32 j = 0; j < queue_count; j++)
  {
    const VkQueueFamilyProperties queueFamily = ((VkQueueFamilyProperties*)nv_list_data(&queue_families))[j];
    VkBool32                      presentSupport;
    nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceSupportKHR(vkctx->phys_device, family_index, vkctx->surface, &presentSupport));

    if (((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) && ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u))
    {
      graphics_and_compute_family       = family_index;
      found_graphics_and_compute_family = true;
    }
    if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u)
    {
      graphics_family       = family_index;
      found_graphics_family = true;
    }
    if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u)
    {
      compute_family       = family_index;
      found_compute_family = true;
    }
    if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u)
    {
      transfer_family       = family_index;
      found_transfer_family = true;
    }
    if (presentSupport != 0u)
    {
      present_family       = family_index;
      found_present_family = true;
    }
    if (found_graphics_family && found_graphics_and_compute_family && found_present_family && found_compute_family && found_transfer_family)
    {
      break;
    }

    family_index++;
  }

  vkctx->graphics_family_index             = graphics_family;
  vkctx->compute_family_index              = compute_family;
  vkctx->transfer_family_index             = transfer_family;
  vkctx->present_family_index              = present_family;
  vkctx->graphics_and_compute_family_index = graphics_and_compute_family;

  vkGetDeviceQueue(vkctx->device, vkctx->graphics_family_index, 0, &vkctx->graphics_queue);
  vkGetDeviceQueue(vkctx->device, vkctx->compute_family_index, 0, &vkctx->compute_queue);
  vkGetDeviceQueue(vkctx->device, vkctx->transfer_family_index, 0, &vkctx->transfer_queue);
  vkGetDeviceQueue(vkctx->device, vkctx->present_family_index, 0, &vkctx->present_queue);
  vkGetDeviceQueue(vkctx->device, vkctx->graphics_and_compute_family_index, 0, &vkctx->graphics_and_compute_queue);

  nv_list_destroy(&queue_families);
  return NV_ERROR_SUCCESS;
}

nv_error
nvvk_ctx_init(nv_ctx_t* nvctx, nvvk_ctx_t* dst)
{
  nv_assert_else_return(nv_ctx_is_valid(nvctx) != false, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nvvk_ctx_t));

  dst->vkalloc = (VkAllocationCallbacks){
    .pUserData             = dst,
    .pfnAllocation         = nvvk_alloc,
    .pfnReallocation       = nvvk_realloc,
    .pfnFree               = nvvk_free,
    .pfnInternalAllocation = nvvk_internal_allocation,
    .pfnInternalFree       = nvvk_internal_free,
  };
  nv_error code = nvvk_allocator_init(&dst->allocator);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  dst->result_fn = nvvk_default_result_check_fn;

  dst->instance = nvvk_create_instance(dst, SDL_GetWindowTitle(nvctx->window));
  nv_assert_else_return(dst->instance != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  if (!SDL_Vulkan_CreateSurface(nvctx->window, dst->instance, &dst->vkalloc, &dst->surface))
  {
    nv_log_and_abort("Surface creation failed.\nSDL reports: %s\n", SDL_GetError());
  }
  nv_assert_else_return(dst->surface != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  dst->phys_device = nvvk_choose_physical_device(dst, dst->instance, dst->surface);
  nv_assert_else_return(dst->phys_device != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  dst->device = nvvk_create_device(dst);
  nv_assert_else_return(dst->device != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  volkLoadDevice(dst->device);

  code = nvvk_ctx_setup_queues(dst);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dst->phys_device, &props);

  dst->max_anisotropy = props.limits.maxSamplerAnisotropy;

  // TODO: is this correct?
  dst->supports_multisampling = true;

  const VkSampleCountFlags vk_samples = props.limits.framebufferColorSampleCounts;
  if ((vk_samples & VK_SAMPLE_COUNT_64_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_64_BIT;
  }
  else if ((vk_samples & VK_SAMPLE_COUNT_32_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_32_BIT;
  }
  else if ((vk_samples & VK_SAMPLE_COUNT_16_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_16_BIT;
  }
  else if ((vk_samples & VK_SAMPLE_COUNT_8_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_8_BIT;
  }
  else if ((vk_samples & VK_SAMPLE_COUNT_4_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_4_BIT;
  }
  else if ((vk_samples & VK_SAMPLE_COUNT_2_BIT) != 0u)
  {
    dst->max_samples = VK_SAMPLE_COUNT_2_BIT;
  }
  else
  {
    dst->max_samples            = VK_SAMPLE_COUNT_1_BIT;
    dst->supports_multisampling = false;
  }

  nv_memset(dst->cmd_buffers_in_use, 0, IRIS_COMMAND_BUFFER_CACHE_COUNT);

  return NV_SUCCESS;
}

void
nvvk_ctx_destroy(nvvk_ctx_t* ctx)
{
  if (ctx == NULL)
  {
    return;
  }

#if DEBUG
  vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debug_messenger, &ctx->vkalloc);
#endif

  vkDestroySurfaceKHR(ctx->instance, ctx->surface, &ctx->vkalloc);
  vkDestroyDevice(ctx->device, &ctx->vkalloc);
  vkDestroyInstance(ctx->instance, &ctx->vkalloc);

  volkFinalize();

  nvvk_allocator_destroy(&ctx->allocator);

  nv_bzero(ctx, sizeof(nvvk_ctx_t));
}