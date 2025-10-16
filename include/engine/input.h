#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

// implementation: engine.c

#include "../std/include/containers/hashmap.h"
#include "../std/include/errorcodes.h"
#include "../std/include/math/vec2.h"
#include <SDL3/SDL_scancode.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Only TWO keys OR ONE mouse button may be bound to an action currently!

  typedef struct nv_input_action_t nv_input_action_t;
  typedef struct nv_input_ctx_t    nv_input_ctx_t;
  struct nv_ctx;

  struct nv_ctx;

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
    nv_hashmap_t action_mapping;
    u8           kb_state[(SDL_SCANCODE_COUNT + 7) / 8];
    u8           last_frame_kb_state[(SDL_SCANCODE_COUNT + 7) / 8];

    struct nv_ctx* ctx;
    vec2           mouse_position;
    vec2           last_frame_mouse_position;
    u32            mouse_state;
    u32            input_last_frame_mouse_state;
    double         scroll_x;
    double         scroll_y;
  };

  extern nv_error nv_input_init(struct nv_ctx* ctx, nv_input_ctx_t* inputctx);
  extern void     nv_input_update(nv_input_ctx_t* ctx);
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

  extern nv_input_key_state nv_input_get_key_state(const nv_input_ctx_t* ctx, SDL_Scancode sc);

  extern bool nv_input_is_key_signalled(const nv_input_ctx_t* ctx, SDL_Scancode sc);
  extern bool nv_input_is_key_unsignalled(const nv_input_ctx_t* ctx, SDL_Scancode sc);
  extern bool nv_input_is_key_just_signalled(const nv_input_ctx_t* ctx, SDL_Scancode sc);
  extern bool nv_input_is_key_just_unsignalled(const nv_input_ctx_t* ctx, SDL_Scancode sc);

  extern vec2 nv_input_get_mouse_position(const nv_input_ctx_t* ctx);
  extern vec2 nv_input_get_last_frame_mouse_position(const nv_input_ctx_t* ctx);
  extern vec2 nv_input_get_mouse_delta(const nv_input_ctx_t* ctx);

  // button is 1 for left mouse, 2 for middle, 3 for right
  extern bool nv_input_is_mouse_signalled(const nv_input_ctx_t* ctx, nv_input_mouse_button button);
  extern bool nv_input_is_mouse_just_signalled(const nv_input_ctx_t* ctx, nv_input_mouse_button button);

  /**
   * Returns 0 for no scroll, >0 for upward, <0 for downward scrolling
   */
  extern double nv_input_get_mouse_scroll(const nv_input_ctx_t* ctx);

  /**
   * Get the mouse scroll in the horizontal direction.
   * =0 no scroll, >0 right scroll, <0 left scroll
   */
  extern double nv_input_get_mouse_scroll_x(const nv_input_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // ENGINE_INPUT_H
