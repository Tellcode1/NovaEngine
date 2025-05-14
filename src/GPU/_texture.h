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
  NV_GPU_TEXTURE_USAGE_TRANSFER_SRC_BIT     = 1 << 3,
  NV_GPU_TEXTURE_USAGE_TRANSFER_DST_BIT     = 1 << 4,
  NV_GPU_TEXTURE_USAGE_DYNAMIC_BIT          = 1 << 5 // allows CPU updates (mapped memory)
} nv_gpu_texture_usage_bits;

typedef enum nv_gpu_texture_hint nv_gpu_texture_hint;
typedef enum nv_gpu_texture_hint_bits
{
  NV_GPU_TEXTURE_HINT_
} nv_gpu_texture_hint_bits;

struct nv_gpu_texture_create_info
{
  size_t width;
  size_t height;
  size_t depth; // for 3D textures; 1 for 2D/cube

  nv_gpu_texture_usage flags;

  nv_gpu_texture_type  type;
  nv_gpu_texture_usage format;
  size_t               mip_levels; // number of mip levels (>=1)

  bool   to_generate_mips; // if true, generate when possible
  size_t array_layers;     // see array layers in VkImageCreateInfo
};

typedef struct nv_gpu_texture_region
{
  size_t x, y, z;
  size_t width, height, depth;
} nv_gpu_texture_region_t;