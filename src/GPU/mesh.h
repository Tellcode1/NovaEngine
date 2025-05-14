#ifndef __NOVA_MESH_H__
#define __NOVA_MESH_H__

#include "../std/math/vec2.h"
#include "../std/math/vec3.h"
#include "buffer.h"

typedef struct nv_gpu_vertex
{
  vec3f position;
  vec2f tex_coords;
} nv_gpu_vertex_t;

typedef struct nv_gpu_mesh_s
{
  nv_gpu_buffer_t vertex_buffer;
  nv_gpu_buffer_t index_buffer;
  bool            using_index_buffer;
} nv_gpu_mesh_t;

static inline void
nv_gpu_mesh_set_vertices(nv_gpu_mesh_t* mesh, size_t num_vertices, nv_gpu_vertex_t* vertices)
{
}

#endif //__NOVA_MESH_H__