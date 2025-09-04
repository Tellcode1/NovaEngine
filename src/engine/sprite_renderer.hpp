#ifndef NOVAENGINE_SRC_ENGINE_SPRITE_RENDERER_H
#define NOVAENGINE_SRC_ENGINE_SPRITE_RENDERER_H

// implementation: none

#include "../std/math/vec2.h"
#include "../std/math/vec4.h"
#include "../std/stdafx.h"
#include "sprite.hpp"

NOVA_HEADER_START

typedef struct nv_sprite_renderer
{
  nv_sprite_t* spr;
  bool         flip_horizontal;
  bool         flip_vertical;
  vec2f        tex_coord_multiplier; // This is multiplied with the texture coordinates while rendering.
  vec4f        color;
} NOVA_ATTR_ALIGNED(64) nv_sprite_renderer;

static inline nv_sprite_renderer
nv_sprite_renderer_init(void)
{
  return (nv_sprite_renderer){
    NULL, 0, 0, (vec2f){ 1.0F, 1.0F }, (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F },
  };
}

NOVA_HEADER_END

#endif // NOVAENGINE_SRC_ENGINE_SPRITE_RENDERER_H
