#ifndef __NOVA_GPU_COMMANDS_H__
#define __NOVA_GPU_COMMANDS_H__

#include "../../external/volk/volk.h"
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
  nv_gpu_buffer_t* m_buffer;
  size_t           m_offset;
};

struct nv_command_buffer_t
{
  bool               m_expired;
  VkCommandBuffer    m_handle;
  nv_command_pool_t* m_parent;

  nv_vk_pipeline*     m_bound_pipeline;
  VkPipelineBindPoint m_bound_pipeline_point;

  nv_descriptor_set_t* m_bound_descriptor_sets[NV_COMMAND_BUFFER_MAX_BOUND_DESCRIPTOR_SETS];
  size_t               m_nbound_descriptor_sets;

  nv_cmd_bound_buffer_t m_bound_vertex_buffers[NV_COMMAND_BUFFER_MAX_BOUND_BUFFERS];
  size_t                m_nbound_vertex_buffers;

  nv_cmd_bound_buffer_t m_bound_index_buffer;
};

struct nv_command_pool_t
{
  VkCommandPool        m_handle;
  u32                  m_queue_family_index;
  size_t               m_nbuffers;
  nv_command_buffer_t* m_command_buffers[NV_COMMAND_POOL_MAX_NUMBER_OF_COMMAND_BUFFERS];
};

static inline void
nv_gpu_create_command_pool(nv_command_pool_t* pool)
{
  pool->m_queue_family_index = graphics_family_index;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = pool->m_queue_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  nvvk_result_check(vkCreateCommandPool(device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &pool->m_handle));
}

static inline void
nv_gpu_create_command_buffers(nv_command_pool_t* pool, nv_command_buffer_t* buffers, size_t nbuffers)
{
  nv_assert_and_ret(pool != NULL, );
  nv_assert_and_ret(pool->m_handle != NULL, );
  nv_assert_and_ret(buffers != NULL, );
  nv_assert_and_ret(nbuffers > 0, );

  VkCommandBuffer* cmds = (VkCommandBuffer*)nv_calloc(sizeof(VkCommandBuffer*) * nbuffers);
  nv_assert(cmds != NULL);

  VkCommandBufferAllocateInfo allocate = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = pool->m_handle,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = (u32)(nbuffers),
  };
  vkAllocateCommandBuffers(device, &allocate, cmds);

  for (size_t i = 0; i < nbuffers; i++)
  {
    nv_command_buffer_t* buf   = &buffers[i];
    pool->m_command_buffers[i] = buf;
    *buf                       = nv_zero_init(nv_command_buffer_t);
    buf->m_handle              = cmds[i];
  }
  pool->m_nbuffers = nbuffers;

  nv_free(cmds);
}

static inline void
nv_gpu_bind_vertex_buffers(nv_command_buffer_t* cmd, nv_gpu_buffer_t* buffers, size_t nbuffers, size_t* offsets)
{
  VkBuffer vkbuffers[NV_COMMAND_BUFFER_MAX_BOUND_BUFFERS] = { 0 };
  for (size_t i = 0; i < nbuffers; i++)
  {
    vkbuffers[i]                   = buffers[i].m_buffer;
    cmd->m_bound_vertex_buffers[i] = (nv_cmd_bound_buffer_t){
      .m_buffer = &buffers[i],
      .m_offset = offsets[i],
    };
  }
  cmd->m_nbound_vertex_buffers = nbuffers;
  vkCmdBindVertexBuffers(cmd->m_handle, 0, nbuffers, vkbuffers, offsets);
}

static inline void
nv_gpu_bind_index_buffer(nv_command_buffer_t* cmd, nv_gpu_buffer_t* buffer, size_t offset)
{
  cmd->m_bound_index_buffer = (nv_cmd_bound_buffer_t){
    .m_buffer = buffer,
    .m_offset = offset,
  };

  vkCmdBindIndexBuffer(cmd->m_handle, buffer->m_buffer, offset, VK_INDEX_TYPE_UINT32);
}

static inline void
nv_gpu_bind_pipeline(nv_command_buffer_t* cmd, VkPipelineBindPoint bind_point, nv_vk_pipeline* pipeline)
{
  cmd->m_bound_pipeline       = pipeline;
  cmd->m_bound_pipeline_point = bind_point;

  vkCmdBindPipeline(cmd->m_handle, bind_point, pipeline->m_pipeline);
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
    vkdescriptors[i]                = descriptor_sets[i].m_set;
    cmd->m_bound_descriptor_sets[i] = &descriptor_sets[i];
    // nv_descriptor_set_flush(&descriptor_sets[i]);
  }
  cmd->m_nbound_descriptor_sets = descriptor_set_count;

  vkCmdBindDescriptorSets(
      cmd->m_handle,
      cmd->m_bound_pipeline_point,
      cmd->m_bound_pipeline->m_pipeline_layout,
      first_set,
      descriptor_set_count,
      vkdescriptors,
      dynamic_offset_count,
      dynamic_offsets);
}

static inline void
nv_gpu_command_buffer_reset(nv_command_buffer_t* cmd)
{
  vkResetCommandBuffer(cmd->m_handle, 0);
}

static inline void
nv_gpu_command_pool_destroy(nv_command_pool_t* pool)
{
  VkCommandBuffer* cmds = (VkCommandBuffer*)nv_calloc(sizeof(VkCommandBuffer*) * pool->m_nbuffers);
  nv_assert(cmds != NULL);

  for (size_t i = 0; i < pool->m_nbuffers; i++)
  {
    nv_command_buffer_t* buf = pool->m_command_buffers[i];

    cmds[i] = buf->m_handle;
  }

  vkFreeCommandBuffers(device, pool->m_handle, pool->m_nbuffers, cmds);
  vkDestroyCommandPool(device, pool->m_handle, NOVA_VK_ALLOCATOR);
}

#endif //__NOVA_GPU_COMMANDS_H__
