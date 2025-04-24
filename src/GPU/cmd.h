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

#endif //__NOVA_GPU_COMMANDS_H__
