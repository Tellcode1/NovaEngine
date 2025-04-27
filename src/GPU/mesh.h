#ifndef __NOVA_MESH_H__
#define __NOVA_MESH_H__

#include "../external/cgltf/cgltf.h"
#include "buffer.h"

typedef struct nv_gpu_mesh_s
{
  cgltf_data*     mesh_data;
  nv_gpu_buffer_t vertex_data;
} nv_gpu_mesh_t;

#endif //__NOVA_MESH_H__