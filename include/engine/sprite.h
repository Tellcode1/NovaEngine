#ifndef __NOVA_SPRITE_H__
#define __NOVA_SPRITE_H__

#include "../GPU/vkstdafx.h"
#include "../GPU/format.h"

NOVA_HEADER_START;

// Renderable sprite

NVVK_FORWARD_DECLARE(VkImage);
NVVK_FORWARD_DECLARE(VkImageView);
NVVK_FORWARD_DECLARE(VkDescriptorSet);
NVVK_FORWARD_DECLARE(VkSampler);

typedef struct nv_sprite nv_sprite;

extern nv_sprite *nv_sprite_empty;

extern nv_sprite *nv_sprite_load_from_memory(const unsigned char *data, int w, int h, nv_format fmt);
extern nv_sprite *nv_sprite_load_from_disk(const char *path);

// force destroy
extern void nv_sprite_destroy(nv_sprite *spr);

// references
extern void nv_sprite_lock(nv_sprite *spr);
extern void nv_sprite_release(nv_sprite *spr);

extern void nv_sprite_get_dimensions(const nv_sprite *spr, int *w, int *h);
extern VkImage nv_sprite_get_vk_image(const nv_sprite *spr);
extern VkImageView nv_sprite_get_vk_image_view(const nv_sprite *spr);
extern VkDescriptorSet nv_sprite_get_descriptor_set(const nv_sprite *spr);
extern VkSampler nv_sprite_get_sampler(const nv_sprite *spr);
extern nv_format nv_sprite_get_format(const nv_sprite *spr);

NOVA_HEADER_END;

#endif //__NOVA_SPRITE_H__