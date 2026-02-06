#include "../../include/iris/sampler.h"
#include "../../external/volk/volk.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/types.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/stdafx.h"
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>

void
iris_create_sampler(iris_driver_t* driver, const iris_sampler_create_info* pInfo, iris_sampler_t* dst)
{
  nv_assert_else_return(driver != NULL, );
  nv_assert_else_return(pInfo != NULL, );
  nv_assert_else_return(dst != NULL, );

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    iris_sampler_internal_t* cache = ((iris_sampler_internal_t*)nv_list_get(&driver->samplers, i));
    if (cache == NULL)
    {
      continue;
    }

    bool const match = (cache->min_filter == pInfo->min_filter) && (cache->mag_filter == pInfo->mag_filter) && (cache->wrapu == pInfo->wrapu) && (cache->wrapv == pInfo->wrapv)
        && (cache->wrapw == pInfo->wrapw) && (cache->anisotropy == pInfo->anisotropy) && (cache->compare_mode == pInfo->compare_mode) && (cache->min_lod == pInfo->min_lod)
        && (cache->max_lod == pInfo->max_lod);

    if (cache != NULL && match && cache->handle != VK_NULL_HANDLE)
    {
      dst->ptr = cache;
      return;
    }
  }

  iris_sampler_internal_t* new_sampler = (iris_sampler_internal_t*)nv_list_push_empty(&driver->samplers);

  *new_sampler = (iris_sampler_internal_t){
    .min_filter   = pInfo->min_filter,
    .mag_filter   = pInfo->mag_filter,
    .wrapu        = pInfo->wrapu,
    .wrapv        = pInfo->wrapv,
    .wrapw        = pInfo->wrapw,
    .anisotropy   = pInfo->anisotropy,
    .compare_mode = pInfo->compare_mode,
  };

  /**
   * Store the pointer to the newly created sampler into dst.
   */

  VkPhysicalDeviceProperties device_properties = nv_zinit(VkPhysicalDeviceProperties);
  vkGetPhysicalDeviceProperties(driver->vkctx->phys_device, &device_properties);

  const VkPhysicalDeviceLimits* limits = &device_properties.limits;
  nv_assert_else_return(nv_list_size(&driver->samplers) < limits->maxSamplerAllocationCount, );

  VkSamplerCreateInfo samplerInfo = nv_zinit(VkSamplerCreateInfo);
  samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter           = (VkFilter)pInfo->mag_filter;
  samplerInfo.minFilter           = (VkFilter)pInfo->min_filter;
  samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU        = (VkSamplerAddressMode)pInfo->wrapu;
  samplerInfo.addressModeV        = (VkSamplerAddressMode)pInfo->wrapv;
  samplerInfo.addressModeW        = (VkSamplerAddressMode)pInfo->wrapw;
  samplerInfo.anisotropyEnable    = (VkBool32)(pInfo->anisotropy > 1.0F);
  samplerInfo.maxLod              = (float)pInfo->max_lod;
  samplerInfo.minLod              = (float)pInfo->min_lod;

  VkSampler sampler;
  nvvk_result_check(*driver->vkctx, vkCreateSampler(driver->vkctx->device, &samplerInfo, &driver->vkctx->vkalloc, &sampler));

  new_sampler->handle = sampler;

  dst->ptr = new_sampler;
}

VkSampler
iris_sampler_get(const iris_sampler_t* sampler)
{
  return (sampler != NULL && sampler->ptr != NULL) ? sampler->ptr->handle : VK_NULL_HANDLE;
}

VkCompareOp
iris_sampler_compare_mode_to_vk_op(iris_sampler_compare_mode mode)
{
  switch (mode)
  {
    default:
    case IRIS_SAMPLER_COMPARE_MODE_NONE: return VK_COMPARE_OP_ALWAYS;
    case IRIS_SAMPLER_COMPARE_MODE_LESS: return VK_COMPARE_OP_LESS;
    case IRIS_SAMPLER_COMPARE_MODE_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case IRIS_SAMPLER_COMPARE_MODE_EQUAL: return VK_COMPARE_OP_EQUAL;
    case IRIS_SAMPLER_COMPARE_MODE_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case IRIS_SAMPLER_COMPARE_MODE_GREATER: return VK_COMPARE_OP_GREATER;
    case IRIS_SAMPLER_COMPARE_MODE_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
    case IRIS_SAMPLER_COMPARE_MODE_ALWAYS: return VK_COMPARE_OP_ALWAYS;
    case IRIS_SAMPLER_COMPARE_MODE_NEVER: return VK_COMPARE_OP_NEVER;
  }
}