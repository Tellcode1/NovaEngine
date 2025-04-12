#ifndef __NOVA_SPRITE_H__
#define __NOVA_SPRITE_H__

// implementation: vk.c

#include "../GPU/descriptors.h"
#include "../GPU/texture.h"
#include "../std/format.h"

NOVA_HEADER_START

// Renderable sprite

struct nv_renderer_t;

typedef struct nv_sprite_t nv_sprite_t;

struct nv_sprite_t
{
  size_t               w, h;
  size_t               rcount;
  nv_format            fmt;
  nv_gpu_texture       tex;
  nv_gpu_memory_t      mem;
  nv_gpu_sampler       sampler;
  nv_descriptor_set_t* set;
};

extern nv_errorc nv_sprite_load_from_memory(struct nv_renderer_t* rd, const unsigned char* data, size_t w, size_t h, nv_format fmt, nv_sprite_t* dst);
extern nv_errorc nv_sprite_load_from_disk(struct nv_renderer_t* rd, const char* path, nv_sprite_t* dst);

// force destroy
extern void nv_sprite_destroy(nvvk_ctx_t* nvvkctx, nv_sprite_t* spr);

// references
extern void nv_sprite_lock(nv_sprite_t* spr);
extern void nv_sprite_release(nvvk_ctx_t* nvvkctx, nv_sprite_t* spr);

extern void            nv_sprite_get_dimensions(const nv_sprite_t* spr, size_t* w, size_t* h);
extern VkImage         nv_sprite_get_vk_image(const nv_sprite_t* spr);
extern VkImageView     nv_sprite_get_vk_image_view(const nv_sprite_t* spr);
extern VkDescriptorSet nv_sprite_get_descriptor_set(const nv_sprite_t* spr);
extern VkSampler       nv_sprite_get_sampler(const nv_sprite_t* spr);
extern nv_format       nv_sprite_get_format(const nv_sprite_t* spr);

NOVA_HEADER_END

#endif //__NOVA_SPRITE_H__
