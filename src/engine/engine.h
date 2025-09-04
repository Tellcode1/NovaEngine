#ifndef NOVAENGINE_SRC_ENGINE_ENGINE_H
#define NOVAENGINE_SRC_ENGINE_ENGINE_H

#include "../std/stdafx.h"
#include "../std/types.h"

NOVA_HEADER_START

struct nv_renderer_config;
struct SDL_Window;
union SDL_Event;

typedef struct nv_ctx nv_ctx_t;

struct nv_ctx
{
  struct SDL_Window* window;

  u8     current_frame;
  u64    last_frame_time; // div by SDL_GetPerofrmanceCounterFrequency to get actual time.
  real_t time;

  real_t delta_time;

  u64 frame_start_time;
  u64 fixed_frame_start_time;
  u64 frame_time;

  bool window_framebuffer_resized;
  bool application_running;

  // This is a fix for really large values of delta time for the first frame.
  u64 sdl_time;
};

static inline bool
nv_running(const nv_ctx_t* ctx)
{
  return ctx->application_running;
}

static inline void
_nv_reset_frame_buffer_resized(nv_ctx_t* ctx)
{
  ctx->window_framebuffer_resized = false;
}

static inline bool
nv_get_frame_buffer_resized(const nv_ctx_t* ctx)
{
  return ctx->window_framebuffer_resized;
}

static inline u8
nv_get_current_frame(const nv_ctx_t* ctx)
{
  return ctx->current_frame;
}

static inline real_t
nv_get_delta_time(const nv_ctx_t* ctx)
{
  return ctx->delta_time;
}

extern real_t nv_get_last_frame_time(const nv_ctx_t* ctx);

static inline real_t
nv_get_time(const nv_ctx_t* ctx)
{
  return ctx->time;
}

// TODO(bird): get better name
extern void nv_window_init(const char* window_title, int window_width, int window_height, nv_ctx_t* dst);
extern void nv_window_shutdown(nv_ctx_t* ctx);

static inline bool
nv_ctx_is_valid(nv_ctx_t* ctx)
{
  return NV_UNLIKELY(!ctx || !ctx->window) == 0;
}

static const u32    NV_FIXED_FRAME_RATE = 60;
static const real_t NV_FIXED_TICK_RATE  = 1000.0 / (real_t)NV_FIXED_FRAME_RATE; // 1000 milliseconds

extern void nv_consume_event(nv_ctx_t* ctx, const union SDL_Event* event);
extern void nv_update(nv_ctx_t* ctx);

NOVA_HEADER_END

#endif // NOVAENGINE_SRC_ENGINE_ENGINE_H
