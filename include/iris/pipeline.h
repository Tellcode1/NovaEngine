
#ifndef NOVA_PIPELINE_H
#define NOVA_PIPELINE_H

#include "../../external/volk/volk.h"
#include "../engine/format.h"
#include "../engine/renderer.h"
#include "../shadersystem/nvsm.h"
#include "../std/include/errorcodes.h"
#include "../std/include/print.h"
#include "../std/include/stdafx.h"
#include "../std/include/types.h"
#include "types.h"
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define NVVK_REQUIRED_PTR(ptr)                                                                                                                                                \
  if ((ptr) == (VK_NULL_HANDLE))                                                                                                                                              \
  {                                                                                                                                                                           \
    nv_log_and_abort(#ptr " :  Required parameter \"" #ptr "\" specified as NULL.")                                                                                           \
  }
#define NVVK_NOT_EQUAL_TO(val, to)                                                                                                                                            \
  if ((val) == (to))                                                                                                                                                          \
  nv_log_and_abort(#val " == " #to ". Value \"" #val "\" must not be equal to " #to ".")

#define NVVK_TO_BIT_(n) (1 << (n))

  typedef u32 nvvk_pipeline_flags;
  typedef enum nvvk_pipeline_flags_bits
  {
    NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK        = NVVK_TO_BIT_(0),
    NVVK_PIPELINE_FLAGS_UNFORCE_DEPTH_CHECK      = ~NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK,
    NVVK_PIPELINE_FLAGS_FORCE_CULLING            = NVVK_TO_BIT_(1),
    NVVK_PIPELINE_FLAGS_UNFORCE_CULLING          = ~NVVK_PIPELINE_FLAGS_FORCE_CULLING, // Disables culling for resulting pipeline
    NVVK_PIPELINE_FLAGS_FORCE_DYNAMIC_VIEWPORT   = NVVK_TO_BIT_(2),
    NVVK_PIPELINE_FLAGS_UNFORCE_DYNAMIC_VIEWPORT = ~NVVK_PIPELINE_FLAGS_FORCE_DYNAMIC_VIEWPORT,
    NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING      = NVVK_TO_BIT_(3),
    NVVK_PIPELINE_FLAGS_UNFORCE_MULTISAMPLING    = ~NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING
  } nvvk_pipeline_flags_bits;

#define nvvk_result_check(ctx, func) (ctx).result_fn((func), nv_basename(__FILE__), #func, __LINE__)

  extern u32 nv_GPU_vk_flag_register;

  /*
   *	FORWARD DECLARATIONS
   */
  typedef struct nv_vk_pipeline_t             nv_vk_pipeline_t;
  typedef struct nv_baked_pipelines           nv_baked_pipelines;
  typedef struct iris_pipeline_create_info    iris_pipeline_create_info;
  typedef struct iris_swapchain_create_info   iris_swapchain_create_info;
  typedef struct iris_render_pass_create_info iris_render_pass_create_info;
  typedef struct iris_pipeline_blend_state    iris_pipeline_blend_state;

  struct nv_vk_pipeline_t
  {
    VkPipeline       pipeline;
    VkPipelineLayout pipeline_layout;

    // The common descriptor set layout.
    VkDescriptorSetLayout descriptor_layout;
  };

  struct nv_baked_pipelines
  {
    nv_vk_pipeline_t unlit;
    nv_vk_pipeline_t lit;
    nv_vk_pipeline_t ctext;
    nv_vk_pipeline_t line; // Draws lines. Yep.
  };
  extern nv_baked_pipelines g_Pipelines;

  typedef enum iris_pipeline_blend_preset
  {
    NVVK_BLEND_PRESET_NONE                = 0,
    NVVK_BLEND_PRESET_ALPHA               = 1,
    NVVK_BLEND_PRESET_ADDITIVE            = 2,
    NVVK_BLEND_PRESET_MULTIPLICATIVE      = 3,
    NVVK_BLEND_PRESET_PREMULTIPLIED_ALPHA = 4,
    NVVK_BLEND_PRESET_SUBTRACTIVE         = 5,
    NVVK_BLEND_PRESET_SCREEN              = 6,
  } iris_pipeline_blend_preset;

  struct iris_pipeline_blend_state
  {
    VkBlendFactor         src_color_blend_factor;
    VkBlendFactor         dst_color_blend_factor;
    VkBlendOp             color_blend_op;
    VkBlendFactor         src_alpha_blend_factor;
    VkBlendFactor         dst_alpha_blend_factor;
    VkBlendOp             alpha_blend_op;
    VkColorComponentFlags color_write_mask;
  };

  struct iris_pipeline_create_info
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
    const iris_pipeline_blend_state* blend_state;

    //	Array pointers are allowed to be NULL
    const VkVertexInputAttributeDescription* attribute_descriptions;
    const VkVertexInputBindingDescription*   binding_descriptions;
    const VkDescriptorSetLayout*             descriptor_layouts;
    const VkPushConstantRange*               push_constants;
    const nvsm_shader_t* const*              shaders;

    size_t n_attribute_descriptions;
    size_t n_binding_descriptions;
    size_t n_descriptor_layouts;
    size_t n_push_constants;
    size_t n_shaders;
  };

  struct iris_swapchain_create_info
  {
    VkExtent2D       extent;
    VkPresentModeKHR present_mode;
    u64              image_count;
    nv_format        format;
    VkColorSpaceKHR  color_space;
    VkSwapchainKHR   old_swapchain;
  };

  struct iris_render_pass_create_info
  {
    u64       subpass;
    nv_format format;
    nv_format depth_buffer_format;

    // Ignored if flags does not contain PIPELINE_CREATE_FLAGS_ENABLE_MULTISAMPLING
    VkSampleCountFlagBits samples;
  };

  extern iris_swapchain_create_info iris_init_swapchain_create_info(void);
  extern iris_pipeline_blend_state  iris_init_pipeline_blend_state(iris_pipeline_blend_preset preset);
#define iris_init_pipeline_create_info()                                                                                                                                      \
  (iris_pipeline_create_info) { .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .samples = VK_SAMPLE_COUNT_1_BIT }
#define iris_init_render_pass_create_info()                                                                                                                                   \
  (iris_render_pass_create_info) { .samples = VK_SAMPLE_COUNT_1_BIT }

  extern nv_error nv_vk_bake_global_pipelines(struct nvsm_ctx_t* ctx, nv_renderer_t* rd);
  extern void     nv_vk_destroy_global_pipelines(nvvk_ctx_t* vkctx);

  extern void iris_create_graphics_pipeline(nvvk_ctx_t* vkctx, iris_pipeline_create_info const* pCreateInfo, VkPipeline* dstPipeline, u32 flags);
  extern void iris_create_depth_pipeline(nvvk_ctx_t* vkctx, iris_pipeline_create_info const* pCreateInfo, VkPipeline* dstPipeline, u32 flags);
  extern void iris_create_pipeline_layout(nvvk_ctx_t* vkctx, iris_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout);
  extern void iris_create_render_pass(nvvk_ctx_t* vkctx, iris_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags);
  extern void iris_create_depth_pass(nvvk_ctx_t* vkctx, iris_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags);
  extern void iris_create_swapchain(nvvk_ctx_t* vkctx, iris_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain);

#ifdef __cplusplus
}
#endif

#endif // NOVA_PIPELINE_H
