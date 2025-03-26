#ifndef __NOVA_GPU_COMMANDS_H__
#define __NOVA_GPU_COMMANDS_H__

#include "../external/volk/volk.h"
#include "buffer.h"
#include "descriptors.h"
#include "pipeline.h"

#define NV_COMMAND_BUFFER_MAX_BOUND_DESCRIPTOR_SETS 16
#define NV_COMMAND_BUFFER_MAX_BOUND_BUFFERS 16
#define NV_COMMAND_POOL_MAX_NUMBER_OF_COMMAND_BUFFERS 16

typedef struct nv_command_buffer_t   nv_command_buffer_t;
typedef struct nv_command_pool_t     nv_command_pool_t;
typedef struct nv_cmd_bound_buffer_t nv_cmd_bound_buffer_t;

struct nv_cmd_bound_buffer_t
{
  nv_gpu_buffer_t* buffer;
  size_t           offset;
};

struct nv_command_buffer_t
{
  bool               expired;
  VkCommandBuffer    handle;
  nv_command_pool_t* parent;

  nv_vk_pipeline_t*   bound_pipeline;
  VkPipelineBindPoint bound_pipeline_point;

  nv_descriptor_set_t* bound_descriptor_sets[NV_COMMAND_BUFFER_MAX_BOUND_DESCRIPTOR_SETS];
  size_t               nbound_descriptor_sets;

  nv_cmd_bound_buffer_t bound_vertex_buffers[NV_COMMAND_BUFFER_MAX_BOUND_BUFFERS];
  size_t                nbound_vertex_buffers;

  nv_cmd_bound_buffer_t bound_index_buffer;
};

struct nv_command_pool_t
{
  VkCommandPool        handle;
  u32                  queue_family_index;
  size_t               nbuffers;
  nv_command_buffer_t* command_buffers[NV_COMMAND_POOL_MAX_NUMBER_OF_COMMAND_BUFFERS];
};

static inline void
nv_gpu_create_command_pool(nv_command_pool_t* pool)
{
  pool->queue_family_index = graphics_family_index;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = pool->queue_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  nvvk_result_check(vkCreateCommandPool(device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &pool->handle));
}

static inline void
nv_gpu_create_command_buffers(nv_command_pool_t* pool, nv_command_buffer_t* buffers, size_t nbuffers)
{
  nv_assert_and_ret(pool != NULL, );
  nv_assert_and_ret(pool->handle != NULL, );
  nv_assert_and_ret(buffers != NULL, );
  nv_assert_and_ret(nbuffers > 0, );

  VkCommandBuffer* cmds = (VkCommandBuffer*)nv_calloc(sizeof(VkCommandBuffer*) * nbuffers);
  nv_assert(cmds != NULL);

  VkCommandBufferAllocateInfo allocate = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = pool->handle,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = (u32)(nbuffers),
  };
  vkAllocateCommandBuffers(device, &allocate, cmds);

  for (size_t i = 0; i < nbuffers; i++)
  {
    nv_command_buffer_t* buf = &buffers[i];
    pool->command_buffers[i] = buf;
    *buf                     = nv_zero_init(nv_command_buffer_t);
    buf->handle              = cmds[i];
  }
  pool->nbuffers = nbuffers;

  nv_free(cmds);
}

static inline void
nv_gpu_bind_vertex_buffers(nv_command_buffer_t* cmd, nv_gpu_buffer_t* buffers, size_t nbuffers, size_t* offsets)
{
  VkBuffer vkbuffers[NV_COMMAND_BUFFER_MAX_BOUND_BUFFERS] = { 0 };
  for (size_t i = 0; i < nbuffers; i++)
  {
    vkbuffers[i]                 = buffers[i].buffer;
    cmd->bound_vertex_buffers[i] = (nv_cmd_bound_buffer_t){
      .buffer = &buffers[i],
      .offset = offsets[i],
    };
  }
  cmd->nbound_vertex_buffers = nbuffers;
  vkCmdBindVertexBuffers(cmd->handle, 0, nbuffers, vkbuffers, offsets);
}

static inline void
nv_gpu_bind_index_buffer(nv_command_buffer_t* cmd, nv_gpu_buffer_t* buffer, size_t offset)
{
  cmd->bound_index_buffer = (nv_cmd_bound_buffer_t){
    .buffer = buffer,
    .offset = offset,
  };

  vkCmdBindIndexBuffer(cmd->handle, buffer->buffer, offset, VK_INDEX_TYPE_UINT32);
}

static inline void
nv_gpu_bind_pipeline(nv_command_buffer_t* cmd, VkPipelineBindPoint bind_point, nv_vk_pipeline_t* pipeline)
{
  cmd->bound_pipeline       = pipeline;
  cmd->bound_pipeline_point = bind_point;

  vkCmdBindPipeline(cmd->handle, bind_point, pipeline->pipeline);
}

static inline void
nv_gpu_bind_descriptor_sets(
    nv_command_buffer_t* cmd,
    uint32_t             first_set,
    nv_descriptor_set_t* descriptor_sets,
    uint32_t             descriptor_set_count,
    const uint32_t*      dynamic_offsets,
    uint32_t             dynamic_offset_count)
{
  VkDescriptorSet vkdescriptors[NV_COMMAND_BUFFER_MAX_BOUND_DESCRIPTOR_SETS];
  for (size_t i = 0; i < descriptor_set_count; i++)
  {
    vkdescriptors[i]              = descriptor_sets[i].set;
    cmd->bound_descriptor_sets[i] = &descriptor_sets[i];
    // nv_descriptor_set_flush(&descriptor_sets[i]);
  }
  cmd->nbound_descriptor_sets = descriptor_set_count;

  vkCmdBindDescriptorSets(
      cmd->handle, cmd->bound_pipeline_point, cmd->bound_pipeline->pipeline_layout, first_set, descriptor_set_count, vkdescriptors, dynamic_offset_count, dynamic_offsets);
}

static inline void
nv_gpu_command_buffer_reset(nv_command_buffer_t* cmd)
{
  vkResetCommandBuffer(cmd->handle, 0);
}

static inline void
nv_gpu_command_pool_destroy(nv_command_pool_t* pool)
{
  VkCommandBuffer* cmds = (VkCommandBuffer*)nv_calloc(sizeof(VkCommandBuffer*) * pool->nbuffers);
  nv_assert(cmds != NULL);

  for (size_t i = 0; i < pool->nbuffers; i++)
  {
    nv_command_buffer_t* buf = pool->command_buffers[i];

    cmds[i] = buf->handle;
  }

  vkFreeCommandBuffers(device, pool->handle, pool->nbuffers, cmds);
  vkDestroyCommandPool(device, pool->handle, NOVA_VK_ALLOCATOR);
}

#endif //__NOVA_GPU_COMMANDS_H__
