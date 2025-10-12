#ifndef NOVA_OBJECT_H
#define NOVA_OBJECT_H

#include "../std/include/math/vec2.h"
#include "../std/include/math/vec4.h"
#include "../std/include/stdafx.h"
#include "collider.h"
#include "renderer.h"
#include "scene.h"
#include "sprite_renderer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_transform
  {
    vec2 position, half_size;
    vec4 rotation;
  } nv_transform;

  typedef void (*nv_object_update_fn)(double dt);
  typedef void (*nv_object_render_fn)(nv_renderer_t* rd);
  typedef struct nv_object nv_object;

  typedef enum nv_object_flags
  {
    NOVA_OBJECT_NO_COLLISION = 1,
  } nv_object_flags;

  extern nv_object* nv_object_create(nv_scene_t* scene, const char* name, nv_collider_type col_type, uint64_t layer, uint64_t mask, vec2 position, vec2 size, unsigned flags);
  extern void       nv_object_destroy(nv_object* obj);

  extern void nv_object_assign_on_update_fn(nv_object* obj, nv_object_update_fn fn);
  extern void nv_object_assign_on_render_fn(nv_object* obj, nv_object_render_fn fn);

  extern void nv_object_move(nv_object* obj, vec2 add);

  extern vec2 nv_object_get_position(const nv_object* obj);
  extern void nv_object_set_position(nv_object* obj, vec2 to);

  extern vec2 nv_object_get_size(const nv_object* obj);
  extern void nv_object_set_size(nv_object* obj, vec2 to);

  extern const char* nv_object_get_name(nv_object* obj);
  extern void        nv_object_set_name(nv_object* obj, const char* name);

  extern nv_transform*       nv_object_get_transform(nv_object* obj);
  extern nv_collider_t*      nv_object_get_collider(nv_object* obj);
  extern nv_sprite_renderer* nv_object_get_sprite_renderer(nv_object* obj);

#ifdef __cplusplus
}
#endif

#endif // NOVA_OBJECT_H
