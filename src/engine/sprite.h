#ifndef __NOVA_SPRITE_H__
#define __NOVA_SPRITE_H__

// implementation: vk.c

#include "../GPU/vkstdafx.h"
#include "../common/format.h"

NOVA_HEADER_START

// Renderable sprite

struct nv_renderer_t;

typedef struct nv_sprite nv_sprite;

extern nv_sprite* nv_sprite_empty;

extern nv_sprite* nv_sprite_load_from_memory(struct nv_renderer_t* rd, const unsigned char* data, size_t w, size_t h, nv_format fmt);
extern nv_sprite* nv_sprite_load_from_disk(struct nv_renderer_t* rd, const char* path);

// force destroy
extern void nv_sprite_destroy(nv_sprite* spr);

// references
extern void nv_sprite_lock(nv_sprite* spr);
extern void nv_sprite_release(nv_sprite* spr);

extern void            nv_sprite_get_dimensions(const nv_sprite* spr, size_t* w, size_t* h);
extern VkImage         nv_sprite_get_vk_image(const nv_sprite* spr);
extern VkImageView     nv_sprite_get_vk_image_view(const nv_sprite* spr);
extern VkDescriptorSet nv_sprite_get_descriptor_set(const nv_sprite* spr);
extern VkSampler       nv_sprite_get_sampler(const nv_sprite* spr);
extern nv_format       nv_sprite_get_format(const nv_sprite* spr);

NOVA_HEADER_END

#endif //__NOVA_SPRITE_H__
