#ifndef __NOVA_SPRITE_H__
#define __NOVA_SPRITE_H__

// implementation: vk.c

#include "../GPU/descriptors.hpp"
#include "../GPU/sampler.hpp"
#include "../GPU/texture.hpp"
#include "../engine/format.hpp"

NOVA_HEADER_START

// Renderable sprite

struct nvvk_driver;

typedef struct nv_sprite_t nv_sprite_t;

struct nv_sprite_t
{
  size_t               w, h;
  size_t               rcount;
  nv_format            fmt;
  nv_gpu_texture       tex;
  nv_gpu_memory_t      mem;
  nv_gpu_sampler_t*    sampler;
  nv_descriptor_set_t* set;
};

extern nv_error nv_sprite_load_from_memory(nvvk_driver* driver, const unsigned char* data, size_t w, size_t h, nv_format fmt, nv_sprite_t* dst);
extern nv_error nv_sprite_load_from_disk(nvvk_driver* driver, const char* path, nv_sprite_t* dst);

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

NOVA_HEADER_END

#endif //__NOVA_SPRITE_H__
