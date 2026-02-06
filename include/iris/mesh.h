#ifndef IRIS_MESH_H
#define IRIS_MESH_H

#include "../std/include/math/mat.h"
#include "../std/include/math/vec4.h"
#include "buffer.h"
#include "pipeline.h"
#include "sampler.h"
#include "texture.h"
#include "types.h"

typedef struct iris_mesh iris_mesh_t;

struct iris_mesh
{
  iris_buffer_t* vertex_buffer;
  iris_size_t    vertex_offset;

  iris_buffer_t* index_buffer;
  iris_size_t    index_offset;

  uint32_t vertex_count;
  uint32_t index_count;

  mat4 model; // base model matrix
  vec4 color; // optional if using solid tint

  bool use_perspective; // or pass projection flag

  // Bindings
  VkPipeline       pipeline;
  VkPipelineLayout pipeline_layout;

  VkDescriptorSet* descriptor_sets;
  uint32_t         descriptor_set_count;
  VkDeviceSize*    dynamic_offsets;
  size_t           n_dynamic_offsets;

  u8     push_constants[128];
  size_t push_constants_size;
};

extern void iris_mesh_draw(nv_renderer_t* rdr, iris_mesh_t* mesh);

#endif // IRIS_MESH_H
