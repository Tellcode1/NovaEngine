#include "../../include/engine/input.h"
#include "../../include/engine/engine.h"
#include "../../include/engine/renderer.h"

#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/bitset.h"
#include "../../include/std/include/containers/hashmap.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/hash.h"
#include "../../include/std/include/math/vec2.h"
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

int
nv_input_signal_action(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return -1;
  }
  ia->this_frame = true;
  if (ia->response != NULL)
  {
    ia->response(action, ia);
  }
  return 0;
}

nv_input_key_state
nv_input_get_key_state(const nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  const nv_input_key_state this_frame_key_state = (nv_input_key_state)nv_bitset_access_bit(&ctx->input_kb_state, sc);
  const nv_input_key_state last_frame_key_state = (nv_input_key_state)nv_bitset_access_bit(&ctx->input_last_frame_kb_state, sc);

  // curly braces are beautiful, aren't they?
  if ((this_frame_key_state != 0u) && (last_frame_key_state != 0u))
  {
    return NOVA_KEY_STATE_HELD;
  }
  else if ((this_frame_key_state == 0u) && (last_frame_key_state == 0u))
  {
    return NOVA_KEY_STATE_NOT_HELD;
  }
  else if ((this_frame_key_state != 0u) && (last_frame_key_state == 0u))
  {
    return NOVA_KEY_STATE_PRESSED;
  }
  else if ((this_frame_key_state == 0u) && (last_frame_key_state != 0u))
  {
    return NOVA_KEY_STATE_RELEASED;
  }

  return (nv_input_key_state)-1;
}

bool
nv_input_is_key_signalled(const nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_HELD;
}

bool
nv_input_is_key_unsignalled(const nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_NOT_HELD;
}

bool
nv_input_is_key_just_signalled(const nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_PRESSED;
}

bool
nv_input_is_key_just_unsignalled(const nv_input_ctx_t* ctx, const SDL_Scancode sc)
{
  return nv_input_get_key_state(ctx, sc) == NOVA_KEY_STATE_RELEASED;
}

vec2
nv_input_get_mouse_position(const nv_input_ctx_t* ctx)
{
  return ctx->input_mouse_position;
}

vec2
nv_input_get_last_frame_mouse_position(const nv_input_ctx_t* ctx)
{
  return ctx->input_last_frame_mouse_position;
}

vec2
nv_input_get_mouse_delta(const nv_input_ctx_t* ctx)
{
  return v2sub(nv_input_get_last_frame_mouse_position(ctx), nv_input_get_mouse_position(ctx));
}

// button is 1 for left mouse, 2 for middle, 3 for right
bool
nv_input_is_mouse_just_signalled(const nv_input_ctx_t* ctx, nv_input_mouse_button button)
{
  return ((ctx->input_mouse_state & SDL_BUTTON_MASK(button)) != 0u) && ((ctx->input_last_frame_mouse_state & SDL_BUTTON_MASK(button)) == 0u);
}

bool
nv_input_is_mouse_signalled(const nv_input_ctx_t* ctx, nv_input_mouse_button button)
{
  return (ctx->input_mouse_state & SDL_BUTTON_MASK((int)button)) != 0u;
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
  for (const char* its = str1; *its != 0; its++)
  {
    hash = (hash ^ (u32)*its) * FNV_PRIME;
  }
  for (const char* its = str2; *its != 0; its++)
  {
    hash = (hash ^ (u32)*its) * FNV_PRIME;
  }
  return hash;
}

nv_error
nv_input_init(nv_ctx_t* ctx, nv_input_ctx_t* inputctx)
{
  nv_error code = NV_SUCCESS;

  if ((code = nv_hashmap_init(16, sizeof(const char*), sizeof(nv_input_action_t), nv_hash_fnv1a, nv_allocator_c, NULL, &inputctx->input_action_mapping)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_bitset_init(SDL_SCANCODE_COUNT, nv_allocator_c, &inputctx->input_kb_state)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_bitset_init(SDL_SCANCODE_COUNT, nv_allocator_c, &inputctx->input_last_frame_kb_state)) != NV_SUCCESS)
  {
    return code;
  }

  inputctx->input_mouse_position            = v2zero;
  inputctx->input_last_frame_mouse_position = v2zero;
  inputctx->ctx                             = ctx;

  return NV_SUCCESS;
}

void
nv_input_shutdown(nv_input_ctx_t* ctx)
{
  if (ctx == NULL)
  {
    return;
  }

  nv_hashmap_destroy(&ctx->input_action_mapping);
  nv_bitset_destroy(&ctx->input_kb_state);
  nv_bitset_destroy(&ctx->input_last_frame_kb_state);

  nv_bzero(ctx, sizeof(nv_input_ctx_t));
}

void
nv_input_update(nv_input_ctx_t* ctx)
{
  double accum_scroll_x = 0.0;
  double accum_scroll_y = 0.0;

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    nv_consume_event(ctx->ctx, &event);

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
    {
      accum_scroll_x += event.wheel.x;
      accum_scroll_y += event.wheel.y;
    }
  }

  ctx->scroll_x = accum_scroll_x;
  ctx->scroll_y = accum_scroll_y;

  float mx, my;
  ctx->input_last_frame_mouse_state = ctx->input_mouse_state;
  ctx->input_mouse_state            = SDL_GetMouseState(&mx, &my);

  const float width  = (float)nv_get_window_size(ctx->ctx).width;
  const float height = (float)nv_get_window_size(ctx->ctx).height;

  ctx->input_last_frame_mouse_position = ctx->input_mouse_position;
  ctx->input_mouse_position.x          = ((float)mx / width) * 2.0f - 1.0f;
  ctx->input_mouse_position.y          = ((float)my / height) * 2.0f - 1.0f;
  ctx->input_mouse_position.y *= -1.0f;

  const bool* sdl_kb_state = SDL_GetKeyboardState(NULL);
  nv_bitset_copy_from(&ctx->input_last_frame_kb_state, &ctx->input_kb_state);
  nv_bitset_copy_from_bool_array(&ctx->input_kb_state, sdl_kb_state, SDL_SCANCODE_COUNT);

  u32 const mouse_state = SDL_GetMouseState(NULL, NULL);

  size_t             _i = 0;
  nv_hashmap_node_t* node;
  while ((node = nv_hashmap_iterate_unsafe(&ctx->input_action_mapping, &_i)) != NULL)
  {
    nv_input_action_t* ia = (nv_input_action_t*)node->value;

    ia->last_frame = ia->this_frame;

    if (ia->key != 0)
    { // key2 is not checked, most ia's won't have one
      ia->this_frame = sdl_kb_state[ia->key] || sdl_kb_state[ia->key2];
      if (ia->response != NULL)
      {
        ia->response((const char*)node->key, ia);
      }
    }
    // ia->mouse is checked independently
    if (ia->mouse != 255)
    {
      // it's or'd with ia->this_frame so that we can call the response multiple times, as expected.
      ia->this_frame = ia->this_frame || (mouse_state & SDL_BUTTON_MASK(ia->mouse)) != 0;
      if (ia->response != NULL)
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
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return;
  }
  ia->response = response;
}

void
nv_input_bind_key_to_action(nv_input_ctx_t* ctx, SDL_Scancode key, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    nv_input_action_t w = { .key = key };
    nv_hashmap_insert(&ctx->input_action_mapping, action, &w);
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
  nv_hashmap_insert(&ctx->input_action_mapping, action, &ia);
}

void
nv_input_unbind_action(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
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
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return false;
  }
  return ia->this_frame && ia->last_frame;
}

bool
nv_input_is_action_just_signalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return false;
  }
  return ia->this_frame && !ia->last_frame;
}

bool
nv_input_is_action_unsignalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return false;
  }
  return !ia->this_frame && !ia->last_frame;
}

bool
nv_input_is_action_just_unsignalled(nv_input_ctx_t* ctx, const char* action)
{
  nv_input_action_t* ia = (nv_input_action_t*)nv_hashmap_find(&ctx->input_action_mapping, action);
  if (ia == NULL)
  {
    return false;
  }
  return !ia->this_frame && ia->last_frame;
}

double
nv_input_get_mouse_scroll(const nv_input_ctx_t* ctx)
{
  return ctx->scroll_y;
}

double
nv_input_get_mouse_scroll_x(const nv_input_ctx_t* ctx)
{
  return ctx->scroll_x;
}
