#include "../../include/engine/collider.h"
#include "../../include/engine/engine.h"
#include "../../include/engine/object.h"
#include "../../include/engine/renderer.h"
#include "../../include/engine/scene.h"
#include "../../include/engine/sprite_renderer.h"

#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/math/vec3.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"

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
#include "../../external/box2d/include/box2d/id.h"
#include "../../external/box2d/include/box2d/math_functions.h"
#include "../../external/box2d/include/box2d/types.h"

struct nv_object
{
  nv_transform        transform;
  nv_scene_t*         scene;
  const char*         name;
  nv_collider_t*      col;
  size_t              index;
  nv_object_update_fn update_fn;
  nv_object_render_fn render_fn;
  nv_sprite_renderer  spr_renderer;
};

struct nv_scene_t
{
  nv_list_t objects; // the child objects

  const char* scene_name;
  bool        active; // whether the scene is active or not

  b2WorldId world;

  nv_scene_load_fn   load;
  nv_scene_unload_fn unload;
};

#define VEC2_TO_BVEC2(vec) ((b2Vec2){ (vec).x, (vec).y })
#define BVEC2_TO_VEC2(vec) ((vec2){ (vec).x, (vec).y })

nv_object*
nv_object_create(nv_scene_t* scene, const char* name, nv_collider_type col_type, uint64_t layer, uint64_t mask, vec2 position, vec2 size, unsigned flags)
{
  nv_object* obj = (nv_object*)nv_list_push_empty(&scene->objects);
  obj->name      = name;
  obj->scene     = scene;

  obj->transform.position  = position;
  obj->transform.half_size = size;
  obj->transform.rotation  = (vec4){ 0.0f, 0.0f, 0.0f, 1.0f };

  obj->spr_renderer = nv_zero_init(nv_sprite_renderer);
  // TODO: remove things when they stop working? That's the best strategy!
  // obj->spr_renderer.spr                  = nv_sprite_empty;
  obj->spr_renderer.tex_coord_multiplier = (vec2){ 1.0f, 1.0f };
  obj->spr_renderer.color                = (vec4){ 1.0f, 1.0f, 1.0f, 1.0f };

  obj->index = (u32)nv_list_size(&scene->objects);

  bool start_enabled = true;
  if ((flags & NOVA_OBJECT_NO_COLLISION) != 0u)
  {
    start_enabled = false;
  }
  obj->col = nv_collider_init(scene, position, size, col_type, NOVA_COLLIDER_SHAPE_RECT, layer, mask, start_enabled);

  return obj;
}

void
nv_object_destroy(nv_object* obj)
{
  if (obj == NULL)
  {
    return;
  }

  if (obj->col != NULL)
  {
    nv_collider_destroy(obj->col);
  }
  nv_list_remove(&obj->scene->objects, obj->index);
}

void
nv_object_assign_on_update_fn(nv_object* obj, nv_object_update_fn fn)
{
  obj->update_fn = fn;
}

void
nv_object_assign_on_render_fn(nv_object* obj, nv_object_render_fn fn)
{
  obj->render_fn = fn;
}

void
nv_object_move(nv_object* obj, vec2 add)
{
  nv_object_set_position(obj, v2add(nv_object_get_position(obj), add));
}

vec2
nv_object_get_position(const nv_object* obj)
{
  return obj->transform.position;
}

void
nv_object_set_position(nv_object* obj, vec2 to)
{
  obj->transform.position = to;
  if ((obj->col != NULL))
  {
    nv_collider_set_position(obj->col, to);
  }
}

vec2
nv_object_get_size(const nv_object* obj)
{
  return obj->transform.half_size;
}

void
nv_object_set_size(nv_object* obj, vec2 to)
{
  obj->transform.half_size = to;
  nv_collider_set_size(obj->col, to);
}

nv_transform*
nv_object_get_transform(nv_object* obj)
{
  return &obj->transform;
}

nv_sprite_renderer*
nv_object_get_sprite_renderer(nv_object* obj)
{
  return &obj->spr_renderer;
}

nv_collider_t*
nv_object_get_collider(nv_object* obj)
{
  return obj->col;
}

const char*
nv_object_get_name(nv_object* obj)
{
  return obj->name;
}

void
nv_object_set_name(nv_object* obj, const char* name)
{
  obj->name = name;
}

struct b2WorldId
nv_scene_get_world_id(nv_scene_t* scene)
{
  return scene->world;
}

nv_scene_t* scene_main = NULL;

nv_scene_t*
nv_scene_init(void)
{
  nv_scene_t* scn = (nv_scene_t*)nv_calloc(sizeof(nv_scene_t));

  b2WorldDef world_def = b2DefaultWorldDef();
  world_def.gravity    = (b2Vec2){ 0.0f, -9.8f };
  scn->world           = b2CreateWorld(&world_def);
  nv_list_init(sizeof(nv_object), 4, nv_allocator_c, NULL, &scn->objects);

  if (scene_main == NULL)
  {
    nv_scene_change_to_scene(scn);
  }
  return scn;
}

void
nv_scene_update(const nv_ctx_t* ctx)
{
  const int    substeps = 4;
  const double timeStep = ctx->fixed_time_step;

  b2World_Step(scene_main->world, (float)timeStep, substeps);

  nv_object* objects = (nv_object*)scene_main->objects.data;

  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    nv_collider_t* col = objects[i].col;
    if (!nv_collider_is_enabled(col))
    {
      continue;
    }

    b2Vec2 const pos              = b2Body_GetPosition(nv_collider_get_body_id(col));
    objects[i].transform.position = BVEC2_TO_VEC2(pos);
    nv_collider_set_position(objects[i].col, BVEC2_TO_VEC2(pos));
  }

  const double dt = nv_ctx_get_delta_time(ctx);
  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    if (objects[i].update_fn != NULL)
    {
      objects[i].update_fn(dt);
    }
  }
}

void
nv_scene_render(nv_renderer_t* rd)
{
  const nv_object* objects = (nv_object*)scene_main->objects.data;

  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    vec2 const                pos          = nv_object_get_position(&objects[i]);
    vec2 const                siz          = nv_object_get_size(&objects[i]);
    const nv_sprite_renderer* spr_renderer = &objects[i].spr_renderer;
    vec2                      texmul       = spr_renderer->tex_coord_multiplier;
    if (spr_renderer->flip_horizontal)
    {
      texmul.x *= -1.0f;
    }
    if (spr_renderer->flip_vertical)
    {
      texmul.y *= -1.0f;
    }
    nv_renderer_render_quad(rd, spr_renderer->spr, spr_renderer->tex_coord_multiplier, (vec3){ pos.x, pos.y, 0.0 }, (vec3){ siz.x, siz.y, 0.0 }, spr_renderer->color, 0);
  }
  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    if (objects[i].render_fn != NULL)
    {
      objects[i].render_fn(rd);
    }
  }
}

void
nv_scene_destroy(nv_scene_t* scene)
{
  if (scene == NULL)
  {
    return;
  }
  b2DestroyWorld(scene->world);
  nv_list_destroy(&scene->objects);
  nv_free(scene);
}

void
nv_scene_assign_load_fn(nv_scene_t* scene, nv_scene_load_fn fn)
{
  scene->load = fn;
}

void
nv_scene_assign_unload_fn(nv_scene_t* scene, nv_scene_unload_fn fn)
{
  scene->unload = fn;
}

void
nv_scene_change_to_scene(nv_scene_t* scene)
{
  if (scene_main == scene)
  {
    return;
  }
  if ((scene_main != NULL) && (scene_main->unload != NULL))
  {
    scene_main->unload(scene);
    nv_scene_destroy(scene_main);
  }
  if (scene->load != NULL)
  {
    scene->load(scene);
  }
  scene_main = scene;
}