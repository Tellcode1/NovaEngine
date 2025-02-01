#ifndef __NOVA_TEXTURE_H__
#define __NOVA_TEXTURE_H__

#include "../../common/stdafx.h"
#include "../engine/renderer.h"
#include "format.h"
#include "vkstdafx.h"

NOVA_HEADER_START;

NVVK_FORWARD_DECLARE(VkImage);
NVVK_FORWARD_DECLARE(VkImageView);
NVVK_FORWARD_DECLARE(VkSampler);

typedef struct nv_gpu_sampler nv_gpu_sampler;
typedef struct nv_image       nv_image;
typedef struct nv_gpu_memory_t  nv_gpu_memory;

typedef enum nv_gpu_texture_usage
{
  NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE      = 0,
  NOVA_GPU_TEXTURE_USAGE_COLOR_TEXTURE        = 1, // color render texture
  NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE        = 2,
  NOVA_GPU_TEXTURE_USAGE_STENCIL_TEXTURE      = 3,
  NOVA_GPU_TEXTURE_USAGE_STORAGE_TEXTURE      = 4,
  NOVA_GPU_TEXTURE_USAGE_INPUT_ATTACHMENT     = 5,
  NOVA_GPU_TEXTURE_USAGE_RESOLVE_TEXTURE      = 6,
  NOVA_GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT = 7,
  NOVA_GPU_TEXTURE_USAGE_PRESENTATION         = 8 // swapchain image
} nv_gpu_texture_usage;

typedef struct nv_gpu_texture_create_info
{
  nv_format             format;
  nv_sample_count      samples;
  uint32_t             type;
  nv_gpu_texture_usage usage;
  nv_extent3D          extent;
  int                  arraylayers;
  int                  miplevels;
} nv_gpu_texture_create_info;

typedef struct nv_gpu_sampler_create_info
{
  /* VkFilter */ uint32_t             filter;
  /* VkSamplerMipmapMode */ uint32_t  mipmap_mode;
  /* VkSamplerAddressMode */ uint32_t address_mode;
  float                               max_anisotropy;
  float                               mip_lod_bias, min_lod, max_lod;
} nv_gpu_sampler_create_info;

extern void        nv_gpu_get_texture_size(const nv_gpu_texture* tex, int* w, int* h);

extern void        nv_gpu_create_texture(const nv_gpu_texture_create_info* pInfo, nv_gpu_texture** tex);

extern void        nv_gpu_texture_attach_view(nv_gpu_texture* tex, VkImageView view);

extern void        nv_gpu_bind_texture_to_memory(nv_gpu_memory* mem, size_t offset, nv_gpu_texture* tex);
extern void        nv_gpu_destroy_texture(nv_gpu_texture* tex);

extern void        nv_gpu_create_sampler(const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler** sampler);

extern void        nv_gpu_write_to_texture(nv_gpu_texture* tex, const nv_image* src);

extern VkImage     nv_gpu_texture_get(const nv_gpu_texture* tex);
extern VkImageView nv_gpu_texture_get_view(const nv_gpu_texture* tex);
extern VkSampler   nv_gpu_sampler_get(const nv_gpu_sampler* sampler);

NOVA_HEADER_END;

#endif //__NOVA_TEXTURE_H__