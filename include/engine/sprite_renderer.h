#ifndef __NOVA_SPRITE_RENDERER_H__
#define __NOVA_SPRITE_RENDERER_H__

// implementation: none

#include "../../std/math/vec2.h"
#include "../../std/math/vec4.h"
#include "sprite.h"

NOVA_HEADER_START

typedef struct nv_sprite_renderer
{
  nv_sprite* m_spr;
  bool       m_flip_horizontal;
  bool       m_flip_vertical;
  vec2f      m_tex_coord_multiplier; // This is multiplied with the texture coordinates while rendering.
  vec4f      m_color;
} nv_sprite_renderer;

static inline nv_sprite_renderer
nv_sprite_renderer_init(void)
{
  return (nv_sprite_renderer){
    nv_sprite_empty, 0, 0, (vec2f){ 1.0f, 1.0f }, (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f },
  };
}

NOVA_HEADER_END

#endif //__NOVA_SPRITE_RENDERER_H__
