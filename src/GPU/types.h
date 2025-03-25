#ifndef __NOVA_GPU_TYPES_H__
#define __NOVA_GPU_TYPES_H__

#include "../std/stdafx.h"

NOVA_HEADER_START

/* Why the fuck are the three enums in GPU/????? */
typedef enum nv_window_flag_bits
{
  NOVA_WINDOW_VSYNC         = 1 << 0,
  NOVA_WINDOW_RESIZABLE     = 1 << 1,
  NOVA_WINDOW_BORDERLESS    = 1 << 2,
  NOVA_WINDOW_FULLSCREEN    = 1 << 3,
  NOVA_WINDOW_MAXIMIZED     = 1 << 4,
  NOVA_WINDOW_MINIMIZED     = 1 << 5,
  NOVA_WINDOW_HIDDEN        = 1 << 6,
  NOVA_WINDOW_HIGH_DPI      = 1 << 7,
  NOVA_WINDOW_ALWAYS_ON_TOP = 1 << 8,
} nv_window_flag_bits;

typedef enum nv_window_v_sync_bits
{
  NOVA_WINDOW_VSYNC_DISABLED = 0,
  NOVA_WINDOW_VSYNC_ENABLED  = 1
} nv_window_v_sync_bits;
typedef bool nv_window_vsync;

typedef enum nv_buffer_mode_bits
{
  NOVA_BUFFER_MODE_SINGLE_BUFFERED = 0,
  NOVA_BUFFER_MODE_DOUBLE_BUFFERED = 1,
  NOVA_BUFFER_MODE_TRIPLE_BUFFERED = 2,
} nv_buffer_mode_bits;
typedef unsigned nv_buffer_mode;

typedef enum nv_sample_count_bits
{
  NOVA_SAMPLE_COUNT_MAX_SUPPORTED    = 0x7FFFFFFF,
  NOVA_SAMPLE_COUNT_NO_EXTRA_SAMPLES = 1,
  NOVA_SAMPLE_COUNT_1_SAMPLES        = 1,
  NOVA_SAMPLE_COUNT_2_SAMPLES        = 2,
  NOVA_SAMPLE_COUNT_4_SAMPLES        = 4,
  NOVA_SAMPLE_COUNT_8_SAMPLES        = 8,
  NOVA_SAMPLE_COUNT_16_SAMPLES       = 16,
  NOVA_SAMPLE_COUNT_32_SAMPLES       = 32,
} nv_sample_count_bits;
typedef unsigned nv_sample_count;

typedef struct nv_extent2d
{
  size_t width, height;
} nv_extent2d;

typedef struct nv_extent3D
{
  size_t width, height, depth;
} nv_extent3D;

NOVA_HEADER_END

#endif //__NOVA_GPU_TYPES_H__
