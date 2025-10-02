
#ifndef NOVA_SPRITE_H
#define NOVA_SPRITE_H

#include "../../external/volk/volk.h"
#include "../engine/format.h"
#include "../iris/descriptors.h"
#include "../iris/memory.h"
#include "../iris/sampler.h"
#include "../iris/texture.h"
#include "../iris/types.h"
#include "../std/include/errorcodes.h"
#include "../std/include/stdafx.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Renderable sprite

  struct iris_driver;

  typedef struct nv_sprite_t nv_sprite_t;

  struct nv_sprite_t
  {
    size_t               w, h;
    size_t               rcount;
    nv_format            fmt;
    iris_texture_t       tex;
    iris_sampler_t*      sampler;
    nv_descriptor_set_t* set;
  };

  extern nv_error nv_sprite_load_from_memory(struct iris_driver* driver, const unsigned char* data, size_t w, size_t h, nv_format fmt, nv_sprite_t* dst);
  extern nv_error nv_sprite_load_from_disk(struct iris_driver* driver, const char* path, nv_sprite_t* dst);

  // force destroy
  extern void nv_sprite_destroy(nvvk_ctx_t* vkctx, nv_sprite_t* spr);

  // references
  extern void nv_sprite_lock(nv_sprite_t* spr);
  extern void nv_sprite_release(nvvk_ctx_t* vkctx, nv_sprite_t* spr);

  extern void            nv_sprite_get_dimensions(const nv_sprite_t* spr, size_t* w, size_t* h);
  extern VkImage         nv_sprite_get_vk_image(const nv_sprite_t* spr);
  extern VkImageView     nv_sprite_get_vk_image_view(const nv_sprite_t* spr);
  extern VkDescriptorSet nv_sprite_get_descriptor_set(const nv_sprite_t* spr);
  extern VkSampler       nv_sprite_get_sampler(const nv_sprite_t* spr);
  extern nv_format       nv_sprite_get_format(const nv_sprite_t* spr);

#ifdef __cplusplus
}
#endif

#endif // NOVA_SPRITE_H
