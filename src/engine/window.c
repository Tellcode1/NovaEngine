#include "../../include/engine/engine.h"

#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/error.h"
#include "../../include/std/include/string.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <stddef.h>
#include <stdint.h>

void
nv_window_init(const char* window_title, int window_width, int window_height, nv_ctx_t* dst)
{
  nv_assert_else_return(dst != NULL, );
  nv_assert_else_return(window_title != NULL, );
  nv_assert_else_return(window_width != 0, );
  nv_assert_else_return(window_height != 0, );

  nv_bzero(dst, sizeof(nv_ctx_t));

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

  u64 sdl_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

  dst->window = SDL_CreateWindow(window_title, window_width, window_height, sdl_flags);
  // nv_assert_and_exec(dst->window != NULL, nv_raise_error(NV_ERROR_EXTERNAL, "%s", SDL_GetError()); return;);
  if (dst->window == NULL)
  {
    nv_raise_error(NV_ERROR_EXTERNAL, "%s", SDL_GetError());
    return;
  }

  nv_log_info("[SUCCESS] Created window (name=%s w=%i h=%i flags=%#lx)\n", window_title, window_width, window_height, sdl_flags);

  nv_error code = nv_list_init(sizeof(nv_fixed_update_fn_stored), 8, &dst->fixed_update_fns);
  if (code != NV_SUCCESS)
  {
    return;
  }

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
  dst->fixed_time_step            = NV_FIXED_TIME_STEP;
}

void
nv_window_shutdown(nv_ctx_t* ctx)
{
  nv_assert_else_return(ctx != NULL, );

  nv_list_destroy(&ctx->fixed_update_fns);

  SDL_DestroyWindow(ctx->window);
  SDL_Quit();
}

void
nv_consume_event(nv_ctx_t* ctx, const SDL_Event* event)
{
  if ((event->type == SDL_EVENT_QUIT) || (((event->type) != 0u) && (event->window.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED))
      || (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE))
  {
    ctx->application_running = false;
  }

  if ((event->type == SDL_EVENT_WINDOW_RESIZED || event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED))
  {
    ctx->window_framebuffer_resized = true;
  }
}

double
nv_get_last_frame_time(const nv_ctx_t* ctx)
{
  return (double)ctx->last_frame_time / (double)SDL_GetPerformanceFrequency();
}

void
nv_update(nv_ctx_t* ctx)
{
  uint64_t now = SDL_GetPerformanceCounter();

  ctx->delta_time = (double)(now - ctx->sdl_time) / (double)SDL_GetPerformanceFrequency();

  if (ctx->delta_time > 0.25) // clamp to avoid spiral of time doom
    ctx->delta_time = 0.25;

  ctx->sdl_time = now;

  ctx->time += ctx->delta_time;
  ctx->accumulator += ctx->delta_time;

  while (ctx->accumulator >= ctx->fixed_time_step)
  {
    for (size_t i = 0; i < nv_list_size(&ctx->fixed_update_fns); i++)
    {
      nv_fixed_update_fn_stored* fn = nv_list_get(&ctx->fixed_update_fns, i);
      if (fn && fn->fn)
        fn->fn(ctx->fixed_time_step, fn->arg);
    }
    ctx->accumulator -= ctx->fixed_time_step;
  }
}

size_t
nv_ctx_register_fixed_update(nv_ctx_t* ctx, nv_fixed_update_fn fn, void* arg)
{
  nv_fixed_update_fn_stored store = { .fn = fn, .arg = arg };
  if (fn != NULL)
  {
    nv_list_push_back(&ctx->fixed_update_fns, &store);
    return nv_list_size(&ctx->fixed_update_fns) - 1;
  }
  return (size_t)-1;
}

nv_error
nv_ctx_change_fixed_update_arg(nv_ctx_t* ctx, size_t index, void* arg)
{
  nv_fixed_update_fn_stored* store = nv_list_get(&ctx->fixed_update_fns, index);
  if (store != NULL)
  {
    store->arg = arg;
  }
  else
  {
    return NV_ERROR_INVALID_ARG;
  }
  return NV_SUCCESS;
}

void
nv_ctx_remove_fixed_update(nv_ctx_t* ctx, size_t index)
{
  nv_list_remove(&ctx->fixed_update_fns, index);
}
