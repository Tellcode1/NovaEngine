#ifndef __NOVA_MESH_H__
#define __NOVA_MESH_H__

#include "../../std/math/mat.h"
#include "../../std/math/vec2.h"
#include "../../std/math/vec3.h"
#include "../../std/stdafx.h"
#include "../GPU/buffer.h"
#include "../GPU/memory.h"
#include "../GPU/pipeline.h"
#include "../GPU/vkstdafx.h"
#include "../engine/camera.h"
#include "../engine/renderer.h"
#include "../engine/sprite.h"

NOVA_HEADER_START;

typedef struct nv_mesh_t   nv_mesh_t;
typedef struct nv_vertex_t nv_vertex_t;

struct nv_mesh_t
{
  nv_gpu_buffer_t  buf;
  nv_gpu_memory_t* mem;
  size_t           nvertices, nindices;
  size_t           indices_offset;
};

struct nv_vertex_t
{
  vec3 position;
  vec3 normal;
  vec2 tex_coord;
};

static const nv_vertex_t cube_vertices[] = {
  { { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },  { { 1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
  { { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },    { { -1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },

  { { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } }, { { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
  { { -1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } }, { { 1.0f, 1.0f, -1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },
};

static const uint32_t cube_indices[] = { 0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4, 5, 0, 3, 3, 6, 5, 1, 4, 7, 7, 2, 1, 3, 2, 7, 7, 6, 3, 5, 4, 1, 1, 0, 5 };

static inline void
nv_mesh_init(const nv_vertex_t* vertices, size_t nvertices, const u32* indices, size_t nindices, nv_mesh_t* dst)
{
  nv_gpu_create_buffer(sizeof(nv_vertex_t) * nvertices + sizeof(u32) * nindices, 1, NOVA_GPU_BUFFER_TYPE_VERTEX_BUFFER | NOVA_GPU_BUFFER_TYPE_INDEX_BUFFER, &dst->buf);
  nv_gpu_allocate_memory(
      sizeof(nv_vertex_t) * nvertices + sizeof(u32) * nindices,
      NOVA_GPU_MEMORY_USAGE_GPU_LOCAL | NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE,
      &dst->mem);
  nv_gpu_bind_buffer_to_memory(dst->mem, 0, &dst->buf);

  nv_gpu_map_buffer(&dst->buf);
  nv_gpu_write_to_buffer(&dst->buf, sizeof(nv_vertex_t) * nvertices, vertices, 0);
  nv_gpu_write_to_buffer(&dst->buf, sizeof(u32) * nindices, indices, sizeof(nv_vertex_t) * nvertices);
  nv_gpu_unmap_buffer(&dst->buf);

  dst->nvertices      = nvertices;
  dst->nindices       = nindices;
  dst->indices_offset = nvertices * sizeof(nv_vertex_t);
}

static inline void
nv_mesh_render(nv_renderer_t* rd, nv_camera_t* cam, nv_sprite* spr, nv_mesh_t* mesh)
{
  VkCommandBuffer cmd = nv_renderer_get_draw_buffer(rd);

  VkDeviceSize offsets[1] = { 0 };
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Lit.pipeline);

  struct lit_push_constants
  {
    mat4f model;
    vec4f color;
    vec3f lightPosition;
    float padding;
    vec3f viewPosition;
    float padding2;
    vec2f tex_multiplier;
    vec2f padding3;
  } pc;

  const VkDescriptorSet camera_set = camera.sets->set;

  mat4f scale       = m4finit(1.0f);
  mat4f rotate      = m4finit(1.0f);
  mat4f translate   = m4finit(1.0f);
  pc.model          = m4fmul(translate, m4fmul(rotate, scale));
  pc.color          = (vec4f){ 1.0f, 1.0f, 1.0f, 1.0f };
  pc.tex_multiplier = (vec2f){ 1.0f, 1.0f };
  pc.lightPosition  = (vec3f){ camera.position.x, camera.position.y, camera.position.z };
  pc.viewPosition   = (vec3f){ camera.position.x, camera.position.y, camera.position.z };
  vkCmdPushConstants(cmd, g_Pipelines.Lit.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct lit_push_constants), &pc);

  VkDescriptorSet       sprite_set = nv_sprite_get_descriptor_set(spr);
  const VkDescriptorSet sets[]     = { camera_set, sprite_set };

  const uint32_t camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Lit.pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
  vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->buf.buffer, offsets);
  vkCmdBindIndexBuffer(cmd, mesh->buf.buffer, mesh->indices_offset, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, mesh->nindices, 1, 0, 0, 0);
  // vkCmdDraw(cmd, mesh->nvertices, 1, 0, 0);
}

static inline void
nv_mesh_destroy(nv_mesh_t* mesh)
{
  nv_gpu_destroy_buffer(&mesh->buf);
  nv_gpu_free_memory(mesh->mem);
}

NOVA_HEADER_END;

#endif //__NOVA_MESH_H__