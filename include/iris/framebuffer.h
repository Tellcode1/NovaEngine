#ifndef IRIS_FRAMEBUFFER_H
#define IRIS_FRAMEBUFFER_H

#include "../../external/volk/volk.h"
#include "texture.h"
#include "types.h"
#include "utils.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct iris_framebuffer_t             iris_framebuffer_t;
  typedef struct iris_framebuffer_create_info_t iris_framebuffer_create_info_t;

  struct iris_framebuffer_t
  {
    VkImageView*  attachments;
    size_t        num_attachments;
    VkRenderPass  pass;
    nv_extent2d   extent;
    size_t        num_layers;
    VkFramebuffer handle;
  };

  struct iris_framebuffer_create_info_t
  {
    /* how to know if attachment has been destroyed? oh no. */
    /* maybe we can use an index or something instead of a pointer */
    /* I hate my life. I hate my life. I hate my life. */
    VkImageView* attachments;
    size_t       num_attachments;
    VkRenderPass pass;
    nv_extent2d  extent;
    size_t       num_layers;
  };

  extern int iris_create_framebuffer(nvvk_ctx_t* vkctx, const iris_framebuffer_create_info_t* pCreateInfo, iris_framebuffer_t* dst);

  extern void iris_destroy_framebuffer(nvvk_ctx_t* vkctx, iris_framebuffer_t* dst);

#ifdef __cplusplus
}
#endif

#endif // IRIS_FRAMEBUFFER_H
