#ifndef __NOVA_SPRITE_RENDERER_H__
#define __NOVA_SPRITE_RENDERER_H__

// implementation: none

#include "../../std/math/vec2.h"
#include "../../std/math/vec4.h"
#include "sprite.h"

NOVA_HEADER_START;

typedef struct nv_renderer_t nv_renderer_t;

typedef struct nv_sprite_renderer
{
  nv_sprite* spr;
  bool       flip_horizontal;
  bool       flip_vertical;
  vec2f      tex_coord_multiplier; // This is multiplied with the texture coordinates while rendering.
  vec4f      color;
} nv_sprite_renderer;

static inline nv_sprite_renderer
nv_sprite_renderer_init()
{
  return (nv_sprite_renderer){ nv_sprite_empty, 0, 0, (vec2f){ 1.0f, 1.0f }, (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f } };
}

NOVA_HEADER_END;

#endif //__NOVA_SPRITE_RENDERER_H__