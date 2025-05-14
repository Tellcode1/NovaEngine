#ifndef NOVA_GPU_PASS_H_INCLUDED_
#define NOVA_GPU_PASS_H_INCLUDED_

#include "../external/volk/volk.h"
#include "../std/containers/list.h"
#include "../std/errorcodes.h"
#include "../std/stdafx.h"

NOVA_HEADER_START

typedef struct nv_gpu_pass nv_gpu_pass_t;

/**
 * If the pass returns a non zero value, the pass is invalidated and will not be submitted.
 */
typedef int (*nv_gpu_pass_record_callback_fn)(nv_gpu_pass_t* pass, void* user_data);

typedef enum nv_gpu_pass_type
{
  NV_GPU_PASS_TYPE_GRAPHICS = 0,
  NV_GPU_PASS_TYPE_COMPUTE  = 1,
  NV_GPU_PASS_TYPE_TRANSFER = 2,
} nv_gpu_pass_type;

struct nv_gpu_pass_callback
{
  nv_gpu_pass_record_callback_fn callback;
  void*                          user_data;
};

struct nv_gpu_pass
{
  nv_gpu_pass_type type;

  nv_list_t record_callbacks; // <nv_gpu_pass_callback>

  VkCommandBuffer cmd;
  VkRenderPass    render_pass; // only if pass is graphics pass
};

extern nv_error nv_gpu_pass_init(nv_gpu_pass_type type, nv_gpu_pass_t* dst);

extern void nv_gpu_pass_destroy(nv_gpu_pass_t* pass);

NOVA_HEADER_END

#endif