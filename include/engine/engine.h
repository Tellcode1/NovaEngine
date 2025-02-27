#ifndef __NOVA_ENGINE_H__
#define __NOVA_ENGINE_H__

#include "../../std/stdafx.h"

NOVA_HEADER_START

struct nv_renderer_config;
union SDL_Event;

typedef struct NVTime
{
  f64 m_time;
  u64 m_last_frame_time;

} NVTime;

extern u8     nv_current_frame;
extern u64    nv_last_frame_time; // div by SDL_GetPerofrmanceCounterFrequency to get actual time.
extern real_t nv_time;

extern real_t nv_delta_time;

extern u64 nv_frame_start_time;
extern u64 nv_fixed_frame_start_time;
extern u64 nv_frame_time;

extern bool nv_window_framebuffer_resized;
extern bool nv_application_running;

static inline bool
nv_running(void)
{
  return nv_application_running;
}

static inline void
_nv_reset_frame_buffer_resized(void)
{
  nv_window_framebuffer_resized = false;
}

static inline bool
nv_get_frame_buffer_resized(void)
{
  return nv_window_framebuffer_resized;
}

static inline u8
nv_get_current_frame(void)
{
  return nv_current_frame;
}

static inline real_t
nv_get_delta_time(void)
{
  return nv_delta_time;
}

extern real_t
nv_get_last_frame_time(void);

static inline real_t
nv_get_time(void)
{
  return nv_time;
}

extern void nv_initialize_context(const char* window_title, int window_width, int window_height);
extern void _nvvk_initialize_context(const char* window_title);

static const u32    NV_FIXED_FRAME_RATE = 60;
static const real_t NV_FIXED_TICK_RATE  = 1000.0 / (real_t)NV_FIXED_FRAME_RATE; // 1000 milliseconds

extern void nv_consume_event(const union SDL_Event* event);
extern void nv_update(void);

NOVA_HEADER_END

#endif // __C_ENGINE_H__
