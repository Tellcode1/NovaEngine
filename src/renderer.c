#include "../include/engine/renderer.h"
#include "../external/volk/volk.h"
#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/format.h"
#include "../include/engine/input.h"
#include "../include/engine/sprite.h"
#include "../include/engine/ui.h"
#include "../include/iris/buffer.h"
#include "../include/iris/descriptors.h"
#include "../include/iris/driver.h"
#include "../include/iris/framebuffer.h"
#include "../include/iris/memory.h"
#include "../include/iris/pipeline.h"
#include "../include/iris/ringbuffer.h"
#include "../include/iris/texture.h"
#include "../include/iris/types.h"
#include "../include/iris/utils.h"
#include "../include/shadersystem/nvsm.h"
#include "../include/std/include/alloc.h"
#include "../include/std/include/containers/list.h"
#include "../include/std/include/errorcodes.h"
#include "../include/std/include/math/mat.h"
#include "../include/std/include/math/math.h"
#include "../include/std/include/math/vec2.h"
#include "../include/std/include/math/vec3.h"
#include "../include/std/include/math/vec4.h"
#include "../include/std/include/stdafx.h"
#include "../include/std/include/string.h"
#include "../include/std/include/types.h"
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>

/**
 * Remove these global variables
 */
nv_baked_pipelines   g_Pipelines;
nv_descriptor_pool_t g_pool;
nv_camera_t          camera;

nv_extent2d
nv_get_window_size(nv_ctx_t* ctx)
{
  int ww, wh;
  SDL_GetWindowSize(ctx->window, &ww, &wh);
  return (nv_extent2d){ (size_t)ww, (size_t)wh };
}

VKAPI_ATTR VkBool32 VKAPI_CALL nvvk_debug_messenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData);

typedef struct nv_quad_vertex_t nv_quad_vertex_t;

struct nv_quad_vertex_t
{
  vec3f position;
  vec2f tex_coords;
  vec4f color;
};

static nv_quad_vertex_t quad_vertices[4];

static const uint32_t quad_indices[] = { 0, 1, 2, 0, 2, 3 };

size_t
nv_camera_get_read_offset(const nv_camera_t* cam)
{
  return iris_ring_buffer_offset(&cam->uniform_buffer);
}

size_t
nv_camera_get_write_offset(const nv_camera_t* cam)
{
  return iris_ring_buffer_offset(&cam->uniform_buffer);
}

u32
nv_renderer_get_frame(const nv_renderer_t* rd)
{
  return rd->frame_index;
}

VkCommandBuffer
nv_renderer_get_draw_buffer(const nv_renderer_t* rd)
{
  nv_rdr_per_image_data_t* access = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, rd->image_index);
  if (NV_LIKELY(access))
  {
    return access->cmd;
  }
  return VK_NULL_HANDLE;
}

VkRenderPass
nv_renderer_get_render_pass(const nv_renderer_t* rd)
{
  return rd->render_pass;
}

nv_extent2d
nv_renderer_get_render_extent(const nv_renderer_t* rd)
{
  return rd->render_extent;
}

u32
nv_renderer_get_frames_in_flight(const nv_renderer_t* rd)
{
  return 1U + (u32)rd->buffer_mode;
}

#define ABSF(x) (((x) >= 0.0f) ? (x) : -(x))

// static inline bool
// nv_is_quad_visible_through_default_camera(const vec3* pos, const vec3* siz)
// {
//   const float half_width  = siz->x * 0.5f;
//   const float half_height = siz->y * 0.5f;
//   const float deltax      = pos->x - camera.position.x;
//   const float deltay      = pos->y - camera.position.y;
//   const float dx          = ABSF(deltax); // delta x & y
//   const float dy          = ABSF(deltay); //

//   return (dx <= (half_width + camera.ortho_size.x) && dy <= (half_height + camera.ortho_size.y));
// }

void
nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite_t* spr, vec2 tex_coord_multiplier, vec3 position, vec3 size, vec4 color, int layer)
{
  if (spr == NULL)
  {
    spr = &rd->sprite_empty;
  }

  nv_draw_call_t drawcall = (nv_draw_call_t){ .type     = NOVA_DRAWCALL_QUAD,
                                              .layer    = layer,
                                              .drawcall = { .quad = {
                                                                .spr            = spr,
                                                                .siz            = size,
                                                                .pos            = position,
                                                                .tex_multiplier = tex_coord_multiplier,
                                                                .color            = color,
                                                            }, }, };
  nv_list_push_back(&rd->drawcalls, &drawcall);
}

void
nv_renderer_render_line(nv_renderer_t* rd, vec3 start, vec3 end, vec4 color, int layer)
{
  nv_draw_call_t drawcall = (nv_draw_call_t){ .type = NOVA_DRAWCALL_LINE, .layer = layer, .drawcall = { .line = { .begin = start, .end = end, .color = color, }, }, };
  nv_list_push_back(&rd->drawcalls, &drawcall);
}

// thjs will just sort the array from small layer to big layer :> and try to group the types together
static inline int
drawcall_compar(const void* obj1, const void* obj2)
{
  const nv_draw_call_t* call1 = (nv_draw_call_t*)obj1;
  const nv_draw_call_t* call2 = (nv_draw_call_t*)obj2;
  return ((int)call1->layer - (int)call2->layer) + ((int)call1->type - (int)call2->type);
}

static inline void
nv_renderer_flush_renders(nv_renderer_t* rd)
{
  const uint32_t camera_ub_offset = nv_camera_get_read_offset(&camera);

  VkCommandBuffer cmd = nv_renderer_get_draw_buffer(rd);
  nv_assert_else_return(cmd != VK_NULL_HANDLE, );

  VkDescriptorSet camera_set = camera.descriptor_sets->set;
  nv_assert_else_return(camera_set != VK_NULL_HANDLE, );

  bool bound_quad_state = false;

  nv_list_sort(&rd->drawcalls, drawcall_compar);

  nv_draw_call_type state = NOVA_DRAWCALL_INVALID;

  for (size_t i = 0; i < nv_list_size(&rd->drawcalls); i++)
  {
    const nv_draw_call_t* drawcall = &((nv_draw_call_t*)nv_list_data(&rd->drawcalls))[i];
    nv_assert_and_exec(drawcall != NULL, { continue; });

    if (drawcall->type == NOVA_DRAWCALL_QUAD)
    {
      // if (!nv_Quad_Visible(&drawcall->drawcall.quad.pos,
      // &drawcall->drawcall.quad.siz)) {
      //     continue;
      // }

      if (state != NOVA_DRAWCALL_QUAD)
      {
        VkDeviceSize const offsets = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.unlit.pipeline);

        if (!bound_quad_state)
        {
          vkCmdBindVertexBuffers(cmd, 0, 1, &rd->quad_vb.handle, &offsets);
          vkCmdBindIndexBuffer(cmd, rd->quad_vb.handle, sizeof(quad_vertices), VK_INDEX_TYPE_UINT32);
        }

        state            = NOVA_DRAWCALL_QUAD;
        bound_quad_state = true;
      }

      struct push_constants
      {
        mat4f model;
        vec4f color;
        vec2f tex_multiplier; // Multiplied with the tex coords
      } pc;

      const mat4 scale     = m4scale(m4init(1.0f), v3muls(drawcall->drawcall.quad.siz, 2.0f));
      const mat4 rotate    = m4init(1.0f);
      const mat4 translate = m4translate(m4init(1.0f), drawcall->drawcall.quad.pos);

      mat4 model = m4mul(translate, m4mul(rotate, scale));

      NVM_MATRIX_COPY(pc.model, model);
      NVM_VEC_COPY(pc.color, drawcall->drawcall.quad.color);
      NVM_VEC_COPY(pc.tex_multiplier, drawcall->drawcall.quad.tex_multiplier);
      vkCmdPushConstants(cmd, g_Pipelines.unlit.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

      VkDescriptorSet       sprite_set = nv_sprite_get_descriptor_set(drawcall->drawcall.quad.spr);
      const VkDescriptorSet sets[]     = { camera_set, sprite_set };

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.unlit.pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
      vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }
    else if (drawcall->type == NOVA_DRAWCALL_LINE)
    {
      if (state != NOVA_DRAWCALL_LINE)
      {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.line.pipeline);

        state = NOVA_DRAWCALL_LINE;
      }

      const VkDescriptorSet sets[] = { camera_set };
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.line.pipeline_layout, 0, 1, sets, 1, &camera_ub_offset);

      struct line_push_constants
      {
        mat4f model;
        vec4f color;
        vec4f line_begin;
        vec4f line_end;
      } pc;
      const vec3 begin = drawcall->drawcall.line.begin;
      const vec3 end   = drawcall->drawcall.line.end;
      pc.model         = m4finit(1.0f);
      NVM_VEC_COPY(pc.color, drawcall->drawcall.line.color);
      NVM_VEC_COPY(pc.line_begin, begin);
      NVM_VEC_COPY(pc.line_end, end);
      vkCmdPushConstants(cmd, g_Pipelines.line.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants), &pc);

      vkCmdDraw(cmd, 2, 1, 0, 0);
    }
  }

  nv_list_clear(&rd->drawcalls);
}

static inline nv_error
nv_renderer_prepare_quad_renderer(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);

  nv_memcpy(
      quad_vertices,
      (const nv_quad_vertex_t[4]){
          (const nv_quad_vertex_t){ .position = (vec3f){ +0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 0.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ -0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 0.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ -0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 1.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ +0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 1.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } } },
      sizeof(quad_vertices));

  nv_error code = iris_buffer_init(
      rd->driver, sizeof(quad_vertices) + sizeof(quad_indices), 1, NULL, IRIS_BUFFER_FLAGS_VERTEX_BUFFER_BIT | IRIS_BUFFER_FLAGS_INDEX_BUFFER_BIT, &rd->quad_vb);
  nv_assert_else_return(code == NV_SUCCESS, code);

  uchar data[sizeof(quad_vertices) + sizeof(quad_indices)];
  nv_assert_else_return(data != NULL, NV_ERROR_MALLOC_FAILED);

  nv_memcpy(data, quad_vertices, sizeof(quad_vertices));
  nv_memcpy((char*)data + sizeof(quad_vertices), quad_indices, sizeof(quad_indices));

  nv_assert_else_return(rd->quad_vb.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  iris_buffer_write_data(&rd->quad_vb, data, sizeof(quad_vertices) + sizeof(quad_indices), 0);

  return NV_ERROR_SUCCESS;
}

void
nv_renderer_destroy(nv_renderer_t* rd)
{
  if (rd == NULL)
  {
    return;
  }

  vkDeviceWaitIdle(rd->vkctx->device);

  // we were only making frames_in_flight fences and it was working for some
  // reason!! that was the reason we were getting errors!
  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->main_pass.render_data, i);

    // as the view was silently smushed into the
    // structure, we just kinda smush it out as well.
    vkDestroyImageView(rd->vkctx->device, data->swapchain_image_view, &rd->vkctx->vkalloc);

    iris_texture_destroy(&data->depth_image);

    iris_destroy_framebuffer(rd->vkctx, &data->color_framebuffer);
  }

  for (size_t i = 0; i < nv_list_size(&rd->main_pass.per_frame_data); i++)
  {
    nv_rdr_per_frame_data_t* image_frame = (nv_rdr_per_frame_data_t*)nv_list_get(&rd->main_pass.per_frame_data, i);
    vkDestroyFence(rd->vkctx->device, image_frame->in_flight_fence, &rd->vkctx->vkalloc);
    vkDestroySemaphore(rd->vkctx->device, image_frame->image_available_semaphore, &rd->vkctx->vkalloc);
  }
  for (size_t i = 0; i < nv_list_size(&rd->main_pass.per_image_data); i++)
  {
    nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    vkDestroySemaphore(rd->vkctx->device, image_frame->render_finish_semaphore, &rd->vkctx->vkalloc);
  }

  nv_sprite_destroy(&rd->sprite_empty);

  nv_camera_destroy(&camera);

  nv_vk_destroy_global_pipelines(rd->vkctx);
  nv_descriptor_pool_destroy(rd->vkctx, &g_pool);

  ctext_shutdown(rd);

  // iris_memory_free(&rd->main_pass.depth_image_memory);

  iris_buffer_destroy(&rd->quad_vb);

  VkCommandBuffer buffer[32];
  for (size_t i = 0; i < nv_list_size(&rd->main_pass.per_image_data); i++)
  {
    nv_rdr_per_image_data_t* frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    buffer[i]                      = frame->cmd;
  }

  vkFreeCommandBuffers(rd->vkctx->device, rd->command_pool, nv_list_size(&rd->main_pass.per_image_data), buffer);
  vkDestroyCommandPool(rd->vkctx->device, rd->command_pool, &rd->vkctx->vkalloc);

  vkFreeCommandBuffers(rd->vkctx->device, rd->vkctx->cmd_pool, IRIS_COMMAND_BUFFER_CACHE_COUNT, rd->vkctx->cmd_buffers);
  vkDestroyCommandPool(rd->vkctx->device, rd->vkctx->cmd_pool, &rd->vkctx->vkalloc);

  vkDestroySwapchainKHR(rd->vkctx->device, rd->swapchain.handle, &rd->vkctx->vkalloc);

  if ((rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE) != 0u)
  {
    iris_texture_destroy(&rd->main_pass.color_image);
  }
  vkDestroyRenderPass(rd->vkctx->device, rd->render_pass, &rd->vkctx->vkalloc);

  nv_list_destroy(&rd->drawcalls);
  nv_list_destroy(&rd->main_pass.render_data);
  nv_list_destroy(&rd->main_pass.per_frame_data);
  nv_list_destroy(&rd->main_pass.per_image_data);

  nv_bzero(rd, sizeof(nv_renderer_t));
}

static inline nv_error
create_optional_images(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_MALLOC_FAILED);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->swapchain.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&rd->main_pass.render_data), NV_ERROR_MALLOC_FAILED);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_BROKEN_STATE);

  if (nvvk_result_check(*rd->vkctx, vkGetSwapchainImagesKHR(rd->vkctx->device, rd->swapchain.handle, &rd->swapchain.image_count, NULL)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  VkImage* swapchainImages = (VkImage*)nv_malloc(rd->swapchain.image_count * sizeof(VkImage));
  nv_assert_else_return(swapchainImages != NULL, NV_ERROR_MALLOC_FAILED);

  if (nvvk_result_check(*rd->vkctx, vkGetSwapchainImagesKHR(rd->vkctx->device, rd->swapchain.handle, &rd->swapchain.image_count, swapchainImages)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_list_reserve(&rd->main_pass.render_data, rd->swapchain.image_count);
  nv_list_clear(&rd->main_pass.render_data);

  if ((rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE) != 0u)
  {
    // We need transient memory for the color attachment
    iris_texture_extra_create_info_t extra = { .custom_memory_flags = IRIS_MEMORY_FLAGS_LAZILY_ALLOCATED_BIT | IRIS_MEMORY_FLAGS_DEDICATED_BIT };

    iris_texture_create_info_t const color_image_desc = {
      .extent        = (nv_extent3D){ rd->render_extent.width, rd->render_extent.height, 1 },
      .alignment     = 1,
      .array_layers  = 1,
      .format        = rd->swapchain.image_format,
      .samples       = rd->samples,
      .flags         = IRIS_TEXTURE_RESOLVE_ATTACHMENT_BIT,
      .linear_tiling = false,
      .extra         = &extra,
    };
    iris_texture_init(rd->driver, &color_image_desc, &rd->main_pass.color_image);
  }

  nv_list_resize(&rd->main_pass.render_data, rd->swapchain.image_count);

  // attachment vector will be like <color resolve, depth attachment, swapchain
  // image>
  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->main_pass.render_data, i);

    data->swapchain_image = swapchainImages[i];
    nv_assert_else_return(swapchainImages[i] != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

    const iris_texture_create_info_t image_info = {
      .extent        = (nv_extent3D){ rd->render_extent.width, rd->render_extent.height, 1 },
      .alignment     = 1,
      .array_layers  = 1,
      .format        = NOVA_FORMAT_D32,
      .samples       = rd->samples,
      .flags         = IRIS_TEXTURE_DEPTH_ATTACHMENT_BIT,
      .linear_tiling = false,
      .extra         = NULL,
    };
    iris_texture_init(rd->driver, &image_info, &data->depth_image);
    // nv_assert_else_return(data->depth_image.image != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
  }

  nv_free((void*)swapchainImages);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
create_framebuffers_and_swapchain_image_views(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_pass != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_INVALID_ARG);

  nv_list_t attachments;
  nv_error  code = NV_ERROR_SUCCESS;
  if ((code = nv_list_init(sizeof(iris_texture_t*), 3, nv_allocator_c, NULL, &attachments)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->main_pass.render_data, i);
    nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);

    nv_assert_else_return(data->swapchain_image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(rd->swapchain.image_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);

    // we don't know anything about the swapchain_image, as it's a swapchain
    // image so we have to manually create the image view;
    // we will later smush in the view using iris_texture_attach_view
    VkImageViewCreateInfo imageViewCreateInfo           = nv_zero_init(VkImageViewCreateInfo);
    imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image                           = data->swapchain_image;
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = (VkFormat)nv_format_to_vk_format(rd->swapchain.image_format);
    imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;
    VkImageView vioew;
    if (nvvk_result_check(*rd->vkctx, vkCreateImageView(rd->vkctx->device, &imageViewCreateInfo, &rd->vkctx->vkalloc, &vioew)) != VK_SUCCESS)
    {
      nv_list_destroy(&attachments);
      return NV_ERROR_EXTERNAL;
    }

    data->swapchain_image_view = vioew;

    nv_list_clear(&attachments);
    if ((rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE) != 0u)
    {
      nv_assert_else_return(rd->main_pass.color_image.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
      nv_list_push_set(
          &attachments,
          (void*)(VkImageView[]){ iris_texture_get_image_view(&rd->main_pass.color_image), iris_texture_get_image_view(&data->depth_image), data->swapchain_image_view },
          3);
    }
    else
    {
      nv_assert_else_return(data->depth_image.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
      nv_list_push_set(&attachments, (void*)(VkImageView[]){ data->swapchain_image_view, iris_texture_get_image_view(&data->depth_image) }, 2);
    }

    iris_framebuffer_create_info_t framebuffer_info = nv_zero_init(iris_framebuffer_create_info_t);
    framebuffer_info.attachments                    = (VkImageView*)nv_list_data(&attachments);
    framebuffer_info.num_attachments                = (u32)nv_list_size(&attachments);
    framebuffer_info.extent                         = rd->render_extent;
    framebuffer_info.num_layers                     = 1;
    framebuffer_info.pass                           = rd->render_pass;

    nv_assert_else_return(nv_list_data(&attachments) != NULL, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(nv_list_size(&attachments) != 0, NV_ERROR_BROKEN_STATE);

    if (iris_create_framebuffer(rd->vkctx, &framebuffer_info, &data->color_framebuffer) != 0)
    {
      nv_log_error("Error in framebuffer creation.\n");
      return code;
    }
  }

  nv_list_destroy(&attachments);
  return NV_ERROR_SUCCESS;
}

static inline nv_error
nv_renderer_initialize_rendering_components(nv_renderer_t* rd, const nv_renderer_config* conf)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(conf != NULL, NV_ERROR_INVALID_ARG);

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (conf->vsync_enabled)
  {
    switch (conf->buffer_mode)
    {
      case NOVA_BUFFER_MODE_TRIPLE_BUFFERED: present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR; break;
      case NOVA_BUFFER_MODE_SINGLE_BUFFERED:
      case NOVA_BUFFER_MODE_DOUBLE_BUFFERED:
      default: present_mode = VK_PRESENT_MODE_FIFO_KHR; break;
    };
  }
  else
  {
    present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
  }

  nv_assert_else_return(rd->swapchain.image_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->swapchain.image_count != 0, NV_ERROR_BROKEN_STATE);

  iris_swapchain_create_info swapchain_create_info = nv_zero_init(iris_swapchain_create_info);
  swapchain_create_info.extent.width               = rd->render_extent.width;
  swapchain_create_info.extent.height              = rd->render_extent.height;
  swapchain_create_info.present_mode               = present_mode;
  swapchain_create_info.format                     = rd->swapchain.image_format;
  swapchain_create_info.color_space                = (VkColorSpaceKHR)rd->swapchain.color_space;
  swapchain_create_info.image_count                = rd->swapchain.image_count;

  iris_create_swapchain(rd->vkctx, &swapchain_create_info, &rd->swapchain.handle);
  nv_assert_else_return(rd->swapchain.handle != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  VkSampleCountFlagBits conf_samples;
  if (conf->samples == NOVA_SAMPLE_COUNT_MAX_SUPPORTED)
  {
    conf_samples = (VkSampleCountFlagBits)rd->vkctx->max_samples;
  }
  else
  {
    conf_samples = (VkSampleCountFlagBits)conf->samples;
  }
  const VkSampleCountFlagBits _samples = conf->multisampling_enable ? conf_samples : VK_SAMPLE_COUNT_1_BIT;
  rd->samples                          = _samples;

  if (conf->multisampling_enable)
  {
    rd->vkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING;
  }
  rd->vkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK;
  rd->vkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_CULLING;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = rd->vkctx->graphics_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  nv_error code = NV_ERROR_SUCCESS;
  if (nvvk_result_check(*rd->vkctx, vkCreateCommandPool(rd->vkctx->device, &cmdPoolCreateInfo, &rd->vkctx->vkalloc, &rd->command_pool)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  VkCommandBuffer buffer[32];

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = rd->swapchain.image_count;
  cmdAllocInfo.commandPool                 = rd->command_pool;
  if (nvvk_result_check(*rd->vkctx, vkAllocateCommandBuffers(rd->vkctx->device, &cmdAllocInfo, buffer)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_list_resize(&rd->main_pass.per_image_data, rd->swapchain.image_count);
  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    // We already pushed new frames before while creating the synchronizaiton primitives, no need to do that here too
    nv_rdr_per_image_data_t* frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    frame->cmd                     = buffer[i];
  }

  rd->main_pass.depth_buffer_format = (VkFormat)nv_format_to_vk_format(NOVA_FORMAT_D32); // replace (probably)
  nv_assert_else_return(rd->main_pass.depth_buffer_format != VK_FORMAT_UNDEFINED, NV_ERROR_INVALID_RETVAL);

  iris_render_pass_create_info rpi = nv_zero_init(iris_render_pass_create_info);

  rpi.format              = rd->swapchain.image_format;
  rpi.depth_buffer_format = nv_vk_format_to_nv_format(rd->main_pass.depth_buffer_format);
  nv_assert_else_return(rpi.format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rpi.depth_buffer_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);

  rpi.subpass = 0;
  rpi.samples = (VkSampleCountFlagBits)rd->samples;
  iris_create_render_pass(rd->vkctx, &rpi, &rd->render_pass, rd->vkctx->flag_register);

  if ((code = create_optional_images(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = create_framebuffers_and_swapchain_image_views(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  return NV_ERROR_SUCCESS;
}

nv_error
nv_renderer_init(struct nv_ctx* ctx, nvsm_ctx_t* nvsmctx, iris_driver_t* driver, const nv_renderer_config* conf, nv_renderer_t* dst)
{
  nv_assert_else_return(conf != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->initial_window_size.width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->initial_window_size.height != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->samples != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvsmctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(iris_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->vkctx) == true, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_renderer_t));

  dst->ctx    = ctx;
  dst->driver = driver;
  dst->vkctx  = driver->vkctx;

  if (conf->multisampling_enable)
  {
    nv_assert_else_return(conf->samples != NOVA_SAMPLE_COUNT_1_SAMPLES, NV_ERROR_INVALID_ARG);
  }

  dst->swapchain.image_count = nv_vk_get_surface_image_count(dst->vkctx, dst->vkctx->phys_device, dst->vkctx->surface);
  nv_assert_else_return(dst->swapchain.image_count > 0, NV_ERROR_INVALID_RETVAL);

  if (conf->multisampling_enable)
  {
    dst->flags |= NOVA_RENDERER_MULTISAMPLING_ENABLE;
  }
  if (conf->window_resizable)
  {
    dst->flags |= NOVA_RENDERER_WINDOW_RESIZABLE;
  }
  if (conf->vsync_enabled)
  {
    dst->flags |= NOVA_RENDERER_VSYNC_ENABLE;
  }
  dst->buffer_mode = conf->buffer_mode;

  dst->render_extent.width  = conf->initial_window_size.width;
  dst->render_extent.height = conf->initial_window_size.height;

  nv_error code = NV_ERROR_SUCCESS;

  if (!nv_vk_get_supported_format(dst->vkctx, dst->vkctx->phys_device, dst->vkctx->surface, &dst->swapchain.image_format, (VkColorSpaceKHR*)&dst->swapchain.color_space))
  {
    nv_log_and_abort("No supported format for display.\n");
  }

  dst->swapchain.image_count = nv_vk_get_surface_image_count(dst->vkctx, dst->vkctx->phys_device, dst->vkctx->surface);

  code = nv_list_init(sizeof(nv_draw_call_t), 4, nv_allocator_c, NULL, &dst->drawcalls);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  code = nv_list_init(sizeof(nv_renderer_frame_render_info), dst->swapchain.image_count, nv_allocator_c, NULL, &dst->main_pass.render_data);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  code = nv_list_init(sizeof(nv_rdr_per_frame_data_t), nv_renderer_get_frames_in_flight(dst), nv_allocator_c, NULL, &dst->main_pass.per_frame_data);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  code = nv_list_init(sizeof(nv_rdr_per_image_data_t), dst->swapchain.image_count, nv_allocator_c, NULL, &dst->main_pass.per_image_data);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  nv_assert_else_return(nv_list_is_valid(&dst->drawcalls), NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&dst->main_pass.render_data), NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&dst->main_pass.per_frame_data), NV_ERROR_BROKEN_STATE);

  const VkSemaphoreCreateInfo semaphore_create_info = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
  const VkFenceCreateInfo     fence_create_info     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

  nv_list_resize(&dst->main_pass.per_frame_data, nv_renderer_get_frames_in_flight(dst));
  nv_list_resize(&dst->main_pass.per_image_data, dst->swapchain.image_count);

  for (size_t i = 0; i < dst->swapchain.image_count; i++)
  {
    nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&dst->main_pass.per_image_data, i);
    nv_assert_else_return(image_frame != NULL, NV_ERROR_BROKEN_STATE);

    nvvk_result_check(*dst->vkctx, vkCreateSemaphore(dst->vkctx->device, &semaphore_create_info, &driver->vkctx->vkalloc, &image_frame->render_finish_semaphore));
  }

  for (size_t i = 0; i < nv_list_size(&dst->main_pass.per_frame_data); i++)
  {
    nv_rdr_per_frame_data_t* frame = (nv_rdr_per_frame_data_t*)nv_list_get(&dst->main_pass.per_frame_data, i);

    nvvk_result_check(*dst->vkctx, vkCreateSemaphore(dst->vkctx->device, &semaphore_create_info, &driver->vkctx->vkalloc, &frame->image_available_semaphore));
    nvvk_result_check(*dst->vkctx, vkCreateFence(dst->vkctx->device, &fence_create_info, &driver->vkctx->vkalloc, &frame->in_flight_fence));

    nv_assert_else_return(frame->in_flight_fence != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
  }

  if ((code = nv_renderer_initialize_rendering_components(dst, conf)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = (nv_error)nv_descriptor_pool_init(dst->vkctx, &g_pool)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  const unsigned char empty_data[3] = { 255, 255, 255 }; // fill rgb with 255 so it's white
  if ((code = nv_sprite_load_from_memory(dst->driver, empty_data, 1, 1, NOVA_FORMAT_RGB8, &dst->sprite_empty)) != NV_SUCCESS)
  {
    return code;
  }

  if ((code = nv_camera_init(driver, &camera)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = ctext_init(dst)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = nv_vk_bake_global_pipelines(nvsmctx, dst)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = nv_renderer_prepare_quad_renderer(dst)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  return NV_ERROR_SUCCESS;
}

static inline nv_error
nvvk_renderer_resize(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  vkDeviceWaitIdle(rd->vkctx->device);

  rd->will_render_this_frame = false;

  // Clean up old synchronization objects
  for (size_t i = 0; i < nv_list_size(&rd->main_pass.per_image_data); i++)
  {
    nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    if (image_frame == NULL)
    {
      continue;
    }
    vkDestroySemaphore(rd->vkctx->device, image_frame->render_finish_semaphore, &rd->vkctx->vkalloc);
    image_frame->render_finish_semaphore = VK_NULL_HANDLE;
  }

  for (size_t i = 0; i < nv_list_size(&rd->main_pass.per_frame_data); i++)
  {
    nv_rdr_per_frame_data_t* frame = (nv_rdr_per_frame_data_t*)nv_list_get(&rd->main_pass.per_frame_data, i);
    if (frame == NULL)
    {
      continue;
    }
    vkDestroyFence(rd->vkctx->device, frame->in_flight_fence, &rd->vkctx->vkalloc);
    vkDestroySemaphore(rd->vkctx->device, frame->image_available_semaphore, &rd->vkctx->vkalloc);

    frame->in_flight_fence           = VK_NULL_HANDLE;
    frame->image_available_semaphore = VK_NULL_HANDLE;
  }

  // Clean up images and framebuffers
  // iris_memory_free(&rd->main_pass.depth_image_memory);
  if (rd->main_pass.color_image.handle != VK_NULL_HANDLE)
  {
    iris_texture_destroy(&rd->main_pass.color_image);
  }
  for (size_t i = 0; i < nv_list_size(&rd->main_pass.render_data); i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->main_pass.render_data, i);
    if (data == NULL)
    {
      continue;
    }
    if (data->depth_image.handle != VK_NULL_HANDLE)
    {
      iris_texture_destroy(&data->depth_image);
    }
    if (data->swapchain_image != VK_NULL_HANDLE)
    {
      vkDestroyImageView(rd->vkctx->device, data->swapchain_image_view, &rd->vkctx->vkalloc);
    }
    if (data->color_framebuffer.handle != VK_NULL_HANDLE)
    {
      iris_destroy_framebuffer(rd->vkctx, &data->color_framebuffer);
    }
  }

  // Get new swapchain image count
  rd->swapchain.image_count = nv_vk_get_surface_image_count(rd->vkctx, rd->vkctx->phys_device, rd->vkctx->surface);
  int w, h;
  SDL_GetWindowSizeInPixels(rd->ctx->window, &w, &h);
  VkSurfaceCapabilitiesKHR surface_capabilities;
  nvvk_result_check(*rd->vkctx, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rd->vkctx->phys_device, rd->vkctx->surface, &surface_capabilities));
  const u32 min_width  = surface_capabilities.minImageExtent.width;
  const u32 min_height = surface_capabilities.minImageExtent.height;
  const u32 max_width  = surface_capabilities.maxImageExtent.width;
  const u32 max_height = surface_capabilities.maxImageExtent.height;
  w                    = NVM_CLAMP((u32)w, min_width, max_width);
  h                    = NVM_CLAMP((u32)h, min_height, max_height);
  rd->render_extent    = (nv_extent2d){ (size_t)w, (size_t)h };
  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_INVALID_RETVAL);

  VkSwapchainKHR   old_swapchain = rd->swapchain.handle;
  VkPresentModeKHR present_mode  = VK_PRESENT_MODE_FIFO_KHR;
  if ((rd->flags & NOVA_RENDERER_VSYNC_ENABLE) != 0)
  {
    switch (rd->buffer_mode)
    {
      case NOVA_BUFFER_MODE_TRIPLE_BUFFERED: present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR; break;
      case NOVA_BUFFER_MODE_SINGLE_BUFFERED:
      case NOVA_BUFFER_MODE_DOUBLE_BUFFERED:
      default: present_mode = VK_PRESENT_MODE_FIFO_KHR; break;
    };
  }
  else
  {
    present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  }

  iris_swapchain_create_info swapchain_create_info = nv_zero_init(iris_swapchain_create_info);
  swapchain_create_info.extent.width               = rd->render_extent.width;
  swapchain_create_info.extent.height              = rd->render_extent.height;
  swapchain_create_info.present_mode               = present_mode;
  swapchain_create_info.format                     = rd->swapchain.image_format;
  swapchain_create_info.color_space                = (VkColorSpaceKHR)rd->swapchain.color_space;
  swapchain_create_info.image_count                = rd->swapchain.image_count;
  swapchain_create_info.old_swapchain              = old_swapchain;
  iris_create_swapchain(rd->vkctx, &swapchain_create_info, &rd->swapchain.handle);
  nv_assert_else_return(rd->swapchain.handle != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  if (old_swapchain != VK_NULL_HANDLE)
  {
    vkDestroySwapchainKHR(rd->vkctx->device, old_swapchain, &rd->vkctx->vkalloc);
  }

  // Clear and recreate synchronization objects
  nv_list_clear(&rd->main_pass.per_frame_data);
  nv_list_clear(&rd->main_pass.per_image_data);

  nv_list_resize(&rd->main_pass.per_frame_data, nv_renderer_get_frames_in_flight(rd));
  nv_list_resize(&rd->main_pass.per_image_data, rd->swapchain.image_count);

  const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
  const VkFenceCreateInfo     fenceCreateInfo     = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

  // Create per-image data (one per frame in flight)
  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    nv_rdr_per_image_data_t* frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    nv_assert_else_return(frame != NULL, NV_ERROR_BROKEN_STATE);

    vkCreateSemaphore(rd->vkctx->device, &semaphoreCreateInfo, &rd->vkctx->vkalloc, &frame->render_finish_semaphore);
  }

  size_t const frames_in_flight = nv_renderer_get_frames_in_flight(rd);
  for (size_t i = 0; i < frames_in_flight; i++)
  {
    nv_rdr_per_frame_data_t* frame = (nv_rdr_per_frame_data_t*)nv_list_get(&rd->main_pass.per_frame_data, i);
    nv_assert_else_return(frame != NULL, NV_ERROR_BROKEN_STATE);

    vkCreateSemaphore(rd->vkctx->device, &semaphoreCreateInfo, &rd->vkctx->vkalloc, &frame->image_available_semaphore);
    vkCreateFence(rd->vkctx->device, &fenceCreateInfo, &rd->vkctx->vkalloc, &frame->in_flight_fence);
  }

  VkCommandBuffer             buffer[32];
  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = rd->swapchain.image_count;
  cmdAllocInfo.commandPool                 = rd->command_pool;
  VkResult const allocResult               = vkAllocateCommandBuffers(rd->vkctx->device, &cmdAllocInfo, buffer);
  if (allocResult != VK_SUCCESS)
  {
    nv_log_error("Failed to allocate command buffers: %d\n", allocResult);
    return NV_ERROR_INVALID_RETVAL;
  }

  for (size_t i = 0; i < rd->swapchain.image_count; i++)
  {
    nv_rdr_per_image_data_t* frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, i);
    frame->cmd                     = buffer[i];
  }

  nv_error code = NV_ERROR_SUCCESS;
  if ((code = create_optional_images(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }
  if ((code = create_framebuffers_and_swapchain_image_views(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  _nv_ctx_reset_frame_buffer_resized(rd->ctx);
  // Reset frame counters to avoid out-of-bounds access
  rd->frame_index = 0;
  rd->image_index = 0;
  return NV_ERROR_SUCCESS;
}

bool
nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_list_is_valid(&rd->main_pass.render_data), NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_BROKEN_STATE);

  nv_camera_update(&camera, rd);

  rd->will_render_this_frame = false;

  // Get current frame data for frames in flight
  // nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, rd->frame);
  nv_rdr_per_frame_data_t* frame = (nv_rdr_per_frame_data_t*)nv_list_get(&rd->main_pass.per_frame_data, rd->frame_index);

  // Check if we need to resize
  bool const needs_resize = frame == NULL || frame->in_flight_fence == VK_NULL_HANDLE;
  if (needs_resize)
  {
    nvvk_renderer_resize(rd);
    return nv_renderer_begin(rd, clear_color);
  }

  // Wait for the previous frame using this frame's fence to finish
  VkResult const waitResult = vkWaitForFences(rd->vkctx->device, 1, &frame->in_flight_fence, VK_TRUE, UINT64_MAX);
  if (waitResult != VK_SUCCESS)
  {
    nv_log_error("Failed to wait for fence: %d\n", waitResult);
    return false;
  }

  // Reset the fence for the current frame
  vkResetFences(rd->vkctx->device, 1, &frame->in_flight_fence);

  // Get next image index
  VkResult const image_acquire_res =
      vkAcquireNextImageKHR(rd->vkctx->device, rd->swapchain.handle, UINT64_MAX, frame->image_available_semaphore, VK_NULL_HANDLE, &rd->image_index);

  if (image_acquire_res == VK_ERROR_OUT_OF_DATE_KHR || image_acquire_res == VK_SUBOPTIMAL_KHR || nv_ctx_get_frame_buffer_resized(rd->ctx))
  {
    nvvk_renderer_resize(rd);
    return false;
  }
  else if (image_acquire_res != VK_SUCCESS)
  {
    nv_log_error("Failed to acquire image from swapchain: %d\n", image_acquire_res);
    return false;
  }

  nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, rd->image_index);
  if (image_frame == NULL)
  {
    nvvk_renderer_resize(rd);
    return nv_renderer_begin(rd, clear_color);
  }

  VkCommandBuffer cmd = image_frame->cmd;
  nv_assert_else_return(cmd != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  // Reset command buffer
  VkResult const resetResult = vkResetCommandBuffer(cmd, 0);
  if (resetResult != VK_SUCCESS)
  {
    nv_log_error("Failed to reset command buffer: %d\n", resetResult);
    return false;
  }

  nv_renderer_frame_render_info* image = (nv_renderer_frame_render_info*)nv_list_get(&rd->main_pass.render_data, rd->image_index);
  if (image == NULL)
  {
    nvvk_renderer_resize(rd);
    return nv_renderer_begin(rd, clear_color);
  }

  VkFramebuffer framebuffer = image->color_framebuffer.handle;
  nv_assert_else_return(framebuffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  VkClearValue clear_values[2];
  clear_values[0].color        = (VkClearColorValue){ { (float)clear_color.x, (float)clear_color.y, (float)clear_color.z, (float)clear_color.w } };
  clear_values[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };

  VkRect2D const render_extent = { .offset = { 0, 0 }, .extent = { (uint32_t)rd->render_extent.width, (uint32_t)rd->render_extent.height } };

  VkRenderPassBeginInfo const renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass      = rd->render_pass,
    .framebuffer     = framebuffer,
    .renderArea      = render_extent,
    .clearValueCount = 2,
    .pClearValues    = clear_values,
  };

  const VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };

  VkResult const beginResult = vkBeginCommandBuffer(cmd, &beginInfo);
  if (beginResult != VK_SUCCESS)
  {
    nv_log_error("Failed to begin command buffer: %d\n", beginResult);
    return false;
  }

  vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport const viewport = {
    .x        = 0.0f,
    .y        = 0.0f,
    .width    = (float)rd->render_extent.width,
    .height   = (float)rd->render_extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D const scissor = {
    .offset = { 0, 0 },
    .extent = { (uint32_t)rd->render_extent.width, (uint32_t)rd->render_extent.height },
  };

  vkCmdSetScissor(cmd, 0, 1, &scissor);

  // We're prepared to render this frame
  rd->will_render_this_frame = true;

  return true;
}

nv_error
nv_renderer_end(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_list_is_valid(&rd->main_pass.render_data), NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_BROKEN_STATE);

  // Get the frame data for the acquired image
  nv_rdr_per_frame_data_t* frame       = (nv_rdr_per_frame_data_t*)nv_list_get(&rd->main_pass.per_frame_data, rd->frame_index);
  nv_rdr_per_image_data_t* image_frame = (nv_rdr_per_image_data_t*)nv_list_get(&rd->main_pass.per_image_data, rd->image_index);
  if (frame == NULL)
  {
    return NV_ERROR_BROKEN_STATE;
  }

  const VkCommandBuffer cmd = image_frame->cmd;
  nv_assert_else_return(cmd != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  if (rd->will_render_this_frame)
  {
    if (rd->ctext != NULL)
    {
      ctext_flush_renders(rd);
    }

    // nvui internally checks whether it has been initialized or not
    nvui_render(rd);
  }

  nv_renderer_flush_renders(rd);

  vkCmdEndRenderPass(cmd);

  VkResult const endResult = vkEndCommandBuffer(cmd);
  if (endResult != VK_SUCCESS)
  {
    nv_log_error("Failed to end command buffer: %d\n", endResult);
    return NV_ERROR_BROKEN_STATE;
  }

  VkSemaphore       render_finish_semaphore = image_frame->render_finish_semaphore;
  const VkSemaphore wait_semaphores[]       = { frame->image_available_semaphore };
  const VkSemaphore signal_semaphores[]     = { render_finish_semaphore };

  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

  VkSubmitInfo const submit_info = {
    .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount   = 1,
    .pWaitSemaphores      = wait_semaphores,
    .pWaitDstStageMask    = waitStages,
    .commandBufferCount   = 1,
    .pCommandBuffers      = &cmd,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores    = signal_semaphores,
  };

  VkResult const submitResult = vkQueueSubmit(rd->vkctx->graphics_queue, 1, &submit_info, frame->in_flight_fence);
  if (submitResult != VK_SUCCESS)
  {
    nv_log_error("Failed to submit command buffer: %d\n", submitResult);
    return NV_ERROR_BROKEN_STATE;
  }

  VkPresentInfoKHR const present_info = {
    .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores    = signal_semaphores,
    .swapchainCount     = 1,
    .pSwapchains        = &rd->swapchain.handle,
    .pImageIndices      = &rd->image_index,
  };

  VkResult const presentResult = vkQueuePresentKHR(rd->vkctx->present_queue, &present_info);
  if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || nv_ctx_get_frame_buffer_resized(rd->ctx))
  {
    nv_error code = NV_ERROR_SUCCESS;
    if ((code = nvvk_renderer_resize(rd)) != NV_ERROR_SUCCESS)
    {
      return code;
    }
  }
  else if (presentResult != VK_SUCCESS)
  {
    nv_log_error("Failed to present swapchain image: %d\n", presentResult);
    return NV_ERROR_BROKEN_STATE;
  }

  rd->frame_index = (rd->frame_index + 1) % nv_renderer_get_frames_in_flight(rd);

  // For safety's sake, set will_render_this_frame to 0
  rd->will_render_this_frame = false;

  return NV_ERROR_SUCCESS;
}
