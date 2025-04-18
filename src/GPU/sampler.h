#ifndef __NOVA_GPU_SAMPLER_H__
#define __NOVA_GPU_SAMPLER_H__

#include "../external/volk/volk.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

struct nvvk_driver_t;
typedef struct nv_gpu_sampler_t nv_gpu_sampler_t;

typedef enum nv_gpu_filter
{
  NV_FILTER_NEAREST = 0,
  NV_FILTER_LINEAR  = 1,
} nv_gpu_filter;

/* What to do when the texture coordinates read past the texture (> 1.0F) */
typedef enum nv_gpu_texture_address_mode
{
  /* Modulus the texture coordinates by 1 so it endlessly repeats */
  NV_TEXTURE_ADDRESS_MODE_REPEAT,

  /* Repeat, but the out of bounds reads are mirrored (laterally inverted) */
  NV_TEXTURE_ADDRESS_MODE_MIRRORED_REPEAT,

  /* Take the colors of the pixels on the EDGES of the texture */
  NV_TEXTURE_ADDRESS_MODE_CLAMP_TO_EDGE,
  NV_TEXTURE_ADDRESS_MODE_CLAMP_TO_BORDER,
} nv_gpu_texture_address_mode;

typedef struct nv_gpu_sampler_create_info
{
  nv_gpu_filter               filter;
  nv_gpu_filter               mipmap_filter;
  nv_gpu_texture_address_mode address_mode;
  flt_t                       max_anisotropy;
  flt_t                       mip_lod_bias, min_lod, max_lod;
} nv_gpu_sampler_create_info;

struct nv_gpu_sampler_t
{
  nv_gpu_filter               filter;
  nv_gpu_filter               mipmap_filter;
  nv_gpu_texture_address_mode address_mode;
  flt_t                       max_anisotropy;
  flt_t                       mip_lod_bias, min_lod, max_lod;
  VkSampler                   vksampler;

  /**
   * The reference count
   * This shouldn't be used by the user
   * It's tracked by the driver and when it hits 0
   * , the sampler (the vulkan one) is destroyed.
   */
  size_t rcount;
};

extern void nv_gpu_create_sampler(struct nvvk_driver_t* driver, const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler_t** dst);
extern void nv_gpu_destroy_sampler(struct nvvk_driver_t* driver, nv_gpu_sampler_t* sampler);

extern VkSampler nv_gpu_sampler_get(const nv_gpu_sampler_t* sampler);

NOVA_HEADER_END

#endif //__NOVA_GPU_SAMPLER_H__
