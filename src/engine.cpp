#include "engine/engine.h"
#include "engine/camera.hpp"
#include "engine/collider.h"
#include "engine/input.h"
#include "engine/object.hpp"
#include "engine/scene.h"
#include "engine/ui.hpp"
#include "external/box2d/include/box2d/box2d.h"
#include "std/containers/bitset.h"
#include "std/containers/hashmap.h"
#include "std/containers/list.h"

#include "std/errorcodes.h"
#include "std/math/math.h"
#include "std/stdafx.h"
#include "std/string.h"

#include <SDL3/SDL.h>

b2BodyId _nv_collider_body_init(nv_scene_t* scene, nv_collider_type type, nv_collider_shape shape, vec2 pos, vec2 siz, uint64_t layer, uint64_t mask, bool start_enabled);
float    cast_result_fn(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context);

void
nv_window_init(const char* window_title, int window_width, int window_height, nv_ctx_t* dst)
{
  nv_assert_else_return(dst != NULL, );
  nv_assert_else_return(window_title != NULL, );
  nv_assert_else_return(window_width != 0, );
  nv_assert_else_return(window_height != 0, );

  nv_bzero(dst, sizeof(nv_ctx_t));

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  dst->window = SDL_CreateWindow(window_title, window_width, window_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  nv_assert_and_exec(dst->window != NULL, nv_log_error("%s\n", SDL_GetError()); return;);

  nv_log_info("Created window (name=%s w=%i h=%i flags=%#x)\n", window_title, window_width, window_height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  dst->sdl_time                   = SDL_GetPerformanceCounter();
  dst->current_frame              = 0;
  dst->last_frame_time            = 0;
  dst->time                       = 0.0;
  dst->delta_time                 = 0.0;
  dst->frame_start_time           = 0;
  dst->fixed_frame_start_time     = 0;
  dst->frame_time                 = 0;
  dst->window_framebuffer_resized = false;
  dst->application_running        = true;
}

void
nv_window_shutdown(nv_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, );

  SDL_DestroyWindow(ctx->window);
  SDL_Quit();
}

void
nv_consume_event(nv_ctx_t* ctx, const SDL_Event* event)
{
  if ((event->type == SDL_EVENT_QUIT) || ((event->type) && (event->window.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED))
      || (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE))
  {
    ctx->application_running = false;
  }

  if ((event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED))
  {
    ctx->window_framebuffer_resized = true;
  }
}

real_t
nv_get_last_frame_time(const nv_ctx_t* ctx)
{
  return (real_t)ctx->last_frame_time / (real_t)SDL_GetPerformanceFrequency();
}

void
nv_update(nv_ctx_t* ctx)
{
  ctx->time = (real_t)SDL_GetTicks() * (1.0 / 1000.0);

  ctx->last_frame_time = ctx->sdl_time;
  ctx->sdl_time        = SDL_GetPerformanceCounter();
  ctx->delta_time      = (real_t)(ctx->sdl_time - ctx->last_frame_time) / (real_t)SDL_GetPerformanceFrequency();
}

// nv
#define NOVA_MAX_OBJECT_COUNT 512

#define VEC2_TO_BVEC2(vec) ((b2Vec2){ vec.x, vec.y })
#define BVEC2_TO_VEC2(vec) ((vec2){ vec.x, vec.y })

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

#define VEC2_TO_BVEC2(vec) ((b2Vec2){ vec.x, vec.y })
#define BVEC2_TO_VEC2(vec) ((vec2){ vec.x, vec.y })

nv_object*
nv_object_create(nv_scene_t* scene, const char* name, nv_collider_type col_type, uint64_t layer, uint64_t mask, vec2 position, vec2 size, unsigned flags)
{
  nv_object* obj = (nv_object*)nv_list_push_empty(&scene->objects);
  obj->name      = name;
  obj->scene     = scene;

  obj->transform.position = position;
  obj->transform.size     = size;
  obj->transform.rotation = (vec4){ 0.0f, 0.0f, 0.0f, 1.0f };

  obj->spr_renderer = nv_zero_init(nv_sprite_renderer);
  // TODO: remove things when they stop working? That's the best strategy!
  // obj->spr_renderer.spr                  = nv_sprite_empty;
  obj->spr_renderer.tex_coord_multiplier = (vec2f){ 1.0f, 1.0f };
  obj->spr_renderer.color                = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f };

  obj->index = (u32)nv_list_size(&scene->objects);

  bool start_enabled = 1;
  if (flags & NOVA_OBJECT_NO_COLLISION)
  {
    start_enabled = 0;
  }
  obj->col = nv_collider_init(scene, position, size, col_type, NOVA_COLLIDER_SHAPE_RECT, layer, mask, start_enabled);

  return obj;
}

void
nv_object_destroy(nv_object* obj)
{
  if (!obj)
  {
    return;
  }

  if (obj->col)
  {
    b2DestroyBody(obj->col->body);
    b2DestroyShape(obj->col->shape, 0);
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
  if (obj->col && obj->col->enabled)
  {
    nv_collider_set_position(obj->col, to);
  }
}

vec2
nv_object_get_size(const nv_object* obj)
{
  return obj->transform.size;
}

void
nv_object_set_size(nv_object* obj, vec2 to)
{
  obj->transform.size = to;
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

b2BodyId
_nv_collider_body_init(nv_scene_t* scene, nv_collider_type type, nv_collider_shape shape, vec2 pos, vec2 siz, uint64_t layer, uint64_t mask, bool start_enabled)
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
  b2BodyId body_id  = b2CreateBody(scene->world, &body_def);

  b2ShapeDef shape_def          = b2DefaultShapeDef();
  shape_def.density             = 5.0f;
  shape_def.restitution         = 0.0f;
  shape_def.filter.categoryBits = layer;
  shape_def.filter.maskBits     = mask;
  shape_def.filter.groupIndex   = 0;

  if (shape == NOVA_COLLIDER_SHAPE_RECT)
  {
    b2Polygon poly = b2MakeBox(siz.x, siz.y);
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
    b2Capsule capsule;
    // capsule.
    nv_assert(0);
    b2CreateCapsuleShape(body_id, &shape_def, &capsule);
  }

  return body_id;
}

nv_collider_t*
nv_collider_init(nv_scene_t* scene, vec2 position, vec2 size, nv_collider_type type, nv_collider_shape shape, uint64_t layer, uint64_t mask, bool start_enabled)
{
  nv_collider_t* col = (nv_collider_t*)nv_calloc(sizeof(nv_collider_t));

  col->body       = _nv_collider_body_init(scene, type, shape, position, size, layer, mask, start_enabled);
  col->position   = position;
  col->size       = size;
  col->scene      = scene;
  col->shape_type = shape;
  col->type       = type;
  col->world      = scene->world;
  col->enabled    = start_enabled;
  col->layer      = layer;
  col->mask       = mask;

  return col;
}

void
nv_collider_destroy(nv_collider_t* col)
{
  if (col && col->enabled)
  {
    b2DestroyBody(col->body);
    nv_free(col);
  }
}

vec2
nv_collider_get_position(const nv_collider_t* col)
{
  if (col)
  {
    return col->position;
  }
  else
  {
    return nv_zero_init(vec2);
  }
}

void
nv_collider_set_position(nv_collider_t* col, vec2 to)
{
  col->position = to;
  if (col->enabled)
  {
    b2Body_SetTransform(col->body, VEC2_TO_BVEC2(to), b2MakeRot(0.0f));
    b2Body_SetAwake(col->body, 1); // wake the bodty
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

    col->body = _nv_collider_body_init(col->scene, col->type, col->shape_type, col->position, col->size, col->layer, col->mask, 1);
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
  ctx->hit->hit              = 1;
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

  b2World_CastRay(col->scene->world, VEC2_TO_BVEC2(orig), VEC2_TO_BVEC2(dir), filter, cast_result_fn, &ctx);

  return hit;
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

  if (!scene_main)
  {
    nv_scene_change_to_scene(scn);
  }
  return scn;
}

void
nv_scene_update(const nv_ctx_t* ctx)
{
  const int   substeps = 4;
  const flt_t timeStep = 1.0F / 60.0F;

  b2World_Step(scene_main->world, timeStep, substeps);

  nv_object* objects = (nv_object*)scene_main->objects.data;

  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    nv_collider_t* col = objects[i].col;
    if (!col->enabled)
    {
      continue;
    }

    b2Vec2 pos                    = b2Body_GetPosition(col->body);
    objects[i].transform.position = BVEC2_TO_VEC2(pos);
    objects[i].col->position      = BVEC2_TO_VEC2(pos);
  }

  const real_t dt = nv_get_delta_time(ctx);
  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    if (objects[i].update_fn)
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
    vec2                      pos          = nv_object_get_position(&objects[i]);
    vec2                      siz          = nv_object_get_size(&objects[i]);
    const nv_sprite_renderer* spr_renderer = &objects[i].spr_renderer;
    vec2f                     texmul       = spr_renderer->tex_coord_multiplier;
    if (spr_renderer->flip_horizontal)
    {
      texmul.x *= -1.0f;
    }
    if (spr_renderer->flip_vertical)
    {
      texmul.y *= -1.0f;
    }
    nv_renderer_render_quad(rd, spr_renderer->spr, spr_renderer->tex_coord_multiplier, (vec3f){ pos.x, pos.y, 0.0f }, (vec3f){ siz.x, siz.y, 0.0f }, spr_renderer->color, 0);
  }
  for (int i = 0; i < (int)nv_list_size(&scene_main->objects); i++)
  {
    if (objects[i].render_fn)
    {
      objects[i].render_fn(rd);
    }
  }
}
// nv

void
nv_scene_destroy(nv_scene_t* scene)
{
  if (!scene)
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
  if (scene_main && scene_main->unload)
  {
    scene_main->unload(scene);
    nv_scene_destroy(scene_main);
  }
  if (scene->load)
  {
    scene->load(scene);
  }
  scene_main = scene;
}

// nvui
typedef struct nvui_context
{
  nv_list_t btons;
  nv_list_t sliders;
  void*     ubmapped;
  bool      active;
} nvui_context;

nvui_context nvui_ctx;

void
nvui_init(void)
{
  nvui_ctx.active = true;
  nv_list_init(sizeof(nvui_button), 4, nv_allocator_c, NULL, &nvui_ctx.btons);
  nv_list_init(sizeof(nvui_slider), 4, nv_allocator_c, NULL, &nvui_ctx.sliders);
}

void
nvui_shutdown(void)
{
  if (!nvui_ctx.active)
  {
    return;
  }
  nv_list_destroy(&nvui_ctx.btons);
  nv_list_destroy(&nvui_ctx.sliders);
  nvui_ctx.active = false;
}

nvui_button*
nvui_create_button(nv_sprite_t* spr)
{
  if (!nvui_ctx.active)
  {
    nv_log_error("nvui not initialized\n");
    return NULL;
  }
  nvui_button bton        = nv_zero_init(nvui_button);
  bton.transform.position = nv_zero_init(vec2);
  bton.transform.size     = (vec2){ 0.5f, 0.5f };
  bton.color              = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f };
  bton.spr                = spr;
  nv_list_push_back(&nvui_ctx.btons, &bton);
  return &((nvui_button*)nv_list_data(&nvui_ctx.btons))[nv_list_size(&nvui_ctx.btons) - 1];
}

nvui_slider*
nvui_create_slider(nv_sprite_t* foreground, nv_sprite_t* background)
{
  if (!nvui_ctx.active)
  {
    nv_log_error("nvui not initialized\n");
    return NULL;
  }
  nvui_slider slider        = nv_zero_init(nvui_slider);
  slider.transform.position = nv_zero_init(vec2);
  slider.transform.size     = (vec2){ 0.5f, 1.5f };
  slider.min                = 0.0f;
  slider.max                = 1.0f;
  slider.value              = 0.0f;
  slider.bg_color           = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f };
  slider.slider_color       = (vec4f){ 1.0f, 0.0f, 0.0f, 1.0f };
  slider.bg_sprite          = foreground;
  slider.slider_sprite      = background;
  slider.interactable       = false;
  nv_list_push_back(&nvui_ctx.sliders, &slider);
  return (nvui_slider*)nv_list_get(&nvui_ctx.sliders, nv_list_size(&nvui_ctx.sliders) - 1);
}

void
nvui_destroy_button(nvvk_ctx_t* vkctx, nvui_button* obj)
{
  if (!obj)
  {
    return;
  }
  nv_sprite_release(vkctx, obj->spr);
}

void
nvui_destroy_slider(nvvk_ctx_t* vkctx, nvui_slider* obj)
{
  if (!obj)
  {
    return;
  }
  nv_sprite_release(vkctx, obj->slider_sprite);
  nv_sprite_release(vkctx, obj->bg_sprite);
}

void
nvui_render(nv_renderer_t* rd)
{
  if (!nvui_ctx.active)
  {
    return;
  }
  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.btons); i++)
  {
    const nvui_button* bton = (nvui_button*)nv_list_get(&nvui_ctx.btons, i);

    const nv_transform* t = &bton->transform;

    nv_renderer_render_quad(rd, bton->spr, (vec2f){ 1.0f, 1.0f }, (vec3f){ t->position.x, t->position.y, 0.0f }, (vec3f){ t->size.x, t->size.y, 1.0f }, bton->color, 0);
  }

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.sliders); i++)
  {
    const nvui_slider* slider = (nvui_slider*)nv_list_get(&nvui_ctx.sliders, i);

    if (slider->max == slider->min)
    {
      nv_log_error("Slider %i has equal min and max\n", i);
      continue;
    }

    const nv_transform* t = &slider->transform;

    nv_renderer_render_quad(
        rd, slider->bg_sprite, (vec2f){ 1.0f, 1.0f }, (vec3f){ t->position.x, t->position.y, 0.0f }, (vec3f){ t->size.x, t->size.y, 1.0f }, slider->bg_color, 0);

    flt_t pcent = ((slider->value - slider->min) / (slider->max - slider->min));
    pcent       = NVM_CLAMP(pcent, 0.0f, 1.0f);

    nv_renderer_render_quad(
        rd,
        slider->slider_sprite,
        (vec2f){ 1.0f, 1.0f },
        (vec3f){ t->position.x + 0.5f * t->size.x * (pcent - 1.0f), t->position.y, 0.0f },
        (vec3f){ t->size.x * pcent, t->size.y, 1.0f },
        slider->slider_color,
        1);
  }
}

void
nvui_update(nv_input_ctx_t* inputctx)
{
  const bool mouse_pressed  = nv_input_is_mouse_just_signalled(inputctx, (nv_input_mouse_button)SDL_BUTTON_LEFT);
  const vec2 mouse_position = nv_camera_get_global_mouse_position(inputctx, &camera);

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.btons); i++)
  {
    nvui_button*        bton = (nvui_button*)nv_list_get(&nvui_ctx.btons, i);
    const nv_transform* t    = &bton->transform;

    const nvm_rect2d bton_rect = (nvm_rect2d){ .position = t->position, .size = v2muls(t->size, 2.0f) };
    if (nvm_is_point_inside_rect(&mouse_position, &bton_rect))
    {
      bton->was_hovered = true;
      if (mouse_pressed && bton->on_click)
      {
        bton->was_clicked = true;
        bton->on_click(bton);
      }
      else if (bton->on_hover)
      {
        bton->was_clicked = false;
        bton->on_hover(bton);
      }
    }
    else
    {
      bton->was_hovered = false;
      bton->was_clicked = false;
    }
  }

  for (int i = 0; i < (int)nv_list_size(&nvui_ctx.sliders); i++)
  {
    nvui_slider*        slider = (nvui_slider*)nv_list_get(&nvui_ctx.sliders, i);
    const nv_transform* t      = &slider->transform;

    const nvm_rect2d slider_rect = (nvm_rect2d){ .position = t->position, .size = v2muls(t->size, 2.0f) };
    if (slider->interactable && nvm_is_point_inside_rect(&mouse_position, &slider_rect))
    {
      if (nv_input_is_mouse_signalled(inputctx, NOVA_MOUSE_BUTTON_LEFT))
      {
        flt_t rel_mx     = mouse_position.x - (t->position.x - t->size.x * 0.5f);
        flt_t clamped_mx = NVM_CLAMP(rel_mx, 0.0f, t->size.x);
        flt_t percentage = clamped_mx / t->size.x;
        slider->value    = slider->min + (percentage * (slider->max - slider->min));
        slider->moved    = true;
      }
      else
      {
        slider->moved = false;
      }
    }
  }
}
// nvui

int
nv_input_signal_action(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (!ia)
  {
    return -1;
  }
  ia->this_frame = 1;
  if (ia->response)
  {
    ia->response(action, ia);
  }
  return 0;
}

nv_input_key_state
nv_input_get_key_state(nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  const nv_input_key_state this_frame_key_state = (nv_input_key_state)nv_bitset_access_bit(&ctx->input_kb_state, sc);
  const nv_input_key_state last_frame_key_state = (nv_input_key_state)nv_bitset_access_bit(&ctx->input_last_frame_kb_state, sc);

  // curly braces are beautiful, aren't they?
  if (this_frame_key_state && last_frame_key_state)
  {
    return NOVA_KEY_STATE_HELD;
  }
  else if (!this_frame_key_state && !last_frame_key_state)
  {
    return NOVA_KEY_STATE_NOT_HELD;
  }
  else if (this_frame_key_state && !last_frame_key_state)
  {
    return NOVA_KEY_STATE_PRESSED;
  }
  else if (!this_frame_key_state && last_frame_key_state)
  {
    return NOVA_KEY_STATE_RELEASED;
  }

  return (nv_input_key_state)-1;
}

bool
nv_input_is_key_signalled(nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_HELD;
}

bool
nv_input_is_key_unsignalled(nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_NOT_HELD;
}

bool
nv_input_is_key_just_signalled(nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_PRESSED;
}

bool
nv_input_is_key_just_unsignalled(nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_RELEASED;
}

vec2
nv_input_get_mouse_position(nv_input_ctx_t* ctx)
{
  return ctx->input_mouse_position;
}

vec2
nv_input_get_last_frame_mouse_position(nv_input_ctx_t* ctx)
{
  return ctx->input_last_frame_mouse_position;
}

vec2
nv_input_get_mouse_delta(nv_input_ctx_t* ctx)
{
  return v2sub(nv_input_get_last_frame_mouse_position(ctx), nv_input_get_mouse_position(ctx));
}

// button is 1 for left mouse, 2 for middle, 3 for right
bool
nv_input_is_mouse_just_signalled(nv_input_ctx_t* ctx, nv_input_mouse_button button)
{
  return ctx->input_mouse_state & SDL_BUTTON_MASK(button) && !(ctx->input_last_frame_mouse_state & SDL_BUTTON_MASK(button));
}

bool
nv_input_is_mouse_signalled(nv_input_ctx_t* ctx, nv_input_mouse_button button)
{
  return ctx->input_mouse_state & SDL_BUTTON_MASK((int)button);
}

unsigned str_hash(const void* key1, const void* key2, size_t keysize);

unsigned
str_hash(const void* key1, const void* key2, size_t keysize)
{
  (void)keysize;
  const char* str1 = (const char*)key1;
  const char* str2 = (const char*)key2;

  const u32 FNV_OFFSET_BASIS = 2166136261U;
  const u32 FNV_PRIME        = 16777619U;

  unsigned hash = FNV_OFFSET_BASIS;
  for (const char* its = str1; *its; its++)
  {
    hash = (hash ^ (u32)*its) * FNV_PRIME;
  }
  for (const char* its = str2; *its; its++)
  {
    hash = (hash ^ (u32)*its) * FNV_PRIME;
  }
  return hash;
}

nv_error
nv_input_init(nv_input_ctx_t* ctx)
{
  nv_error code = NV_SUCCESS;

  if ((code = nv_hashmap_init(16, sizeof(const char*), sizeof(nv_input_action_t), nv_hash_fnv1a, nv_allocator_c, NULL, &ctx->input_action_mapping)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_bitset_init(SDL_SCANCODE_COUNT, nv_allocator_c, &ctx->input_kb_state)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_bitset_init(SDL_SCANCODE_COUNT, nv_allocator_c, &ctx->input_last_frame_kb_state)) != NV_SUCCESS)
  {
    return code;
  }

  ctx->input_mouse_position            = nv_zero_init(vec2);
  ctx->input_last_frame_mouse_position = nv_zero_init(vec2);

  return NV_SUCCESS;
}

void
nv_input_shutdown(nv_input_ctx_t* ctx)
{
  if (!ctx)
  {
    return;
  }

  nv_hashmap_destroy(&ctx->input_action_mapping);
  nv_bitset_destroy(&ctx->input_kb_state);
  nv_bitset_destroy(&ctx->input_last_frame_kb_state);

  nv_bzero(ctx, sizeof(nv_input_ctx_t));
}

void
nv_input_update(nv_input_ctx_t* ctx, nv_ctx_t* globalctx)
{
  float mx, my;
  ctx->input_last_frame_mouse_state = ctx->input_mouse_state;
  ctx->input_mouse_state            = SDL_GetMouseState(&mx, &my);

  const flt_t width  = (flt_t)nv_get_window_size(globalctx).width;
  const flt_t height = (flt_t)nv_get_window_size(globalctx).height;

  ctx->input_last_frame_mouse_position = ctx->input_mouse_position;
  ctx->input_mouse_position.x          = ((flt_t)mx / width) * 2.0f - 1.0f;
  ctx->input_mouse_position.y          = ((flt_t)my / height) * 2.0f - 1.0f;
  ctx->input_mouse_position.y *= -1.0f;

  const bool* sdl_kb_state = SDL_GetKeyboardState(NULL);
  nv_bitset_copy_from(&ctx->input_last_frame_kb_state, &ctx->input_kb_state);
  for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
  {
    nv_bitset_set_bit_to(&ctx->input_kb_state, i, (nv_bitset_bit)sdl_kb_state[i]);
  }

  u32 mouse_state = SDL_GetMouseState(NULL, NULL);

  size_t             __i = 0;
  nv_hashmap_node_t* node;
  while ((node = nv_hashmap_iterate_unsafe(&ctx->input_action_mapping, &__i)) != NULL)
  {
    nv_input_action_t* ia = (nv_input_action_t*)node->value;

    ia->last_frame = ia->this_frame;

    if (ia->key != 0)
    { // key2 is not checked, most ia's won't have one
      ia->this_frame = sdl_kb_state[ia->key] || sdl_kb_state[ia->key2];
      if (ia->response)
      {
        ia->response((const char*)node->key, ia);
      }
    }
    // ia->mouse is checked independently
    if (ia->mouse != 255)
    {
      // it's or'd with ia->this_frame so that we can call the response multiple times, as expected.
      ia->this_frame = ia->this_frame || (mouse_state & SDL_BUTTON_MASK(ia->mouse)) != 0;
      if (ia->response)
      {
        ia->response((const char*)node->key, ia);
      }
    }
    else
    {
      ia->this_frame = false;
    }
  }
}

void
nv_input_bind_function_to_action(nv_input_ctx_t* ctx, const char* action, nv_input_action_response_fn response)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return;
  }
  ia->response = response;
}

void
nv_input_bind_key_to_action(nv_input_ctx_t* ctx, SDL_Scancode key, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (!ia)
  {
    nv_input_action_t w = { .key = key };
    nv_hashmap_insert(&ctx->input_action_mapping, action, &w, NULL);
  }
  else
  {
    if (ia->key != SDL_SCANCODE_UNKNOWN)
    {
      ia->key2 = key;
    }
    else
    {
      ia->key = key;
    }
  }
}

void
nv_input_bind_mouse_to_action(nv_input_ctx_t* ctx, int bton, const char* action)
{
  nv_input_action_t ia = nv_zero_init(nv_input_action_t);
  ia.mouse             = bton;
  nv_hashmap_insert(&ctx->input_action_mapping, action, &ia, NULL);
}

void
nv_input_unbind_action(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return;
  }
  ia->key      = SDL_SCANCODE_UNKNOWN;
  ia->key2     = SDL_SCANCODE_UNKNOWN;
  ia->mouse    = 255;
  ia->response = NULL;
}

bool
nv_input_is_action_signalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return false;
  }
  return ia->this_frame && ia->last_frame;
}

bool
nv_input_is_action_just_signalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return false;
  }
  return ia->this_frame && !ia->last_frame;
}

bool
nv_input_is_action_unsignalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return false;
  }
  return !ia->this_frame && !ia->last_frame;
}

bool
nv_input_is_action_just_unsignalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action, NULL);
  if (ia == NULL)
  {
    return false;
  }
  return !ia->this_frame && ia->last_frame;
}
