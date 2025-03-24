#ifndef __NOVA_GPU_FRAMEBUFFER_H__
#define __NOVA_GPU_FRAMEBUFFER_H__

#include "../../external/volk/volk.h"
#include "../containers/list.h"
#include "../std/string.h"
#include "texture.h"
#include "types.h"
#include "vkstdafx.h"

NOVA_HEADER_START

typedef struct nv_gpu_framebuffer_t             nv_gpu_framebuffer_t;
typedef struct nv_gpu_framebuffer_create_info_t nv_gpu_framebuffer_create_info_t;

struct nv_gpu_framebuffer_t
{
  nv_gpu_texture** m_attachments;
  size_t           m_num_attachments;
  VkRenderPass     m_pass;
  nv_extent2d      m_extent;
  size_t           m_num_layers;
  VkFramebuffer    m_handle;
};

struct nv_gpu_framebuffer_create_info_t
{
  /* how to know if attachment has been destroyed? oh no. */
  /* maybe we can use an index or something instead of a pointer */
  /* I hate my life. I hate my life. I hate my life. */
  nv_gpu_texture**  m_attachments;
  size_t           m_num_attachments;
  VkRenderPass     m_pass;
  nv_extent2d      m_extent;
  size_t           m_num_layers;
};

extern int nv_gpu_create_framebuffer(const nv_gpu_framebuffer_create_info_t* pCreateInfo, nv_gpu_framebuffer_t* dst);

extern void nv_gpu_destroy_framebuffer(nv_gpu_framebuffer_t* dst);

NOVA_HEADER_END

#endif //__NOVA_GPU_FRAMEBUFFER_H__
