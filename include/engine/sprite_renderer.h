#ifndef ENGINE_SPRITE_RENDERER_H
#define ENGINE_SPRITE_RENDERER_H

// implementation: none

#include "../std/include/attributes.h"
#include "../std/include/math/vec2.h"
#include "../std/include/math/vec4.h"
#include "../std/include/stdafx.h"
#include "sprite.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_sprite_renderer
  {
    nv_sprite_t* spr;
    bool         flip_horizontal;
    bool         flip_vertical;
    vec2         tex_coord_multiplier; // This is multiplied with the texture coordinates while rendering.
    vec4         color;
  } NOVA_ATTR_ALIGNED(64) nv_sprite_renderer;

  static inline nv_sprite_renderer
  nv_sprite_renderer_init(void)
  {
    return (nv_sprite_renderer){
      NULL, false, false, v2one, v4one,
    };
  }

#ifdef __cplusplus
}
#endif

#endif // ENGINE_SPRITE_RENDERER_H
