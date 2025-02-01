#ifndef __NOVA_ENGINE_H__
#define __NOVA_ENGINE_H__

#include "../../common/stdafx.h"

NOVA_HEADER_START;

struct nv_renderer_config;
union SDL_Event;

typedef struct NVTime {
  f64 time;
  u64 last_frame_time;

} NVTime;

extern u8 nv_current_frame;
extern u64 nv_last_frame_time; // div by SDL_GetPerofrmanceCounterFrequency to get actual time.
extern double nv_time;

extern double nv_delta_time;

extern u64 nv_frame_start_time;
extern u64 nv_fixed_frame_start_time;
extern u64 nv_frame_time;

extern bool nv_window_framebuffer_resized;
extern bool nv_application_running;

static inline bool nv_running() {
  return nv_application_running;
}

static inline void _nv_reset_frame_buffer_resized() {
  nv_window_framebuffer_resized = false;
}

static inline bool nv_get_frame_buffer_resized() {
  return nv_window_framebuffer_resized;
}

static inline u8 nv_get_current_frame() {
  return nv_current_frame;
}

static inline double nv_get_delta_time() {
  return nv_delta_time;
}

static inline double nv_get_last_frame_time() {
  return nv_last_frame_time;
}

static inline double nv_get_time() {
  return nv_time;
}

extern void nv_initialize_context(const char *window_title, int window_width, int window_height);
extern void _nvvk_initialize_context(const char *window_title, u32 window_width, u32 window_height);

static const u32 NV_FIXED_FRAME_RATE   = 60;
static const double NV_FIXED_TICK_RATE = 1000.0 / (double)NV_FIXED_FRAME_RATE; // 1000 milliseconds

extern void nv_consume_event(const union SDL_Event *event);
extern void nv_update();

NOVA_HEADER_END;

#endif // __C_ENGINE_H__