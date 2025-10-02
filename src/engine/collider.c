#include "../../include/engine/collider.h"
#include "../../include/engine/scene.h"

#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>

#include "../../external/box2d/include/box2d/box2d.h"
#include "../../external/box2d/include/box2d/collision.h"
#include "../../external/box2d/include/box2d/id.h"
#include "../../external/box2d/include/box2d/math_functions.h"
#include "../../external/box2d/include/box2d/types.h"

b2BodyId nv_collider_body_init(nv_scene_t* scene, nv_collider_type type, nv_collider_shape shape, vec2 pos, vec2 siz, uint64_t layer, uint64_t mask, bool start_enabled);
float    cast_result_fn(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context);

#define NOVA_MAX_OBJECT_COUNT 512

#define VEC2_TO_BVEC2(vec) ((b2Vec2){ (vec).x, (vec).y })
#define BVEC2_TO_VEC2(vec) ((vec2){ (vec).x, (vec).y })

struct nv_collider_t
{
  b2BodyId          body;
  vec2              position;
  vec2              size;
  b2WorldId         world;
  b2ShapeId         shape;
  nv_scene_t*       scene;
  nv_collider_shape shape_type;
  nv_collider_type  type;
  bool              enabled;
  uint32_t          layer, mask;
};

b2BodyId
nv_collider_body_init(nv_scene_t* scene, nv_collider_type type, nv_collider_shape shape, vec2 pos, vec2 siz, uint64_t layer, uint64_t mask, bool start_enabled)
{
  b2BodyDef body_def = b2DefaultBodyDef();
  if (type == NOVA_COLLIDER_TYPE_STATIC)
  {
    body_def.type = b2_staticBody;
  }
  else if (type == NOVA_COLLIDER_TYPE_DYNAMIC)
  {
    body_def.type = b2_dynamicBody;
  }
  else if (type == NOVA_COLLIDER_TYPE_KINEMATIC)
  {
    body_def.type = b2_kinematicBody;
  }

  if (!start_enabled)
  {
    return nv_zero_init(b2BodyId);
  }

  body_def.position = VEC2_TO_BVEC2(pos);
  b2BodyId body_id  = b2CreateBody(nv_scene_get_world_id(scene), &body_def);

  b2ShapeDef shape_def          = b2DefaultShapeDef();
  shape_def.density             = 5.0f;
  shape_def.restitution         = 0.0f;
  shape_def.filter.categoryBits = layer;
  shape_def.filter.maskBits     = mask;
  shape_def.filter.groupIndex   = 0;

  if (shape == NOVA_COLLIDER_SHAPE_RECT)
  {
    b2Polygon const poly = b2MakeBox(siz.x, siz.y);
    b2CreatePolygonShape(body_id, &shape_def, &poly);
  }
  else if (shape == NOVA_COLLIDER_SHAPE_CIRCLE)
  {
    b2Circle circle;
    circle.center = (b2Vec2){ 0 };
    circle.radius = siz.x;
    b2CreateCircleShape(body_id, &shape_def, &circle);
  }
  else if (shape == NOVA_COLLIDER_SHAPE_CAPSULE)
  {
    b2Capsule const capsule = nv_zero_init(b2Capsule);
    b2CreateCapsuleShape(body_id, &shape_def, &capsule);
    // capsule.
    nv_assert(0);
  }

  return body_id;
}

nv_collider_t*
nv_collider_init(nv_scene_t* scene, vec2 position, vec2 size, nv_collider_type type, nv_collider_shape shape, uint64_t layer, uint64_t mask, bool start_enabled)
{
  nv_collider_t* col = (nv_collider_t*)nv_calloc(sizeof(nv_collider_t));

  col->body       = nv_collider_body_init(scene, type, shape, position, size, layer, mask, start_enabled);
  col->position   = position;
  col->size       = size;
  col->scene      = scene;
  col->shape_type = shape;
  col->type       = type;
  col->world      = nv_scene_get_world_id(scene);
  col->enabled    = start_enabled;
  col->layer      = layer;
  col->mask       = mask;

  return col;
}

void
nv_collider_destroy(nv_collider_t* col)
{
  if ((col != NULL) && col->enabled)
  {
    b2DestroyBody(col->body);
    b2DestroyShape(col->shape, false);
    nv_free(col);
  }
}

vec2
nv_collider_get_position(const nv_collider_t* col)
{
  if (col != NULL)
  {
    return col->position;
  }
  else
  {
    return v2zero;
  }
}

void
nv_collider_set_position(nv_collider_t* col, vec2 to)
{
  col->position = to;
  if (col->enabled)
  {
    b2Body_SetTransform(col->body, VEC2_TO_BVEC2(to), b2MakeRot(0.0f));
    b2Body_SetAwake(col->body, true); // wake the bodty
  }
}

vec2
nv_collider_get_size(const nv_collider_t* col)
{
  return col->size;
}

void
nv_collider_set_size(nv_collider_t* col, vec2 to)
{
  col->size = to;
  if (col->enabled)
  {
    b2DestroyBody(col->body);

    col->body = nv_collider_body_init(col->scene, col->type, col->shape_type, col->position, col->size, col->layer, col->mask, true);
  }
}

typedef struct ray_cast_context
{
  b2ShapeId            raycaster;
  nv_collider_ray_hit* hit;
  bool                 has_hit;
} ray_cast_context;

float
cast_result_fn(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
{
  (void)point;
  (void)normal;
  (void)fraction;

  ray_cast_context* ctx = (ray_cast_context*)context;
  if (nv_memcmp(&shapeId, &ctx->raycaster, sizeof(b2ShapeId)) == 0)
  {
    return -1.0f;
  }
  ctx->hit->hit              = true;
  ctx->hit->point_of_contact = BVEC2_TO_VEC2(point);
  return 0.0f;
}

nv_collider_ray_hit
nv_collider_cast_ray(const nv_collider_t* col, vec2 orig, vec2 dir, uint32_t layer, uint32_t mask)
{
  nv_collider_ray_hit hit = nv_zero_init(nv_collider_ray_hit);
  hit.host                = col;

  ray_cast_context ctx = nv_zero_init(ray_cast_context);
  ctx.raycaster        = col->shape;
  ctx.hit              = &hit;

  b2QueryFilter filter = b2DefaultQueryFilter();
  filter.categoryBits  = layer;
  filter.maskBits      = mask;

  b2World_CastRay(nv_scene_get_world_id(col->scene), VEC2_TO_BVEC2(orig), VEC2_TO_BVEC2(dir), filter, cast_result_fn, &ctx);

  return hit;
}

bool
nv_collider_is_enabled(const nv_collider_t* col)
{
  return col->enabled;
}

b2BodyId
nv_collider_get_body_id(const nv_collider_t* col)
{
  return col->body;
}
