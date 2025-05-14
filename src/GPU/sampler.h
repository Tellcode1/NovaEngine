#ifndef __NOVA_GPU_SAMPLER_H__
#define __NOVA_GPU_SAMPLER_H__

#include "../external/volk/volk.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

struct nvvk_driver;
typedef struct nv_gpu_sampler_t nv_gpu_sampler_t;

typedef enum nv_gpu_filter
{
  NV_FILTER_NEAREST = 0,
  NV_FILTER_LINEAR  = 1,
} nv_gpu_filter;

/* What to do when the texture coordinates read past the texture (> 1.0F) */
typedef enum nv_gpu_sampler_wrap_mode
{
  /* Modulus the texture coordinates by 1 so it endlessly repeats */
  NV_GPU_SAMPLER_WRAP_MODE_REPEAT,

  /* Repeat, but the out of bounds reads are mirrored (laterally inverted) */
  NV_GPU_SAMPLER_MODE_MIRRORED_REPEAT,

  /* Take the colors of the pixels on the EDGES of the texture */
  NV_GPU_SAMPLER_MODE_CLAMP_TO_EDGE,
  NV_GPU_SAMPLER_MODE_CLAMP_TO_BORDER,
} nv_gpu_sampler_wrap_mode;

typedef enum nv_gpu_sampler_compare_mode
{
  NV_GPU_SAMPLER_COMPARE_MODE_NONE     = 1,
  NV_GPU_SAMPLER_COMPARE_MODE_LESS     = 2,
  NV_GPU_SAMPLER_COMPARE_MODE_LEQUAL   = 3,
  NV_GPU_SAMPLER_COMPARE_MODE_EQUAL    = 4,
  NV_GPU_SAMPLER_COMPARE_MODE_GEQUAL   = 5,
  NV_GPU_SAMPLER_COMPARE_MODE_GREATER  = 6,
  NV_GPU_SAMPLER_COMPARE_MODE_NOTEQUAL = 7,
  NV_GPU_SAMPLER_COMPARE_MODE_ALWAYS   = 8,
  NV_GPU_SAMPLER_COMPARE_MODE_NEVER    = 9
} nv_gpu_sampler_compare_mode;

typedef struct nv_gpu_sampler_create_info
{
  nv_gpu_filter               min_filter;
  nv_gpu_filter               mag_filter;
  nv_gpu_sampler_wrap_mode    wrapu;
  nv_gpu_sampler_wrap_mode    wrapv;
  nv_gpu_sampler_wrap_mode    wrapw;
  float                       anisotropy;
  nv_gpu_sampler_compare_mode compare_mode;
  float                       min_lod;
  float                       max_lod;
} nv_gpu_sampler_create_info;

struct nv_gpu_sampler_t
{
  nv_gpu_filter               min_filter;
  nv_gpu_filter               mag_filter;
  nv_gpu_sampler_wrap_mode    wrapu;
  nv_gpu_sampler_wrap_mode    wrapv;
  nv_gpu_sampler_wrap_mode    wrapw;
  float                       anisotropy;
  nv_gpu_sampler_compare_mode compare_mode;

  float                       min_lod;
  float                       max_lod;

  VkSampler vksampler;

  /**
   * The reference count
   * This shouldn't be used by the user
   * It's tracked by the driver and when it hits 0
   * , the sampler (the vulkan one) is destroyed.
   */
  size_t rcount;
};

extern void nv_gpu_create_sampler(struct nvvk_driver* driver, const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler_t** dst);
extern void nv_gpu_destroy_sampler(struct nvvk_driver* driver, nv_gpu_sampler_t* sampler);

extern VkSampler nv_gpu_sampler_get(const nv_gpu_sampler_t* sampler);

extern VkCompareOp nv_gpu_sampler_compare_mode_to_vk_op(nv_gpu_sampler_compare_mode mode);

NOVA_HEADER_END

#endif //__NOVA_GPU_SAMPLER_H__
