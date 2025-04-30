#ifndef __NOVA_INPUT_H__
#define __NOVA_INPUT_H__

// implementation: engine.c

#include "../std/containers/bitset.h"
#include "../std/containers/hashmap.h"
#include "../std/math/vec2.h"
#include <SDL2/SDL.h>

NOVA_HEADER_START

// Only TWO keys OR ONE mouse button may be bound to an action currently!

typedef struct nv_input_action_t nv_input_action_t;
typedef struct nv_input_ctx_t    nv_input_ctx_t;

struct nv_ctx_s;

// A function that is called every time an action is signalled
// This is better than polling the event every frame when the action is signalled multiple times per frame
typedef void (*nv_input_action_response_fn)(const char* action, const nv_input_action_t* ia);

typedef enum nv_input_action_type
{
  NOVA_INPUT_ACTION_TYPE_KEYBOARD = 1,
  NOVA_INPUT_ACTION_TYPE_MOUSE    = 2,
  NOVA_INPUT_ACTION_TYPE_ALL      = (1 | 2)
} nv_input_action_type;

typedef enum nv_input_key_state
{
  NOVA_KEY_STATE_PRESSED  = 0,
  NOVA_KEY_STATE_RELEASED = 1,
  NOVA_KEY_STATE_HELD     = 2,
  NOVA_KEY_STATE_NOT_HELD = 3,
} nv_input_key_state;

typedef enum nv_input_mouse_button
{
  NOVA_MOUSE_BUTTON_LEFT   = 0,
  NOVA_MOUSE_BUTTON_MIDDLE = 1,
  NOVA_MOUSE_BUTTON_RIGHT  = 2,
} nv_input_mouse_button;

struct nv_input_action_t
{
  SDL_Scancode                key;
  SDL_Scancode                key2;
  uint8_t                     mouse;
  nv_input_action_response_fn response;
  bool                        this_frame, last_frame;
};

struct nv_input_ctx_t
{
  vec2        input_mouse_position;
  vec2        input_last_frame_mouse_position;
  nv_bitset_t input_kb_state;
  nv_bitset_t input_last_frame_kb_state;
  unsigned    input_mouse_state;
  unsigned    input_last_frame_mouse_state;

  nv_hashmap_t input_action_mapping;
};

extern nv_error nv_input_init(nv_input_ctx_t* ctx);
extern void     nv_input_update(nv_input_ctx_t* ctx, struct nv_ctx_s* globalctx);
extern void     nv_input_shutdown(nv_input_ctx_t* ctx);

void nv_input_bind_function_to_action(nv_input_ctx_t* ctx, const char* action, nv_input_action_response_fn response);

extern void nv_input_bind_key_to_action(nv_input_ctx_t* ctx, SDL_Scancode key, const char* action);
extern void nv_input_bind_mouse_to_action(nv_input_ctx_t* ctx, int bton, const char* action);

// remove all keys, mouse buttons and the response function from action
// essentially, clear it.
extern void nv_input_unbind_action(nv_input_ctx_t* ctx, const char* action);

// action held
extern bool nv_input_is_action_signalled(nv_input_ctx_t* ctx, const char* action);

// action pressed
extern bool nv_input_is_action_just_signalled(nv_input_ctx_t* ctx, const char* action);

// action not held
extern bool nv_input_is_action_unsignalled(nv_input_ctx_t* ctx, const char* action);

// action released
extern bool nv_input_is_action_just_unsignalled(nv_input_ctx_t* ctx, const char* action);

/// @brief signals the action for a frame
/// @return 0 on action signalled, -1 if it can't find the action specified.
extern int nv_input_signal_action(nv_input_ctx_t* ctx, const char* action);

extern nv_input_key_state nv_input_get_key_state(nv_input_ctx_t* ctx, const SDL_Scancode sc);

extern bool nv_input_is_key_signalled(nv_input_ctx_t* ctx, const SDL_Scancode sc);
extern bool nv_input_is_key_unsignalled(nv_input_ctx_t* ctx, const SDL_Scancode sc);
extern bool nv_input_is_key_just_signalled(nv_input_ctx_t* ctx, const SDL_Scancode sc);
extern bool nv_input_is_key_just_unsignalled(nv_input_ctx_t* ctx, const SDL_Scancode sc);

extern vec2 nv_input_get_mouse_position(nv_input_ctx_t* ctx);
extern vec2 nv_input_get_last_frame_mouse_position(nv_input_ctx_t* ctx);
extern vec2 nv_input_get_mouse_delta(nv_input_ctx_t* ctx);

// button is 1 for left mouse, 2 for middle, 3 for right
extern bool nv_input_is_mouse_signalled(nv_input_ctx_t* ctx, nv_input_mouse_button button);
extern bool nv_input_is_mouse_just_signalled(nv_input_ctx_t* ctx, nv_input_mouse_button button);

NOVA_HEADER_END

#endif //__NOVA_INPUT_H__
