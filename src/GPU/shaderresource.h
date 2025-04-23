#ifndef __NOVA_GPU_SHADER_RESOURCE_H__
#define __NOVA_GPU_SHADER_RESOURCE_H__

#include "../std/stdafx.h"

NOVA_HEADER_START

typedef u32 SpvReflectDescriptorType_;

typedef enum nv_shader_resource_type
{
  NV_SHADER_RESOURCE_TYPE_UNIFORM_BUFFER         = 0,
  NV_SHADER_RESOURCE_TYPE_STORAGE_BUFFER         = 1,
  NV_SHADER_RESOURCE_TYPE_SAMPLED_IMAGE          = 2,
  NV_SHADER_RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER = 3,
  NV_SHADER_RESOURCE_TYPE_STORAGE_IMAGE          = 4,
  NV_SHADER_RESOURCE_TYPE_SAMPLER                = 5,
  NV_SHADER_RESOURCE_TYPE_UNKNOWN                = 100
} nv_shader_resource_type;

typedef struct nv_shader_resources_t
{
  const char*             name;
  nv_shader_resource_type type;
  u32                     set;
  u32                     binding;
  u32                     array_length; // For arrays
} nv_shader_resources_t;

extern nv_shader_resource_type _nv_shader_resources_type_spv_reflect_type(SpvReflectDescriptorType_ type);

extern nv_shader_resources_t* nv_shader_resources_extract(const u32* spirv_data, size_t spirv_size, size_t* out_count);

NOVA_HEADER_END

#endif //__NOVA_GPU_SHADER_RESOURCE_H__