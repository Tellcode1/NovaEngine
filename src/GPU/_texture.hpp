#include "../std/stdafx.h"

typedef enum nv_gpu_texture_type
{
  NV_GPU_TEXTURE_TYPE_2D      = 0,
  NV_GPU_TEXTURE_TYPE_3D      = 1,
  NV_GPU_TEXTURE_TYPE_CUBEMAP = 2,
} nv_gpu_texture_type;

typedef struct nv_gpu_texture_create_info nv_gpu_texture_create_info_t;

typedef u32 nv_gpu_texture_usage;
typedef enum nv_gpu_texture_usage_bits
{
  NV_GPU_TEXTURE_USAGE_SAMPLED_BIT          = 1 << 0,
  NV_GPU_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT = 1 << 1,
  NV_GPU_TEXTURE_USAGE_DEPTH_STENCIL_BIT    = 1 << 2,
  NV_GPU_TEXTURE_USAGE_DYNAMIC_BIT          = 1 << 5 // allows CPU updates (mapped memory)
} nv_gpu_texture_usage_bits;

struct nv_gpu_texture_create_info
{
  size_t width;
  size_t height;

  nv_gpu_texture_usage flags;

  nv_gpu_texture_type  type;
  nv_gpu_texture_usage format;
};