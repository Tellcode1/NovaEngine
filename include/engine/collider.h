#ifndef ENGINE_COLLIDER_H
#define ENGINE_COLLIDER_H

// implementation: engine.c

#include "../std/include/attributes.h"
#include "../std/include/math/vec2.h"
#include "../std/include/stdafx.h"
#include "scene.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_collider_t nv_collider_t;

  struct b2BodyId;

  typedef enum nv_collider_type
  {
    NOVA_COLLIDER_TYPE_STATIC    = 0,
    NOVA_COLLIDER_TYPE_DYNAMIC   = 1,
    NOVA_COLLIDER_TYPE_KINEMATIC = 2
  } nv_collider_type;

  typedef enum nv_collider_shape
  {
    NOVA_COLLIDER_SHAPE_RECT    = 0,
    NOVA_COLLIDER_SHAPE_CIRCLE  = 1, // only x of size is used
    NOVA_COLLIDER_SHAPE_CAPSULE = 2, // x of size is radius and y is height.
  } nv_collider_shape;

  typedef struct nv_collider_ray_hit
  {
    const nv_collider_t* host;
    nv_collider_t*       other;
    vec2                 point_of_contact;
    bool                 hit;
  } NOVA_ATTR_ALIGNED(32) nv_collider_ray_hit;

  // mask defines the layers that the collider can collide with
  // both layer and mask must be bitmasks
  extern nv_collider_t*
              nv_collider_init(nv_scene_t* scene, vec2 position, vec2 size, nv_collider_type type, nv_collider_shape shape, uint64_t layer, uint64_t mask, bool start_enabled);
  extern void nv_collider_destroy(nv_collider_t* col);

  extern vec2 nv_collider_get_position(const nv_collider_t* col);
  extern void nv_collider_set_position(nv_collider_t* col, vec2 to);

  vec2 nv_collider_get_size(const nv_collider_t* col);
  void nv_collider_set_size(nv_collider_t* col, vec2 to);

  extern struct nv_collider_ray_hit nv_collider_cast_ray(const nv_collider_t* col, vec2 orig, vec2 dir, uint32_t layer, uint32_t mask);

  extern bool            nv_collider_is_enabled(const nv_collider_t* col);
  extern struct b2BodyId nv_collider_get_body_id(const nv_collider_t* col);

  // You need to update the colliders through luneScene_Update();

#ifdef __cplusplus
}
#endif

#endif // ENGINE_COLLIDER_H
