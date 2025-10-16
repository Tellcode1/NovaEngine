#include "../../include/iris/utils.h"
#include "../../external/volk/volk.h"
#include "../../include/ctext/ctext.h"
#include "../../include/engine/camera.h"
#include "../../include/engine/format.h"
#include "../../include/engine/image.h"
#include "../../include/engine/renderer.h"
#include "../../include/iris/descriptors.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/framebuffer.h"
#include "../../include/iris/pipeline.h"
#include "../../include/iris/types.h"
#include "../../include/shadersystem/nvsm.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/math/mat.h"
#include "../../include/std/include/math/math.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/math/vec3.h"
#include "../../include/std/include/math/vec4.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define HAS_FLAG(flag) ((vkctx->flag_register & (flag)) || (flags & (flag)))
#define STR(s) #s

static inline nv_error
bake_unlit_pipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  VkDescriptorSetLayoutBinding bindings[] = {
    // binding; descriptorType; descriptorCount; stageFlags;
    // pImmutableSamplers;
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
  };

  VkDescriptorSetLayoutCreateInfo layoutinfo = nv_zero_init(VkDescriptorSetLayoutCreateInfo);
  layoutinfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutinfo.pBindings                       = bindings;
  layoutinfo.bindingCount                    = 1;
  nvvk_result_check(*rd->vkctx, vkCreateDescriptorSetLayout(rd->vkctx->device, &layoutinfo, &rd->vkctx->vkalloc, &g_Pipelines.unlit.descriptor_layout));

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "Unlit/vert", &vertex) == 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "Unlit/frag", &fragment) == 0, NV_ERROR_EXTERNAL);

  nv_assert(vertex != NULL && fragment != NULL);

  const nvsm_shader_t*        shaders[] = { vertex, fragment };
  const VkDescriptorSetLayout layouts[] = { camera.descriptor_sets->layout, g_Pipelines.unlit.descriptor_layout };

  const nv_extent2 RenderExtent = nv_rdr_get_render_extent(rd);

  const VkVertexInputAttributeDescription attributeDescriptions[] = {
    // location; binding; format; offset;
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },                                // pos
    { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(vec3f) },                       // texcoord
    { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(vec3f) + sizeof(vec2f) }, // color
  };

  const VkVertexInputBindingDescription bindingDescriptions[] = {
    // binding; stride; inputRate
    {
        0,
        sizeof(vec3f) + sizeof(vec2f) + sizeof(vec4f),
        VK_VERTEX_INPUT_RATE_VERTEX,
    },
  };

  const VkPushConstantRange pushConstants[] = { // stageFlags, offset, size
                                                { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4f) + sizeof(vec4f) + sizeof(vec2f) }
  };

  iris_pipeline_create_info pc = iris_init_pipeline_create_info();
  pc.format                    = rd->swapchain.image_format;
  pc.subpass                   = 0;
  pc.render_pass               = nv_rdr_get_render_pass(rd);

  pc.n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.attribute_descriptions   = attributeDescriptions;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.push_constants   = pushConstants;

  pc.n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.binding_descriptions   = bindingDescriptions;

  pc.n_shaders = nv_arrlen(shaders);
  pc.shaders   = (const nvsm_list_file_entry_t* const*)shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.descriptor_layouts   = layouts;

  pc.extent.width  = RenderExtent.width;
  pc.extent.height = RenderExtent.height;
  pc.samples       = (VkSampleCountFlagBits)rd->samples;
  iris_create_pipeline_layout(rd->vkctx, &pc, &g_Pipelines.unlit.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.unlit.pipeline_layout;
  iris_create_graphics_pipeline(rd->vkctx, &pc, &g_Pipelines.unlit.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
bake_ctext_pipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  const VkVertexInputAttributeDescription attributeDescriptions[] = {
    // location; binding; format; offset;
    { 0, 0, (VkFormat)nv_format_to_vk_format(NOVA_FORMAT_RGB32), 0 },            // pos
    { 1, 0, (VkFormat)nv_format_to_vk_format(NOVA_FORMAT_RG32), sizeof(vec3f) }, // uv
  };

  const VkVertexInputBindingDescription bindingDescriptions[] = {
    // binding; stride; inputRate
    { 0, sizeof(vec3f) + sizeof(vec2f), VK_VERTEX_INPUT_RATE_VERTEX },
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct ctext_push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "ctext/vert", &vertex) == 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "ctext/frag", &fragment) == 0, NV_ERROR_EXTERNAL);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.descriptor_sets->layout, rd->ctext->desc_set->layout };

  const iris_pipeline_blend_state blend = iris_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  iris_pipeline_create_info pc = iris_init_pipeline_create_info();
  pc.format                    = rd->swapchain.image_format;
  pc.subpass                   = 0;
  pc.render_pass               = nv_rdr_get_render_pass(rd);

  pc.n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.attribute_descriptions   = attributeDescriptions;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.push_constants   = pushConstants;

  pc.n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.binding_descriptions   = bindingDescriptions;

  pc.n_shaders = nv_arrlen(shaders);
  pc.shaders   = (const nvsm_list_file_entry_t* const*)shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.descriptor_layouts   = layouts;

  const nv_extent2 RenderExtent = nv_rdr_get_render_extent(rd);
  pc.extent.width               = RenderExtent.width;
  pc.extent.height              = RenderExtent.height;
  pc.blend_state                = &blend;
  pc.samples                    = (VkSampleCountFlagBits)rd->samples;

  iris_create_pipeline_layout(rd->vkctx, &pc, &g_Pipelines.ctext.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.ctext.pipeline_layout;
  iris_create_graphics_pipeline(rd->vkctx, &pc, &g_Pipelines.ctext.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
bake_debug_line_pipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  struct line_push_constants
  {
    mat4f model;
    vec4f color;
    vec4f line_begin;
    vec4f line_end;
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "Debug/Line/vert", &vertex) == NV_SUCCESS, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "Debug/Line/frag", &fragment) == NV_SUCCESS, NV_ERROR_EXTERNAL);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.descriptor_sets->layout };

  const iris_pipeline_blend_state blend = iris_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  iris_pipeline_create_info pc = iris_init_pipeline_create_info();
  pc.format                    = rd->swapchain.image_format;

  pc.topology    = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pc.render_pass = nv_rdr_get_render_pass(rd);

  pc.n_attribute_descriptions = 0;
  pc.attribute_descriptions   = NULL;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.push_constants   = pushConstants;

  pc.n_binding_descriptions = 0;
  pc.binding_descriptions   = NULL;

  pc.n_shaders = nv_arrlen(shaders);
  pc.shaders   = (const nvsm_shader_t**)shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.descriptor_layouts   = layouts;

  const nv_extent2 rdr_extent = nv_rdr_get_render_extent(rd);
  pc.extent.width             = rdr_extent.width;
  pc.extent.height            = rdr_extent.height;
  pc.blend_state              = &blend;
  pc.samples                  = (VkSampleCountFlagBits)rd->samples;

  iris_create_pipeline_layout(rd->vkctx, &pc, &g_Pipelines.line.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.line.pipeline_layout;
  iris_create_graphics_pipeline(rd->vkctx, &pc, &g_Pipelines.line.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

nv_error
nv_vk_bake_global_pipelines(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_pass != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(camera.descriptor_sets != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(camera.descriptor_sets->layout != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  nv_error code = NV_ERROR_SUCCESS;

  if ((code = bake_unlit_pipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = bake_debug_line_pipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = bake_ctext_pipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  return NV_ERROR_SUCCESS;
}

static inline void
nv_vk_destroy_pipeline(nvvk_ctx_t* vkctx, nv_vk_pipeline_t* pipeline)
{
  if (pipeline == NULL)
  {
    return;
  }

  vkDestroyPipeline(vkctx->device, pipeline->pipeline, &vkctx->vkalloc);
  vkDestroyPipelineLayout(vkctx->device, pipeline->pipeline_layout, &vkctx->vkalloc);
  vkDestroyDescriptorSetLayout(vkctx->device, pipeline->descriptor_layout, &vkctx->vkalloc);
}

void
nv_vk_destroy_global_pipelines(nvvk_ctx_t* vkctx)
{
  nv_vk_pipeline_t pipelines[] = {
    g_Pipelines.unlit,
    g_Pipelines.ctext,
    g_Pipelines.line,
  };
  for (size_t i = 0; i < nv_arrlen(pipelines); i++)
  {
    nv_vk_destroy_pipeline(vkctx, &pipelines[i]);
  }
}

void
iris_create_graphics_pipeline(nvvk_ctx_t* vkctx, const iris_pipeline_create_info* pCreateInfo, VkPipeline* dstPipeline, u32 flags)
{
  NVVK_REQUIRED_PTR(vkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstPipeline);
  NVVK_REQUIRED_PTR(pCreateInfo->render_pass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->n_shaders, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.height, 0);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING))
  {
    // Vulkan requires samples to not be 1.
    NVVK_NOT_EQUAL_TO(pCreateInfo->samples, VK_SAMPLE_COUNT_1_BIT);
  }

  VkPipelineVertexInputStateCreateInfo const vertexInputState = {
    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount   = (u32)pCreateInfo->n_binding_descriptions,
    .pVertexBindingDescriptions      = pCreateInfo->binding_descriptions,
    .vertexAttributeDescriptionCount = (u32)pCreateInfo->n_attribute_descriptions,
    .pVertexAttributeDescriptions    = pCreateInfo->attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo const inputAssemblyState = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .topology               = pCreateInfo->topology,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport const viewportState = {
    .x        = 0,
    .y        = 0,
    .width    = (float)(pCreateInfo->extent.width),
    .height   = (float)(pCreateInfo->extent.height),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D const scissor = {
    .offset = (VkOffset2D){ 0, 0 },
    .extent = pCreateInfo->extent,
  };

  VkPipelineViewportStateCreateInfo const viewportStateCreateInfo = {
    .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext         = NULL,
    .flags         = 0,
    .viewportCount = 1,
    .pViewports    = &viewportState,
    .scissorCount  = 1,
    .pScissors     = &scissor,
  };

  VkPipelineRasterizationStateCreateInfo const rasterizerPipelineStateCreateInfo = {
    .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .depthClampEnable        = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode             = VK_POLYGON_MODE_FILL,
    .cullMode                = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_CULLING) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE,
    .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .depthBiasEnable         = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp          = 0.0f,
    .depthBiasSlopeFactor    = 0.0f,
    .lineWidth               = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo const multisamplerPipelineStageCreateInfo = {
    .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .rasterizationSamples  = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING) ? pCreateInfo->samples : VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable   = VK_FALSE,
    .minSampleShading      = 1.0f,
    .pSampleMask           = VK_NULL_HANDLE,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineColorBlendAttachmentState colorblendAttachmentState = nv_zero_init(VkPipelineColorBlendAttachmentState);

  if (pCreateInfo->blend_state != NULL)
  {
    const iris_pipeline_blend_state* blendState = pCreateInfo->blend_state;

    colorblendAttachmentState = (VkPipelineColorBlendAttachmentState){
      .blendEnable         = VK_TRUE,
      .srcColorBlendFactor = blendState->src_color_blend_factor,
      .dstColorBlendFactor = blendState->dst_color_blend_factor,
      .colorBlendOp        = blendState->color_blend_op,
      .srcAlphaBlendFactor = blendState->src_alpha_blend_factor,
      .dstAlphaBlendFactor = blendState->dst_alpha_blend_factor,
      .alphaBlendOp        = blendState->alpha_blend_op,
      .colorWriteMask      = blendState->color_write_mask,
    };
  }
  else
  {
    colorblendAttachmentState.blendEnable    = VK_FALSE;
    colorblendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  }

  VkPipelineColorBlendStateCreateInfo const colorblendState = {
    .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext           = NULL,
    .flags           = 0,
    .logicOpEnable   = VK_FALSE,
    .logicOp         = VK_LOGIC_OP_COPY,
    .attachmentCount = 1,
    .pAttachments    = &colorblendAttachmentState,
    .blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f },
  };

  VkPipelineShaderStageCreateInfo* shader_infos = (VkPipelineShaderStageCreateInfo*)nv_calloc(pCreateInfo->n_shaders * sizeof(VkPipelineShaderStageCreateInfo));
  for (size_t i = 0; i < pCreateInfo->n_shaders; i++)
  {
    if ((pCreateInfo->shaders[i] == NULL) || pCreateInfo->shaders[i]->handle == VK_NULL_HANDLE)
    {
      continue;
    }

    const VkShaderStageFlagBits stage = (VkShaderStageFlagBits)nvsm_shader_stage_from_string(pCreateInfo->shaders[i]->stage);
    if ((int)stage == -1)
    {
      nv_log_error("Error in parsing stage for shader %zu(%p): \"%s\"", i, (void*)pCreateInfo->shaders[i], pCreateInfo->shaders[i]->stage);
      continue;
    }

    shader_infos[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_infos[i].stage  = stage;
    shader_infos[i].module = (VkShaderModule)pCreateInfo->shaders[i]->handle;
    shader_infos[i].pName  = "main";
  }

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {
    .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount          = (u32)pCreateInfo->n_shaders,
    .pStages             = shader_infos,
    .pVertexInputState   = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState      = &viewportStateCreateInfo,
    .pRasterizationState = &rasterizerPipelineStateCreateInfo,
    .pMultisampleState   = &multisamplerPipelineStageCreateInfo,
    .pColorBlendState    = &colorblendState,
    .layout              = pCreateInfo->pipeline_layout,
    .renderPass          = pCreateInfo->render_pass,
    .subpass             = (u32)pCreateInfo->subpass,
    .basePipelineHandle  = pCreateInfo->old_pipeline,
    .basePipelineIndex   = 0, // ?
  };

  VkPipelineDynamicStateCreateInfo dynamicStateInfo = nv_zero_init(VkPipelineDynamicStateCreateInfo);
  const VkDynamicState             dynamicStates[]  = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DYNAMIC_VIEWPORT)) {}
  else
  {
    dynamicStateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount       = 2;
    dynamicStateInfo.pDynamicStates          = dynamicStates;
    graphicsPipelineCreateInfo.pDynamicState = &dynamicStateInfo;
  }

  VkPipelineDepthStencilStateCreateInfo depthStencilState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    depthStencilState = (VkPipelineDepthStencilStateCreateInfo){
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0,
      .depthTestEnable       = VK_TRUE,
      .depthWriteEnable      = VK_TRUE,
      .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = VK_FALSE,
      .front                 = nv_zero_init(VkStencilOpState),
      .back                  = nv_zero_init(VkStencilOpState),
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
    };

    graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilState;
  }

  nvvk_result_check(*vkctx, vkCreateGraphicsPipelines(vkctx->device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, &vkctx->vkalloc, dstPipeline));

  nv_free(shader_infos);
}

void
iris_create_render_pass(nvvk_ctx_t* vkctx, iris_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags)
{
  NVVK_REQUIRED_PTR(vkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstRenderPass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    NVVK_NOT_EQUAL_TO(pCreateInfo->depth_buffer_format, NOVA_FORMAT_UNDEFINED);
  }

  VkAttachmentDescription colorAttachmentDescription = {
    .flags          = 0,
    .format         = (VkFormat)nv_format_to_vk_format(pCreateInfo->format),
    .samples        = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING) ? pCreateInfo->samples : VK_SAMPLE_COUNT_1_BIT,
    .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp        = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
    .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };

  VkAttachmentReference colorAttachmentReference = nv_zero_init(VkAttachmentReference);
  colorAttachmentReference.attachment            = 0;
  colorAttachmentReference.layout                = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {
    .flags                   = 0,
    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .inputAttachmentCount    = 0,
    .pInputAttachments       = NULL,
    .colorAttachmentCount    = 1,
    .pColorAttachments       = &colorAttachmentReference,
    .pResolveAttachments     = NULL,
    .pDepthStencilAttachment = NULL,
    .preserveAttachmentCount = 0,
    .pPreserveAttachments    = NULL,
  };

  nv_list_t attachments;
  nv_list_init(sizeof(VkAttachmentDescription), 5, nv_allocator_c, NULL, &attachments);
  nv_list_push_back(&attachments, &colorAttachmentDescription);

  VkAttachmentDescription depthAttachment    = nv_zero_init(VkAttachmentDescription);
  VkAttachmentReference   depthAttachmentRef = nv_zero_init(VkAttachmentReference);
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    depthAttachment = (VkAttachmentDescription){
      .flags          = 0,
      .format         = (VkFormat)nv_format_to_vk_format(pCreateInfo->depth_buffer_format),
      .samples        = pCreateInfo->samples,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    depthAttachmentRef.attachment = (u32)nv_list_size(&attachments);
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    nv_list_push_back(&attachments, &depthAttachment);
  }

  VkAttachmentReference   colorAttachmentResolveRef = nv_zero_init(VkAttachmentReference);
  VkAttachmentDescription colorAttachmentResolve    = nv_zero_init(VkAttachmentDescription);
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING))
  {
    colorAttachmentResolve = (VkAttachmentDescription){
      .flags          = 0,
      .format         = (VkFormat)nv_format_to_vk_format(pCreateInfo->format),
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    colorAttachmentResolveRef.attachment = (u32)nv_list_size(&attachments);
    colorAttachmentResolveRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    nv_list_push_back(&attachments, &colorAttachmentResolve);

    subpass.pResolveAttachments = &colorAttachmentResolveRef;
  }

  VkSubpassDependency deps[2] = {
    // (1) EXTERNAL → subpass: “wait for present/host‐writes → start COLOR_ATTACHMENT_OUTPUT”
    {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        // We only need to wait on the color‐attachment stage, not BOTTOM_OF_PIPE
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,

        // No need to wait on MEMORY_READ—just ensure that the image is in a presentable state
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,

        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    },
    // (2) subpass → EXTERNAL: “color‐writes → present (transfer or host‐read)”
    {
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,

        // We only need to wait until all color writes are done
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        // The next use is present or a transfer to host, which can be considered as
        // a bottom‐of‐pipe/host‐read stage
        .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,

        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0, // No subsequent GPU read is guaranteed—we’re just transitioning to present

        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
    },
  };

  VkRenderPassCreateInfo const renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext           = NULL,
    .flags           = 0,
    .attachmentCount = (u32)nv_list_size(&attachments),
    .pAttachments    = (const VkAttachmentDescription*)nv_list_data(&attachments),
    .subpassCount    = 1,
    .pSubpasses      = &subpass,
    .dependencyCount = nv_arrlen(deps),
    .pDependencies   = deps,
  };
  nvvk_result_check(*vkctx, vkCreateRenderPass(vkctx->device, &renderPassInfo, &vkctx->vkalloc, dstRenderPass));

  nv_list_destroy(&attachments);
}

void
iris_create_pipeline_layout(nvvk_ctx_t* vkctx, iris_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout)
{
  NVVK_REQUIRED_PTR(vkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstLayout);

  // int totalLayouts = 0;
  // for (size_t i = 0; i < pCreateInfo->n_shaders; i++) {
  // 	totalLayouts += pCreateInfo->p_shaders[i]->nsetlayouts;
  // }

  // nv_list_t *sets = nv_list_init(sizeof(VkDescriptorSetLayout, nv_allocator_c),
  // totalLayouts);

  // for (size_t i = 0; i < pCreateInfo->n_shaders; i++) {
  // 	const nvsm_shader_t *shader = pCreateInfo->p_shaders[i];
  // 	for (int j = 0; j < shader->nsetlayouts; j++) {
  // 		nv_list_push_back(sets, &shader->setlayouts[j]);
  // 	}
  // }

  VkPipelineLayoutCreateInfo const pipelineLayoutCreateInfo = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = (u32)pCreateInfo->n_descriptor_layouts,
    .pSetLayouts            = pCreateInfo->descriptor_layouts,
    .pushConstantRangeCount = (u32)pCreateInfo->n_push_constants,
    .pPushConstantRanges    = pCreateInfo->push_constants,
  };
  nvvk_result_check(*vkctx, vkCreatePipelineLayout(vkctx->device, &pipelineLayoutCreateInfo, &vkctx->vkalloc, dstLayout));
}

static inline const char*
iris_present_mode_to_string(VkPresentModeKHR present_mode)
{
  switch (present_mode)
  {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR"; break;
    case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR"; break;
    case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR"; break;
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR"; break;
    case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: return "VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR"; break;
    case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: return "VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR"; break;
    case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT: return "VK_PRESENT_MODE_FIFO_LATEST_READY_EXT"; break;
    default:
    case VK_PRESENT_MODE_MAX_ENUM_KHR: return "(Invalid present mode)"; break;
  }
}

void
iris_create_swapchain(nvvk_ctx_t* vkctx, iris_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain)
{
  NVVK_REQUIRED_PTR(vkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.height, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->image_count, 0);

  /* Used to check for errors or unavailable settings */
  /* These are the variables passed to the create function*/
  VkPresentModeKHR   present_mode   = pCreateInfo->present_mode;
  VkSurfaceFormatKHR surface_format = (VkSurfaceFormatKHR){ (VkFormat)nv_format_to_vk_format(pCreateInfo->format), pCreateInfo->color_space };

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  u32 present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(vkctx->phys_device, vkctx->surface, &present_mode_count, NULL);
  VkPresentModeKHR* present_modes = (VkPresentModeKHR*)nv_allocator_estack(&stack, NULL, NV_ALLOC_NEW_BLOCK, present_mode_count * sizeof(VkPresentModeKHR));
  nv_assert_else_return(present_modes != NULL, );
  vkGetPhysicalDeviceSurfacePresentModesKHR(vkctx->phys_device, vkctx->surface, &present_mode_count, present_modes);

  bool found_present_mode = false;
  for (u32 i = 0; i < present_mode_count; i++)
  {
    if (present_modes[i] == pCreateInfo->present_mode)
    {
      found_present_mode = true;
      break;
    }
  }

  nv_allocator_estack(&stack, present_modes, present_mode_count * sizeof(VkPresentModeKHR), NV_ALLOC_FREE);

  const VkPresentModeKHR fallback_present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (!found_present_mode)
  {
    nv_log_warning("Present mode %s unavailable. Using %s.\n", iris_present_mode_to_string(pCreateInfo->present_mode), iris_present_mode_to_string(fallback_present_mode));
    present_mode = fallback_present_mode;
  }

  u32 surface_format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vkctx->phys_device, vkctx->surface, &surface_format_count, NULL);
  VkSurfaceFormatKHR* surface_formats = (VkSurfaceFormatKHR*)nv_allocator_estack(&stack, NULL, NV_ALLOC_NEW_BLOCK, sizeof(VkSurfaceFormatKHR) * surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vkctx->phys_device, vkctx->surface, &surface_format_count, surface_formats);

  const VkSurfaceFormatKHR* fallback = &surface_formats[0];

  bool found_surface_format = false;
  for (u32 i = 0; i < surface_format_count; i++)
  {
    const VkSurfaceFormatKHR* vk_surface_format = &surface_formats[i];
    const VkFormat            vk_format         = (VkFormat)nv_format_to_vk_format(pCreateInfo->format);

    if (vk_surface_format->format == vk_format && vk_surface_format->colorSpace == pCreateInfo->color_space)
    {
      found_surface_format = true;
      break;
    }
  }

  if (!found_surface_format)
  {
    nv_log_error(
        "Surface format (VkSurfaceFormatKHR)(format=%u,colorspace=%u) is not an available pair."
        "Using (VkSurfaceFormatKHR)(format=%u,colorspace=%u)",
        pCreateInfo->format,
        pCreateInfo->color_space,
        fallback->format,
        fallback->colorSpace);

    surface_format.format     = fallback->format;
    surface_format.colorSpace = fallback->colorSpace;
  }

  nv_allocator_estack(&stack, surface_formats, surface_format_count * sizeof(VkSurfaceFormatKHR), NV_ALLOC_FREE);
  fallback = NULL;

  VkSwapchainCreateInfoKHR const swapChainCreateInfo = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext                 = NULL,
    .flags                 = 0,
    .surface               = vkctx->surface,
    .minImageCount         = (u32)pCreateInfo->image_count,
    .imageFormat           = surface_format.format,
    .imageColorSpace       = surface_format.colorSpace,
    .imageExtent           = pCreateInfo->extent,
    .imageArrayLayers      = 1,
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode           = present_mode,
    .clipped               = VK_TRUE,
    .oldSwapchain          = pCreateInfo->old_swapchain,
  };
  nvvk_result_check(*vkctx, vkCreateSwapchainKHR(vkctx->device, &swapChainCreateInfo, &vkctx->vkalloc, dstSwapchain));
}

iris_pipeline_blend_state
iris_init_pipeline_blend_state(iris_pipeline_blend_preset preset)
{
  iris_pipeline_blend_state ret = nv_zero_init(iris_pipeline_blend_state);
  ret.color_write_mask          = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  switch (preset)
  {
    case NVVK_BLEND_PRESET_NONE:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ALPHA:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ADDITIVE:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_MULTIPLICATIVE:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_DST_COLOR;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_DST_ALPHA;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_PREMULTIPLIED_ALPHA:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_SUBTRACTIVE:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.color_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.alpha_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT;
      break;
    default:
      ret.src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.color_blend_op         = VK_BLEND_OP_ADD;
      ret.src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
  }
  return ret;
}

void
nv_vk_create_buffer(
    nvvk_ctx_t* vkctx, size_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer* dstBuffer, VkDeviceMemory* retMem, bool externallyAllocated)
{
  if (size == 0)
  {
    nv_log_error("Zero size buffer requested.\n");
    return;
  }

  VkBuffer       newBuffer;
  VkDeviceMemory newMemory;

  VkBufferCreateInfo bufferCreateInfo = nv_zero_init(VkBufferCreateInfo);
  bufferCreateInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size               = size;
  bufferCreateInfo.usage              = usageFlags;
  bufferCreateInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(*vkctx, vkCreateBuffer(vkctx->device, &bufferCreateInfo, &vkctx->vkalloc, &newBuffer));

  VkMemoryRequirements bufferMemoryRequirements;
  vkGetBufferMemoryRequirements(vkctx->device, newBuffer, &bufferMemoryRequirements);

  if (!externallyAllocated)
  {
    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = bufferMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = (uint32_t)nv_vk_get_mem_type(vkctx, bufferMemoryRequirements.memoryTypeBits, propertyFlags);
    nvvk_result_check(*vkctx, vkAllocateMemory(vkctx->device, &allocInfo, &vkctx->vkalloc, &newMemory));

    nvvk_result_check(*vkctx, vkBindBufferMemory(vkctx->device, newBuffer, newMemory, 0));
    *retMem = newMemory;
  }

  *dstBuffer = newBuffer;
}

u32
nv_vk_get_mem_type(nvvk_ctx_t* vkctx, const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties)
{
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(vkctx->phys_device, &properties);

  for (u32 i = 0; i < properties.memoryTypeCount; i++)
  {
    if ((memoryTypeBits & (1 << i)) != 0 && (properties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties)
    {
      return i;
    }
  }

  return UINT32_MAX;
}

VkCommandBuffer
nv_vk_begin_command_buffer_from(VkCommandBuffer src)
{
  VkCommandBufferBeginInfo beginInfo = nv_zero_init(VkCommandBufferBeginInfo);
  beginInfo.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(src, &beginInfo);

  return src;
}

VkCommandBuffer
nv_vk_begin_command_buffer(iris_driver_t* driver)
{
  nvvk_ctx_t* vkctx = driver->vkctx;

  if (vkctx->cmd_pool == VK_NULL_HANDLE)
  {
    VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
    cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.queueFamilyIndex        = vkctx->graphics_family_index;
    cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    nvvk_result_check(*vkctx, vkCreateCommandPool(vkctx->device, &cmdPoolCreateInfo, &vkctx->vkalloc, &vkctx->cmd_pool));

    VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
    cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount          = IRIS_COMMAND_BUFFER_CACHE_COUNT;
    cmdAllocInfo.commandPool                 = vkctx->cmd_pool;
    nvvk_result_check(*vkctx, vkAllocateCommandBuffers(vkctx->device, &cmdAllocInfo, vkctx->cmd_buffers));

    VkFenceCreateInfo const fence_create_info = (VkFenceCreateInfo){
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
    };
    for (size_t i = 0; i < nv_arrlen(vkctx->cmd_buffer_fences); i++)
    {
      nvvk_result_check(*vkctx, vkCreateFence(vkctx->device, &fence_create_info, &vkctx->vkalloc, &vkctx->cmd_buffer_fences[i]));
    }
  }
  VkCommandBuffer free_cmd_buffer = VK_NULL_HANDLE;

  for (size_t i = 0; i < IRIS_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    if (!vkctx->cmd_buffers_in_use[i])
    {
      free_cmd_buffer              = vkctx->cmd_buffers[i];
      vkctx->cmd_buffers_in_use[i] = true;
      break;
    }
  }

  if (free_cmd_buffer == VK_NULL_HANDLE)
  {
    nv_log_error("No free command buffers\n");
    return VK_NULL_HANDLE;
  }

  return nv_vk_begin_command_buffer_from(free_cmd_buffer);
}

VkResult
nv_vk_end_command_buffer(iris_driver_t* driver, VkCommandBuffer cmd, VkQueue queue, bool waitForExecution)
{
  nvvk_ctx_t* vkctx = driver->vkctx;

  size_t cmd_index = SIZE_MAX;
  for (size_t i = 0; i < IRIS_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    if (vkctx->cmd_buffers[i] == cmd)
    {
      cmd_index = i;
      break;
    }
  }

  VkResult res = vkEndCommandBuffer(cmd);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  VkSubmitInfo submitInfo       = nv_zero_init(VkSubmitInfo);
  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers    = &cmd;

  VkFence fence = vkctx->cmd_buffer_fences[cmd_index];
  nv_assert_else_return(fence != VK_NULL_HANDLE, VK_ERROR_UNKNOWN);

  res = vkQueueSubmit(queue, 1, &submitInfo, fence);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  if (waitForExecution)
  {
    res = vkWaitForFences(driver->vkctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
    {
      return res;
    }
    vkResetFences(driver->vkctx->device, 1, &fence);
  }

  vkctx->cmd_buffers_in_use[cmd_index] = false;

  return VK_SUCCESS;
}

void
nv_vk_stage_image_transfer(iris_driver_t* driver, VkImage dst, const void* data, size_t width, size_t height, size_t image_size)
{
  nvvk_ctx_t* vkctx = driver->vkctx;

  VkBuffer       stagingBuffer       = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(vkctx->device, dst, &mem_req);

  const VkBufferCreateInfo stagingBufferInfo = {
    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size        = mem_req.size,
    .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  nvvk_result_check(*vkctx, vkCreateBuffer(vkctx->device, &stagingBufferInfo, &vkctx->vkalloc, &stagingBuffer));

  VkMemoryRequirements stagingBufferRequirements;
  vkGetBufferMemoryRequirements(vkctx->device, stagingBuffer, &stagingBufferRequirements);

  const VkMemoryAllocateInfo stagingBufferAllocInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize  = stagingBufferRequirements.size,
    .memoryTypeIndex = nv_vk_get_mem_type(vkctx, stagingBufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  nvvk_result_check(*vkctx, vkAllocateMemory(vkctx->device, &stagingBufferAllocInfo, &vkctx->vkalloc, &stagingBufferMemory));
  nvvk_result_check(*vkctx, vkBindBufferMemory(vkctx->device, stagingBuffer, stagingBufferMemory, 0));

  void* stagingBufferMapped;
  nvvk_result_check(*vkctx, vkMapMemory(vkctx->device, stagingBufferMemory, 0, stagingBufferRequirements.size, 0, &stagingBufferMapped));
  nv_memcpy(stagingBufferMapped, data, image_size);
  vkUnmapMemory(vkctx->device, stagingBufferMemory);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(driver);

  nv_vk_insert_texture_layout_transition(
      cmd,
      dst,
      1,
      VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      0,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy const region = {
    .bufferOffset      = 0,
    .bufferRowLength   = 0,
    .bufferImageHeight = 0,
    .imageSubresource  = (VkImageSubresourceLayers){ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
    .imageOffset       = (VkOffset3D){ 0, 0, 0 },
    .imageExtent       = (VkExtent3D){ (u32)width, (u32)height, 1 },
  };
  vkCmdCopyBufferToImage(cmd, stagingBuffer, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  nv_vk_insert_texture_layout_transition(
      cmd,
      dst,
      1,
      VK_IMAGE_ASPECT_COLOR_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      0,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT);

  nv_vk_end_command_buffer(driver, cmd, vkctx->transfer_queue, true);

  vkDestroyBuffer(vkctx->device, stagingBuffer, &vkctx->vkalloc);
  vkFreeMemory(vkctx->device, stagingBufferMemory, &vkctx->vkalloc);
}

void
nv_vk_create_texture_from_memory(iris_driver_t* driver, u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_vk_create_texture_empty(driver->vkctx, width, height, format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, NULL, dst, dstMem);
  nv_vk_stage_image_transfer(driver, *dst, buffer, width, height, width * height * nv_format_get_bytes_per_pixel(format));
}

// If the format given is oki then it'll just return it
// otherwise it gives you the next best optoin
nv_format
nv_vk_get_supported_format_for_draw(nvvk_ctx_t* vkctx, nv_format fmt)
{
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(vkctx->phys_device, (VkFormat)nv_format_to_vk_format(fmt), &formatProperties);

  if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
  {
    // format not supported
    // TODO: Make something that selects a format with the same number of channels as the format needed.
    nv_log_warning("Format %i (%s) unsupported. Using NOVA_FORMAT_RGBA8\n", fmt, nv_format_to_string(fmt));
    return NOVA_FORMAT_RGBA8;
  }

  return fmt;
}

void
nv_vk_create_texture_empty(
    nvvk_ctx_t*           vkctx,
    u32                   width,
    u32                   height,
    nv_format             format,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags     usage,
    size_t*               image_size,
    VkImage*              dst,
    VkDeviceMemory*       dstMem)
{
  VkPhysicalDeviceProperties device_properties = nv_zero_init(VkPhysicalDeviceProperties);
  vkGetPhysicalDeviceProperties(vkctx->phys_device, &device_properties);

  const VkPhysicalDeviceLimits* limits = &device_properties.limits;
  nv_assert_else_return(width <= limits->maxImageDimension2D, );
  nv_assert_else_return(height <= limits->maxImageDimension2D, );

  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0u)
  {
    format = nv_vk_get_supported_format_for_draw(vkctx, format);
  }

  VkImageCreateInfo imageCreateInfo = nv_zero_init(VkImageCreateInfo);
  imageCreateInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType         = VK_IMAGE_TYPE_2D;
  imageCreateInfo.extent.width      = width;
  imageCreateInfo.extent.height     = height;
  imageCreateInfo.extent.depth      = 1;
  imageCreateInfo.mipLevels         = 1;
  imageCreateInfo.arrayLayers       = 1;
  imageCreateInfo.format            = (VkFormat)nv_format_to_vk_format(format);
  imageCreateInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage             = usage;
  imageCreateInfo.samples           = samples;
  imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(*vkctx, vkCreateImage(vkctx->device, &imageCreateInfo, &vkctx->vkalloc, dst));

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(vkctx->device, *dst, &imageMemoryRequirements);

  if (image_size != NULL)
  {
    *image_size = imageMemoryRequirements.size;
  }

  // allow for preallocated memory.
  if (dstMem != NULL)
  {
    const u32 localDeviceMemoryIndex = nv_vk_get_mem_type(vkctx, imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = imageMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = localDeviceMemoryIndex;

    nvvk_result_check(*vkctx, vkAllocateMemory(vkctx->device, &allocInfo, &vkctx->vkalloc, dstMem));
    nvvk_result_check(*vkctx, vkBindImageMemory(vkctx->device, *dst, *dstMem, 0));
  }
}

u8*
nv_vk_create_texture_from_disk(iris_driver_t* driver, const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_image tex = nv_zero_init(nv_image);

  nv_error const code = nv_image_load(path, &tex);
  nv_assert_else_return(code == NV_SUCCESS, NULL);

  nv_assert(tex.data != NULL);

  *width    = tex.width;
  *height   = tex.height;
  *channels = tex.format;

  nv_vk_create_texture_from_memory(driver, tex.data, tex.width, tex.height, *channels, dst, dstMem);
  return tex.data;
}

void
nv_vk_insert_texture_layout_transition(
    VkCommandBuffer       cmd,
    VkImage               image,
    u32                   mipLevels,
    VkImageAspectFlagBits aspect,
    VkImageLayout         oldLayout,
    VkImageLayout         newLayout,
    VkAccessFlags         srcAccessMask,
    VkAccessFlags         dstAccessMask,
    VkPipelineStageFlags  sourceStage,
    VkPipelineStageFlags  destinationStage)
{
  VkImageMemoryBarrier barrier            = nv_zero_init(VkImageMemoryBarrier);
  barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout                       = oldLayout;
  barrier.newLayout                       = newLayout;
  barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
  barrier.image                           = image;
  barrier.subresourceRange.aspectMask     = aspect;
  barrier.subresourceRange.baseMipLevel   = 0;
  barrier.subresourceRange.levelCount     = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount     = 1;
  barrier.srcAccessMask                   = srcAccessMask;
  barrier.dstAccessMask                   = dstAccessMask;
  vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

bool
nv_vk_get_supported_format(nvvk_ctx_t* vkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dst_format, VkColorSpaceKHR* dst_color_space)
{
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);
  NVVK_REQUIRED_PTR(dst_format);
  NVVK_REQUIRED_PTR(dst_color_space);

  u32 formatCount = 0;
  nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, VK_NULL_HANDLE));
  nv_list_t surface_formats;
  nv_list_init(sizeof(VkSurfaceFormatKHR), formatCount, nv_allocator_c, NULL, &surface_formats);
  nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, (VkSurfaceFormatKHR*)nv_list_data(&surface_formats)));

  VkSurfaceFormatKHR selected_format = { VK_FORMAT_MAX_ENUM, VK_COLOR_SPACE_MAX_ENUM_KHR };

  const VkSurfaceFormatKHR desired_formats[] = { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
                                                 { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };

  for (u32 i = 0; i < formatCount; i++)
  {
    const VkSurfaceFormatKHR* surface_format = (VkSurfaceFormatKHR*)nv_list_get(&surface_formats, i);
    for (u32 j = 0; j < nv_arrlen(desired_formats); j++)
    {
      if (surface_format->format == desired_formats[j].format && surface_format->colorSpace == desired_formats[j].colorSpace)
      {
        selected_format.format     = surface_format->format;
        selected_format.colorSpace = surface_format->colorSpace;
      }
    }
  }

  nv_list_destroy(&surface_formats);
  if (selected_format.format == VK_FORMAT_MAX_ENUM || selected_format.colorSpace == VK_COLOR_SPACE_MAX_ENUM_KHR)
  {
    return VK_FALSE;
  }
  else
  {
    *dst_format      = nv_format_from_vk_format(selected_format.format);
    *dst_color_space = selected_format.colorSpace;

    return VK_TRUE;
  }

  return VK_FALSE;
}

u32
nv_vk_get_surface_image_count(nvvk_ctx_t* vkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface)
{
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  nvvk_result_check(*vkctx, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface, &surfaceCapabilities));

  u32 requestedImageCount = surfaceCapabilities.minImageCount + 1;
  if (requestedImageCount < surfaceCapabilities.maxImageCount)
  {
    requestedImageCount = surfaceCapabilities.maxImageCount;
  }

  return requestedImageCount;
}

const char*
nvvk_vk_result_to_string(VkResult r)
{
  switch (r)
  {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
    case VK_ERROR_NOT_PERMITTED: return "VK_ERROR_NOT_PERMITTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR: return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR: return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
    case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR: return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
    case VK_ERROR_COMPRESSION_EXHAUSTED_EXT: return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
    case VK_INCOMPATIBLE_SHADER_BINARY_EXT: return "VK_INCOMPATIBLE_SHADER_BINARY_EXT";
    case VK_PIPELINE_BINARY_MISSING_KHR: return "VK_PIPELINE_BINARY_MISSING_KHR";
    case VK_ERROR_NOT_ENOUGH_SPACE_KHR: return "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
    case VK_RESULT_MAX_ENUM: return "VK_RESULT_MAX_ENUM";
    default: return "(Not A VkResult)";
  }
}

int
iris_create_framebuffer(nvvk_ctx_t* vkctx, const iris_framebuffer_create_info_t* pCreateInfo, iris_framebuffer_t* dst)
{
  nv_assert_else_return(pCreateInfo != NULL, -1);
  nv_assert_else_return(pCreateInfo->attachments != NULL, -1);
  nv_assert_else_return(pCreateInfo->num_attachments != 0, -1);
  nv_assert_else_return(pCreateInfo->extent.width != 0, -1);
  nv_assert_else_return(pCreateInfo->extent.height != 0, -1);
  nv_assert_else_return(pCreateInfo->num_layers != 0, -1);
  nv_assert_else_return(pCreateInfo->pass != VK_NULL_HANDLE, -1);
  nv_assert_else_return(dst != NULL, -1);

  nv_bzero(dst, sizeof(iris_framebuffer_t));

  dst->attachments     = pCreateInfo->attachments;
  dst->num_attachments = pCreateInfo->num_attachments;
  dst->pass            = pCreateInfo->pass;
  dst->extent          = pCreateInfo->extent;
  dst->num_layers      = pCreateInfo->num_layers;

  VkFramebufferCreateInfo framebufferInfo = nv_zero_init(VkFramebufferCreateInfo);
  framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass              = pCreateInfo->pass;
  framebufferInfo.attachmentCount         = pCreateInfo->num_attachments;
  framebufferInfo.pAttachments            = pCreateInfo->attachments;
  framebufferInfo.width                   = pCreateInfo->extent.width;
  framebufferInfo.height                  = pCreateInfo->extent.height;
  framebufferInfo.layers                  = 1;
  nvvk_result_check(*vkctx, vkCreateFramebuffer(vkctx->device, &framebufferInfo, &vkctx->vkalloc, &dst->handle));

  return 0;
}

void
iris_destroy_framebuffer(nvvk_ctx_t* vkctx, iris_framebuffer_t* dst)
{
  if (dst == NULL)
  {
    return;
  }

  vkDestroyFramebuffer(vkctx->device, dst->handle, &vkctx->vkalloc);
}
