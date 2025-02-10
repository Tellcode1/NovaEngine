#ifndef __NOVA_PIPELINE_H__
#define __NOVA_PIPELINE_H__

// implementation: vk.c

#include "../../external/volk/volk.h"

#include "../../std/print.h"
#include "../../std/stdafx.h"
#include "../engine/renderer.h"
#include "vk.h"
#include "vkstdafx.h"

NOVA_HEADER_START;

#define NOVA_VK_MAX_SHADERS_PER_PIPELINE 8

typedef struct nvsm_shader_t nvsm_shader_t;

typedef void (*nv_gpu_result_check_fn)(const VkResult result, const char* __restrict__ FILE, const char* __restrict__ FUNC, unsigned long LINE);

#define NVVK_REQUIRED_PTR(ptr)                                                                                                                                                \
  if ((ptr) == NULL) nv_log_and_abort(#ptr " :  Required parameter \"" #ptr "\" specified as NULL.", nv_basename(__FILE__), __LINE__, __PRETTY_FUNCTION__)
#define NVVK_NOT_EQUAL_TO(val, to)                                                                                                                                            \
  if ((val) == (to)) nv_log_and_abort(#val " == " #to ". Value \"" #val "\" must not be equal to " #to ".", nv_basename(__FILE__), __LINE__, __PRETTY_FUNCTION__)

#define _NVVK_TO_BIT(n) (1 << n)

typedef enum nvvk_pipeline_flags_bits
{
  NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK        = _NVVK_TO_BIT(0),
  NVVK_PIPELINE_FLAGS_UNFORCE_DEPTH_CHECK      = ~NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK,
  NVVK_PIPELINE_FLAGS_FORCE_CULLING            = _NVVK_TO_BIT(1),
  NVVK_PIPELINE_FLAGS_UNFORCE_CULLING          = ~NVVK_PIPELINE_FLAGS_FORCE_CULLING, // Disables culling for resulting pipeline
  NVVK_PIPELINE_FLAGS_FORCE_DYNAMIC_VIEWPORT   = _NVVK_TO_BIT(2),
  NVVK_PIPELINE_FLAGS_UNFORCE_DYNAMIC_VIEWPORT = ~NVVK_PIPELINE_FLAGS_FORCE_DYNAMIC_VIEWPORT,
  NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING      = _NVVK_TO_BIT(3),
  NVVK_PIPELINE_FLAGS_UNFORCE_MULTISAMPLING    = ~NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING
} nvvk_pipeline_flags_bits;
typedef u32 nvvk_pipeline_flags;

#define nvvk_result_check(func) _nvvk_result_fn(func, nv_basename(__FILE__), #func, __LINE__)

static void
_nvvk_default_result_check_fn(const VkResult result, const char* FILE, const char* FUNC, unsigned long LINE)
{
  if (result == VK_SUCCESS) return;

  struct tm* time = _nv_get_time();
  // Non fatal error codes are positive
  // So we just log OK error codes as warnings instead of errors
  if (result < 0)
    nv_printf("[%d:%d:%d] vkerr: %s returned %s", time->tm_hour, time->tm_min, time->tm_sec, FUNC, nvvk_vk_result_to_string(result));
  else
    nv_printf("[%d:%d:%d] vkwarn: %s returned %s", time->tm_hour, time->tm_min, time->tm_sec, FUNC, nvvk_vk_result_to_string(result));
}

/*
  Used internally. Do NOT modify by yourselves! Use SetResultCheckFunc instead.
*/
extern nv_gpu_result_check_fn _nvvk_result_fn;

/*
  Set the result checking function for the API. This is called every time the program requests something in the order of vkCreate* that this namespace
  has a hold of. Use NULL to deattach the function.
*/
static inline void
nv_GPU_SetResultCheckFn(nv_gpu_result_check_fn func)
{
  if (func != NULL)
    _nvvk_result_fn = func;
  else
    func = _nvvk_default_result_check_fn;
}

extern u32 nv_GPU_vk_flag_register;

/*
 *	FORWARD DECLARATIONS
 */
typedef struct nv_vk_pipeline                 nv_vk_pipeline;
typedef struct nv_baked_pipelines             nv_baked_pipelines;
typedef struct nv_gpu_pipeline_create_info    nv_gpu_pipeline_create_info;
typedef struct nv_gpu_swapchain_create_info   nv_gpu_swapchain_create_info;
typedef struct nv_gpu_render_pass_create_info nv_gpu_render_pass_create_info;
typedef struct nv_gpu_pipeline_blend_state    nv_gpu_pipeline_blend_state;

struct nv_vk_pipeline
{
  VkPipeline       pipeline;
  VkPipelineLayout pipeline_layout;

  // The common descriptor set layout.
  VkDescriptorSetLayout descriptor_layout;
};

struct nv_baked_pipelines
{
  nv_vk_pipeline Unlit;
  nv_vk_pipeline Lit;
  nv_vk_pipeline Ctext;
  nv_vk_pipeline Line; // Draws lines. Yep.
};
extern nv_baked_pipelines g_Pipelines;

typedef enum nv_gpu_pipeline_blend_preset
{
  NVVK_BLEND_PRESET_NONE                = 0,
  NVVK_BLEND_PRESET_ALPHA               = 1,
  NVVK_BLEND_PRESET_ADDITIVE            = 2,
  NVVK_BLEND_PRESET_MULTIPLICATIVE      = 3,
  NVVK_BLEND_PRESET_PREMULTIPLIED_ALPHA = 4,
  NVVK_BLEND_PRESET_SUBTRACTIVE         = 5,
  NVVK_BLEND_PRESET_SCREEN              = 6,
} nv_gpu_pipeline_blend_preset;

struct nv_gpu_pipeline_blend_state
{
  VkBlendFactor         srcColorBlendFactor;
  VkBlendFactor         dstColorBlendFactor;
  VkBlendOp             colorBlendOp;
  VkBlendFactor         srcAlphaBlendFactor;
  VkBlendFactor         dstAlphaBlendFactor;
  VkBlendOp             alphaBlendOp;
  VkColorComponentFlags colorWriteMask;
};

struct nv_gpu_pipeline_create_info
{
  VkRenderPass        render_pass;
  VkPipelineLayout    pipeline_layout;
  VkExtent2D          extent;
  nv_format           format;
  u64                 subpass;
  VkPipeline          old_pipeline;
  VkPipelineCache     cache;
  VkPrimitiveTopology topology;

  /* EXTENSIONS */

  // Ignored if flags does not contain PIPELINE_CREATE_FLAGS_ENABLE_MULTISAMPLING
  VkSampleCountFlagBits samples;

  // Ignored if flags does not contain PIPELINE_CREATE_FLAGS_ENABLE_BLEND
  const nv_gpu_pipeline_blend_state* blend_state;

  //	Array pointers are allowed to be NULL
  const VkVertexInputAttributeDescription* pAttributeDescriptions;
  const VkVertexInputBindingDescription*   pBindingDescriptions;
  const VkDescriptorSetLayout*             pDescriptorLayouts;
  const VkPushConstantRange*               pPushConstants;
  const struct nvsm_shader_t* const*       pShaders;

  int nAttributeDescriptions;
  int nBindingDescriptions;
  int nDescriptorLayouts;
  int nPushConstants;
  int nShaders;
};

struct nv_gpu_swapchain_create_info
{
  VkExtent2D       extent;
  VkPresentModeKHR present_mode;
  u64              image_count;
  nv_format        format;
  VkColorSpaceKHR  color_space;
  VkSwapchainKHR   old_swapchain;
};

struct nv_gpu_render_pass_create_info
{
  u64       subpass;
  nv_format format;
  nv_format depthBufferFormat;

  // Ignored if flags does not contain PIPELINE_CREATE_FLAGS_ENABLE_MULTISAMPLING
  VkSampleCountFlagBits samples;
};

extern nv_gpu_swapchain_create_info nv_gpu_init_swapchain_create_info();
extern nv_gpu_pipeline_blend_state  nv_gpu_init_pipeline_blend_state(nv_gpu_pipeline_blend_preset preset);
#define nv_gpu_init_pipeline_create_info()                                                                                                                                    \
  (nv_gpu_pipeline_create_info) { .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .samples = VK_SAMPLE_COUNT_1_BIT }
#define nv_gpu_init_render_pass_create_info()                                                                                                                                 \
  (nv_gpu_render_pass_create_info) { .samples = VK_SAMPLE_COUNT_1_BIT }

extern void nv_vk_bake_global_pipelines(nv_renderer_t* rd);
extern void nv_vk_destroy_global_pipelines();

extern void nv_gpu_create_graphics_pipeline(nv_gpu_pipeline_create_info const* pCreateInfo, VkPipeline* dstPipeline, u32 flags);
extern void nv_gpu_create_depth_pipeline(nv_gpu_pipeline_create_info const* pCreateInfo, VkPipeline* dstPipeline, u32 flags);
extern void nv_gpu_create_pipeline_layout(nv_gpu_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout);
extern void nv_gpu_create_render_pass(nv_gpu_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags);
extern void nv_gpu_create_depth_pass(nv_gpu_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags);
extern void nv_gpu_create_swapchain(nv_gpu_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain);

NOVA_HEADER_END;

#endif //__NOVA_PIPELINE_H__