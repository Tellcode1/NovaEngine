#ifndef ENGINE_ENGINE_H
#define ENGINE_ENGINE_H

#include "../iris/extent.h"
#include "../std/include/containers/list.h"
#include "../std/include/math/math.h"
#include "../std/include/stdafx.h"
#include "../std/include/types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NV_FIXED_FRAME_RATE 60
#define NV_FIXED_TIME_STEP (1.0 / (double)NV_FIXED_FRAME_RATE)

  typedef void (*nv_fixed_update_fn)(double dt, void* arg);
  typedef struct nv_fixed_update_fn_stored
  {
    nv_fixed_update_fn fn;
    void*              arg;
  } nv_fixed_update_fn_stored;

  struct nv_renderer_config;
  struct SDL_Window;
  union SDL_Event;

  typedef struct nv_ctx nv_ctx_t;

  /**
   * Returns the index of the registered function.
   */
  extern size_t nv_ctx_register_fixed_update(nv_ctx_t* ctx, nv_fixed_update_fn fn, void* arg);

  /**
   * If index is not a valid function (out of bound of list), returns NV_ERROR_INVALID_ARG
   */
  extern nv_error nv_ctx_change_fixed_update_arg(nv_ctx_t* ctx, size_t index, void* arg);
  extern void     nv_ctx_remove_fixed_update(nv_ctx_t* ctx, size_t index);

  struct nv_ctx
  {
    struct SDL_Window* window;

    nv_list_t fixed_update_fns; // <nv_fixed_update_fn_stored>

    u8     current_frame;
    u64    last_frame_time; // div by SDL_GetPerofrmanceCounterFrequency to get actual time.
    double time;

    double delta_time;

    // An accumulator for delta time for fixed updates
    double accumulator;
    double fixed_time_step;

    u64 frame_start_time;
    u64 fixed_frame_start_time;
    u64 frame_time;

    bool window_framebuffer_resized;
    bool application_running;

    // This is a fix for really large values of delta time for the first frame.
    u64 sdl_time;
  };

  static inline bool
  nv_ctx_running(const nv_ctx_t* ctx)
  {
    return ctx->application_running;
  }

  static inline void
  _nv_ctx_reset_frame_buffer_resized(nv_ctx_t* ctx)
  {
    ctx->window_framebuffer_resized = false;
  }

  static inline bool
  nv_ctx_get_frame_buffer_resized(const nv_ctx_t* ctx)
  {
    return ctx->window_framebuffer_resized;
  }

  static inline u8
  nv_ctx_get_current_frame(const nv_ctx_t* ctx)
  {
    return ctx->current_frame;
  }

  static inline double
  nv_ctx_get_delta_time(const nv_ctx_t* ctx)
  {
    return ctx->delta_time;
  }

  extern double nv_get_last_frame_time(const nv_ctx_t* ctx);

  static inline double
  nv_ctx_get_time(const nv_ctx_t* ctx)
  {
    return ctx->time;
  }

  // TODO(bird): get better name
  extern void nv_window_init(const char* window_title, int window_width, int window_height, nv_ctx_t* dst);
  extern void nv_window_shutdown(nv_ctx_t* ctx);

  static inline bool
  nv_ctx_is_valid(nv_ctx_t* ctx)
  {
    return NV_UNLIKELY(!ctx || !ctx->window) == 0 && nv_list_is_valid(&ctx->fixed_update_fns);
  }

  extern void nv_consume_event(nv_ctx_t* ctx, const union SDL_Event* event);
  extern void nv_update(nv_ctx_t* ctx);

  extern nv_extent2 nv_get_window_size(nv_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // ENGINE_ENGINE_H
