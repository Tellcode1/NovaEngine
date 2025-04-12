#ifndef __NOVA_TEXTURE_H__
#define __NOVA_TEXTURE_H__

// implementation: vk.c

#include "../std/format.h"
#include "../std/image.h"
#include "../std/stdafx.h"
#include "memory.h"
#include "types.h"

NOVA_HEADER_START

/* TODO: redo, this is severely out of date and hacky */

typedef struct nv_gpu_sampler nv_gpu_sampler;
typedef struct nv_gpu_texture nv_gpu_texture;
struct nv_renderer_t;

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
  nv_format            format;
  nv_sample_count      samples;
  uint32_t             type;
  nv_gpu_texture_usage usage;
  nv_extent3D          extent;
  int                  arraylayers;
  int                  miplevels;
} nv_gpu_texture_create_info;

typedef struct nv_gpu_sampler_create_info
{
  VkFilter             filter;
  VkSamplerMipmapMode  mipmap_mode;
  VkSamplerAddressMode address_mode;
  flt_t                max_anisotropy;
  flt_t                mip_lod_bias, min_lod, max_lod;
} nv_gpu_sampler_create_info;

struct nv_gpu_texture
{
  nv_gpu_memory_t* memory;
  size_t           size, offset;

  VkImageLayout      layout;
  VkImageAspectFlags aspect;
  VkImageType        type;
  VkImageUsageFlags  usage;

  VkImage         image;
  VkImageView     view;
  VkExtent3D      extent;
  int             miplevels, arraylayers;
  nv_format       format;
  nv_sample_count samples;
};

struct nv_gpu_sampler
{
  VkFilter             filter;
  VkSamplerMipmapMode  mipmap_mode;
  VkSamplerAddressMode address_mode;
  flt_t                max_anisotropy;
  flt_t                mip_lod_bias, min_lod, max_lod;
  VkSampler            vksampler;
};

extern void nv_gpu_get_texture_size(const nv_gpu_texture* tex, size_t* w, size_t* h);

extern void nv_gpu_create_texture(nvvk_ctx_t* nvvkctx, const nv_gpu_texture_create_info* pInfo, nv_gpu_texture* dst);

extern void nv_gpu_texture_attach_view(nv_gpu_texture* tex, VkImageView view);

extern void nv_gpu_bind_texture_to_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* mem, size_t offset, nv_gpu_texture* tex);
extern void nv_gpu_destroy_texture(nvvk_ctx_t* nvvkctx, nv_gpu_texture* tex);

/* TODO: Make render device struct. Who the FFKJSLDKFJKSLDJFKLJ passes the entire renderer to create a sampler */
extern void nv_gpu_create_sampler(struct nv_renderer_t* rd, const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler* sampler);

extern void nv_gpu_write_to_texture(nvvk_ctx_t* nvvkctx, nv_gpu_texture* tex, const nv_image_t* src);

extern VkImage     nv_gpu_texture_get(const nv_gpu_texture* tex);
extern VkImageView nv_gpu_texture_get_view(const nv_gpu_texture* tex);
extern VkSampler   nv_gpu_sampler_get(const nv_gpu_sampler* sampler);

NOVA_HEADER_END

#endif //__NOVA_TEXTURE_H__
