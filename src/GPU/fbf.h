#ifndef __NOVA_GPU_FRAMEBUFFER_H__
#define __NOVA_GPU_FRAMEBUFFER_H__

#include "../external/volk/volk.h"
#include "texture.h"
#include "types.h"
#include "vk.h"

NOVA_HEADER_START

typedef struct nv_gpu_framebuffer_t             nv_gpu_framebuffer_t;
typedef struct nv_gpu_framebuffer_create_info_t nv_gpu_framebuffer_create_info_t;

struct nv_gpu_framebuffer_t
{
  nv_gpu_texture** attachments;
  size_t           num_attachments;
  VkRenderPass     pass;
  nv_extent2d      extent;
  size_t           num_layers;
  VkFramebuffer    handle;
};

struct nv_gpu_framebuffer_create_info_t
{
  /* how to know if attachment has been destroyed? oh no. */
  /* maybe we can use an index or something instead of a pointer */
  /* I hate my life. I hate my life. I hate my life. */
  nv_gpu_texture** attachments;
  size_t           num_attachments;
  VkRenderPass     pass;
  nv_extent2d      extent;
  size_t           num_layers;
};

extern int nv_gpu_create_framebuffer(nvvk_ctx_t* nvvkctx, const nv_gpu_framebuffer_create_info_t* pCreateInfo, nv_gpu_framebuffer_t* dst);

extern void nv_gpu_destroy_framebuffer(nvvk_ctx_t* nvvkctx, nv_gpu_framebuffer_t* dst);

NOVA_HEADER_END

#endif //__NOVA_GPU_FRAMEBUFFER_H__
