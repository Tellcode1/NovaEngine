#ifndef IRIS_SHADER_RESOURCE_H
#define IRIS_SHADER_RESOURCE_H

#include "../std/include/types.h"
#include <stddef.h>

typedef u32 SpvReflectDescriptorType_;

typedef enum nvsm_resource_type
{
  NVSM_SHADER_RESOURCE_TYPE_UNIFORM_BUFFER         = 0,
  NVSM_SHADER_RESOURCE_TYPE_STORAGE_BUFFER         = 1,
  NVSM_SHADER_RESOURCE_TYPE_SAMPLED_IMAGE          = 2,
  NVSM_SHADER_RESOURCE_TYPE_COMBINED_IMAGE_SAMPLER = 3,
  NVSM_SHADER_RESOURCE_TYPE_STORAGE_IMAGE          = 4,
  NVSM_SHADER_RESOURCE_TYPE_SAMPLER                = 5,
  NVSM_SHADER_RESOURCE_TYPE_UNKNOWN                = 100
} nvsm_resource_type;

typedef struct nvsm_shader_resources_t
{
  const char*        name;
  nvsm_resource_type type;
  u32                set;
  u32                binding;
  u32                array_length; // For arrays in the shaders
  size_t             buffer_size;
} nvsm_shader_resources_t;

extern nvsm_resource_type nvsm_shader_resources_type_to_spv_reflect_type(SpvReflectDescriptorType_ type);

extern nvsm_shader_resources_t* nvsm_shader_resources_extract(const u32* spirv_data, size_t spirv_size, size_t* out_count);

#endif // IRIS_SHADER_RESOURCE_H