#include "GPU/vk.h"

#include "GPU/buffer.h"
#include "GPU/driver.h"
#include "GPU/fbf.h"
#include "GPU/memory.h"
#include "GPU/pipeline.h"
#include "GPU/sampler.h"
#include "GPU/texture.h"
#include "GPU/types.h"
#include "GPU/vk.h"

#include "std/format.h"
#include "std/image.h"

#include "std/containers/atlas.h"
#include "std/containers/hashmap.h"
#include "std/containers/list.h"

#include "engine/camera.h"
#include "engine/ctext.h"
#include "engine/engine.h"
#include "engine/fontc.h"
#include "engine/input.h"
#include "engine/nvsm.h"
#include "engine/renderer.h"
#include "engine/sprite.h"
#include "engine/sprite_renderer.h"
#include "engine/ui.h"

#include "std/alloc.h"
#include "std/errorcodes.h"
#include "std/math/math.h"
#include "std/stdafx.h"
#include "std/string.h"

#include <SDL2/SDL_vulkan.h>
#include <stdint.h>

#define CGLTF_IMPLEMENTATION
#include "external/cgltf/cgltf.h"

#define HAS_FLAG(flag) ((nvvkctx->flag_register & flag) || (flags & flag))
#define STR(s) #s

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
  return (nv_extent2d){ ww, wh };
}

VKAPI_ATTR VkBool32 VKAPI_CALL nvvk_debug_messenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData);

typedef struct nv_quad_vertex_t     nv_quad_vertex_t;
typedef struct nv_line_vertex_t     nv_line_vertex_t;
typedef struct ctext_glyph_vertex_t ctext_glyph_vertex_t;

struct nv_quad_vertex_t
{
  vec3f position;
  vec2f tex_coords;
  vec4f color;
};

struct nv_line_vertex_t
{
  vec2 position;
};

static nv_quad_vertex_t quad_vertices[4];

static const uint32_t quad_indices[] = { 0, 1, 2, 0, 2, 3 };

// ctext
struct ctext_glyph_vertex_t
{
  vec3f pos;
  vec2f uv;
};

struct ctext_drawcall_t
{
  mat4f                 model;
  size_t                vertex_count;
  size_t                index_count;
  size_t                index_offset;
  ctext_glyph_vertex_t* vertices;
  u32*                  indices;
  vec4f                 color;
  flt_t                 scale;
};

struct ctext_label_t
{
  ctext_hori_align h_align;
  ctext_vert_align v_align;
  flt_t            scale;
  int              index;
  char*            text;
  cfont_t*         fnt;
  nv_object*       obj;
};

struct push_constants
{
  mat4f model;
  vec4f color;
  vec4f outline_color;
  flt_t scale;
};

u32
nv_renderer_get_frame(const nv_renderer_t* rd)
{
  return rd->frame;
}

VkCommandBuffer
nv_renderer_get_draw_buffer(const nv_renderer_t* rd)
{
  VkCommandBuffer* access = (VkCommandBuffer*)nv_list_get(&rd->draw_cmd_buffers, rd->frame);
  if (access)
  {
    return *access;
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
nv_renderer_get_max_frames_in_flight(const nv_renderer_t* rd)
{
  return 1U + (u32)rd->buffer_mode;
}

#define ABSF(x) ((x >= 0.0f) ? (x) : -(x))

// static inline bool
// nv_is_quad_visible_through_default_camera(const vec3* pos, const vec3* siz)
// {
//   const flt_t half_width  = siz->x * 0.5f;
//   const flt_t half_height = siz->y * 0.5f;
//   const flt_t deltax      = pos->x - camera.position.x;
//   const flt_t deltay      = pos->y - camera.position.y;
//   const flt_t dx          = ABSF(deltax); // delta x & y
//   const flt_t dy          = ABSF(deltay); //

//   return (dx <= (half_width + camera.ortho_size.x) && dy <= (half_height + camera.ortho_size.y));
// }

void
nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite_t* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer)
{
  if (!spr)
  {
    spr = &rd->sprite_empty;
  }

  nv_draw_call_t drawcall = (nv_draw_call_t){ .type     = NOVA_DRAWCALL_QUAD,
                              .layer    = layer,
                              .drawcall = { .quad = { .spr = spr, .tex_multiplier = tex_coord_multiplier, .pos = position, .siz = size, .col = color, }, }, };
  nv_list_push_back(&rd->drawcalls, &drawcall);
}

void
nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer)
{
  nv_draw_call_t drawcall = (nv_draw_call_t){ .type = NOVA_DRAWCALL_LINE, .layer = layer, .drawcall = { .line = { .begin = start, .end = end, .col = color, }, }, };
  nv_list_push_back(&rd->drawcalls, &drawcall);
}

// thjs will just sort the array from small layer to big layer :> and try to group the types together
static inline int
__drawcall_compar(const void* obj1, const void* obj2)
{
  const nv_draw_call_t* call1 = obj1;
  const nv_draw_call_t* call2 = obj2;
  return ((int)call1->layer - (int)call2->layer) + ((int)call1->type - (int)call2->type);
}

static inline void
_nv_renderer_flush_renders(nv_renderer_t* rd)
{
  const uint32_t camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);

  const VkCommandBuffer cmd = nv_renderer_get_draw_buffer(rd);
  nv_assert_else_return(cmd != VK_NULL_HANDLE, );

  const VkDescriptorSet camera_set = camera.sets->set;
  nv_assert_else_return(camera_set != VK_NULL_HANDLE, );

  bool bound_quad_state = 0;

  nv_list_sort(&rd->drawcalls, __drawcall_compar);

  nv_draw_call_type state = NOVA_DRAWCALL_INVALID;

  for (size_t i = 0; i < nv_list_size(&rd->drawcalls); i++)
  {
    const nv_draw_call_t* drawcall = &((nv_draw_call_t*)nv_list_data(&rd->drawcalls))[i];
    nv_assert_and_exec(drawcall != NULL, { continue; })

        if (drawcall->type == NOVA_DRAWCALL_QUAD)
    {
      // if (!nv_Quad_Visible(&drawcall->drawcall.quad.pos,
      // &drawcall->drawcall.quad.siz)) {
      //     continue;
      // }

      if (state != NOVA_DRAWCALL_QUAD)
      {
        VkDeviceSize offsets = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.unlit.pipeline);

        if (!bound_quad_state)
        {
          vkCmdBindVertexBuffers(cmd, 0, 1, &rd->quad_vb.buffer, &offsets);
          vkCmdBindIndexBuffer(cmd, rd->quad_vb.buffer, sizeof(quad_vertices), VK_INDEX_TYPE_UINT32);
        }

        state            = NOVA_DRAWCALL_QUAD;
        bound_quad_state = 1;
      }

      struct push_constants
      {
        mat4f model;
        vec4f color;
        vec2f tex_multiplier; // Multiplied with the tex coords
      } pc;

      mat4f scale       = m4fscale(m4finit(1.0f), v3fmuls(drawcall->drawcall.quad.siz, 2.0f));
      mat4f rotate      = m4finit(1.0f);
      mat4f translate   = m4ftranslate(m4finit(1.0f), drawcall->drawcall.quad.pos);
      pc.model          = m4fmul(translate, m4fmul(rotate, scale));
      pc.color          = drawcall->drawcall.quad.col;
      pc.tex_multiplier = drawcall->drawcall.quad.tex_multiplier;
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
        vec2f line_begin;
        vec2f line_end;
      } pc;
      pc.model      = m4finit(1.0f);
      pc.color      = drawcall->drawcall.line.col;
      pc.line_begin = (vec2f){ drawcall->drawcall.line.begin.x, drawcall->drawcall.line.begin.y };
      pc.line_end   = (vec2f){ drawcall->drawcall.line.end.x, drawcall->drawcall.line.end.y };
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
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);

  void* memcpy_ret = nv_memcpy(
      quad_vertices,
      (const nv_quad_vertex_t[4]){
          (const nv_quad_vertex_t){ .position = (vec3f){ +0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 0.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ -0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 0.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ -0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 1.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } },
          (const nv_quad_vertex_t){ .position = (vec3f){ +0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 1.0f }, .color = (vec4f){ 1.0F, 1.0F, 1.0F, 1.0F } } },
      sizeof(quad_vertices));
  nv_assert_else_return(memcpy_ret != NULL, NV_ERROR_EXTERNAL);

  nv_error code =
      nv_gpu_buffer_init(rd->driver, sizeof(quad_vertices) + sizeof(quad_indices), 1, NV_GPU_BUFFER_VERTEX_BUFFER_BIT | NV_GPU_BUFFER_INDEX_BUFFER_BIT, &rd->quad_vb);
  nv_assert_else_return(code == NV_SUCCESS, code);
  nv_assert_else_return(rd->quad_vb.buffer != VK_NULL_HANDLE, NV_ERROR_INVALID_ARG);

  nv_gpu_allocate_memory(
      rd->nvvkctx, sizeof(quad_vertices) + sizeof(quad_indices), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &rd->quad_memory);
  nv_assert_else_return(rd->quad_memory.memory != VK_NULL_HANDLE, NV_ERROR_INVALID_ARG);

  // nv_gpu_bind_buffer_to_memory(rd->nvvkctx, &rd->quad_memory, 0, &rd->quad_vb);

  void* data = nv_malloc(sizeof(quad_vertices) + sizeof(quad_indices));
  nv_assert_else_return(data != NULL, NV_ERROR_MALLOC_FAILED);

  nv_memcpy(data, quad_vertices, sizeof(quad_vertices));
  nv_memcpy((char*)data + sizeof(quad_vertices), quad_indices, sizeof(quad_indices));

  nv_assert_else_return(rd->quad_vb.buffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  // nv_gpu_write_to_buffer(rd->nvvkctx, &rd->quad_vb, sizeof(quad_vertices) + sizeof(quad_indices), data, 0);
  nv_gpu_buffer_write_data(&rd->quad_vb, data, sizeof(quad_vertices) + sizeof(quad_indices), 0);

  nv_free(data);

  return NV_ERROR_SUCCESS;
}

void
nv_renderer_destroy(nv_renderer_t* rd)
{
  if (!rd)
  {
    return;
  }

  vkDeviceWaitIdle(rd->nvvkctx->device);

  // we were only making frames_in_flight fences and it was working for some
  // reason!! that was the reason we were getting errors!
  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);

    // as the view was silently smushed into the
    // structure, we just kinda smush it out as well.
    vkDestroyImageView(rd->nvvkctx->device, nv_gpu_texture_get_view(&data->sc_image), &rd->nvvkctx->vkalloc);

    nv_gpu_destroy_texture(rd->nvvkctx, &data->depth_image);

    nv_gpu_destroy_framebuffer(rd->nvvkctx, &data->color_framebuffer);

    vkDestroySemaphore(rd->nvvkctx->device, data->image_available_semaphore, &rd->nvvkctx->vkalloc);
    vkDestroySemaphore(rd->nvvkctx->device, data->render_finish_semaphore, &rd->nvvkctx->vkalloc);
    vkDestroyFence(rd->nvvkctx->device, data->in_flight_fence, &rd->nvvkctx->vkalloc);
  }

  nv_sprite_destroy(rd->nvvkctx, &rd->sprite_empty);

  nv_camera_destroy(rd->nvvkctx, &camera);

  nv_vk_destroy_global_pipelines(rd->nvvkctx);
  nv_descriptor_pool_destroy(rd->nvvkctx, &g_pool);

  ctext_shutdown(rd);

  nv_list_destroy(&rd->drawcalls);
  nv_list_destroy(&rd->render_data);

  nv_gpu_free_memory(rd->nvvkctx, &rd->depth_image_memory);

  nv_gpu_buffer_destroy(&rd->quad_vb);
  nv_gpu_free_memory(rd->nvvkctx, &rd->quad_memory);

  vkFreeCommandBuffers(rd->nvvkctx->device, rd->command_pool, nv_list_size(&rd->draw_cmd_buffers), (VkCommandBuffer*)nv_list_data(&rd->draw_cmd_buffers));
  vkDestroyCommandPool(rd->nvvkctx->device, rd->command_pool, &rd->nvvkctx->vkalloc);

  vkFreeCommandBuffers(rd->nvvkctx->device, rd->nvvkctx->cmd_pool, NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT, rd->nvvkctx->cmd_buffers);
  vkDestroyCommandPool(rd->nvvkctx->device, rd->nvvkctx->cmd_pool, &rd->nvvkctx->vkalloc);
  nv_list_destroy(&rd->draw_cmd_buffers);

  vkDestroySwapchainKHR(rd->nvvkctx->device, rd->swapchain, &rd->nvvkctx->vkalloc);

  if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    nv_gpu_destroy_texture(rd->nvvkctx, &rd->color_image);
    nv_gpu_free_memory(rd->nvvkctx, &rd->color_image_memory);
  }
  vkDestroyRenderPass(rd->nvvkctx->device, rd->render_pass, &rd->nvvkctx->vkalloc);

  nv_bzero(rd, sizeof(nv_renderer_t));
}

static inline nv_error
create_optional_images(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_MALLOC_FAILED);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->swapchain != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&rd->render_data), NV_ERROR_MALLOC_FAILED);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_BROKEN_STATE);

  if (nvvk_result_check(*rd->nvvkctx, vkGetSwapchainImagesKHR(rd->nvvkctx->device, rd->swapchain, &rd->swap_chain_image_count, NULL)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  VkImage* swapchainImages = (VkImage*)nv_malloc(rd->swap_chain_image_count * sizeof(VkImage));
  nv_assert_else_return(swapchainImages != NULL, NV_ERROR_MALLOC_FAILED);

  if (nvvk_result_check(*rd->nvvkctx, vkGetSwapchainImagesKHR(rd->nvvkctx->device, rd->swapchain, &rd->swap_chain_image_count, swapchainImages)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_list_resize(&rd->render_data, rd->swap_chain_image_count);

  if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    size_t color_image_size = 0;
    nv_vk_create_texture_empty(
        rd->nvvkctx,
        rd->render_extent.width,
        rd->render_extent.height,
        rd->swap_chain_image_format,
        rd->samples,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        &color_image_size,
        &rd->color_image.image,
        NULL);

    nv_gpu_allocate_memory(rd->nvvkctx, color_image_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &rd->color_image_memory);
    nv_gpu_bind_texture_to_memory(rd->nvvkctx, &rd->color_image_memory, 0, &rd->color_image);
  }

  // attachment vector will be like <color resolve, depth attachment, swapchain
  // image>
  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);

    data->sc_image.image = swapchainImages[i];
    nv_assert_else_return(swapchainImages[i] != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

    const nv_gpu_texture_create_info image_info = {
      .format      = NOVA_FORMAT_D32,
      .samples     = rd->samples,
      .type        = VK_IMAGE_TYPE_2D,
      .usage       = NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE,
      .extent      = (nv_extent3D){ .width = rd->render_extent.width, .height = rd->render_extent.height, .depth = 1 },
      .arraylayers = 1,
      .miplevels   = 1,
    };
    nv_gpu_create_texture(rd->nvvkctx, &image_info, &data->depth_image);
    nv_assert_else_return(data->depth_image.image != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

    if (i == 0)
    {
      VkMemoryRequirements memReqs = nv_zero_init(VkMemoryRequirements);
      vkGetImageMemoryRequirements(rd->nvvkctx->device, nv_gpu_texture_get(&data->depth_image), &memReqs);

      rd->shadow_image_size = memReqs.size;
      nv_assert_else_return(rd->shadow_image_size != 0, NV_ERROR_INVALID_RETVAL);

      nv_gpu_allocate_memory(rd->nvvkctx, rd->shadow_image_size * rd->swap_chain_image_count, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &rd->depth_image_memory);
      nv_assert_else_return(rd->depth_image_memory.memory != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
    }

    nv_gpu_bind_texture_to_memory(rd->nvvkctx, &rd->depth_image_memory, i * rd->shadow_image_size, &data->depth_image);
  }

  nv_free(swapchainImages);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
create_framebuffers_and_swapchain_image_views(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_pass != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_INVALID_ARG);

  nv_list_t attachments;
  nv_error  code = NV_ERROR_SUCCESS;
  if ((code = nv_list_init(sizeof(nv_gpu_texture*), 3, nv_allocator_c, NULL, &attachments)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);
    nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);

    nv_assert_else_return(data->sc_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(rd->swap_chain_image_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);

    // we don't know anything about the swapchain_image, as it's a swapchain
    // image so we have to manually create the image view;
    // we will later smush in the view using nv_gpu_texture_attach_view
    VkImageViewCreateInfo imageViewCreateInfo           = nv_zero_init(VkImageViewCreateInfo);
    imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image                           = nv_gpu_texture_get(&data->sc_image);
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = nv_format_to_vk_format(rd->swap_chain_image_format);
    imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;
    VkImageView vioew;
    if (nvvk_result_check(*rd->nvvkctx, vkCreateImageView(rd->nvvkctx->device, &imageViewCreateInfo, &rd->nvvkctx->vkalloc, &vioew)) != VK_SUCCESS)
    {
      nv_list_destroy(&attachments);
      return NV_ERROR_EXTERNAL;
    }

    nv_gpu_texture_attach_view(&data->sc_image, vioew);

    nv_list_clear(&attachments);
    if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
    {
      nv_assert_else_return(rd->color_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
      nv_list_push_set(&attachments, (nv_gpu_texture*[]){ &rd->color_image, &data->depth_image, &data->sc_image }, 3);
    }
    else
    {
      nv_assert_else_return(data->depth_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
      nv_list_push_set(&attachments, (nv_gpu_texture*[]){ &data->sc_image, &data->depth_image }, 2);
    }

    nv_gpu_framebuffer_create_info_t framebuffer_info = nv_zero_init(nv_gpu_framebuffer_create_info_t);
    framebuffer_info.attachments                      = (nv_gpu_texture**)nv_list_data(&attachments);
    framebuffer_info.num_attachments                  = nv_list_size(&attachments);
    framebuffer_info.extent                           = rd->render_extent;
    framebuffer_info.num_layers                       = 1;
    framebuffer_info.pass                             = rd->render_pass;

    nv_assert_else_return(nv_list_data(&attachments) != NULL, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(nv_list_size(&attachments) != 0, NV_ERROR_BROKEN_STATE);

    if (nv_gpu_create_framebuffer(rd->nvvkctx, &framebuffer_info, &data->color_framebuffer) != 0)
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
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);
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

  nv_assert_else_return(rd->swap_chain_image_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->swap_chain_image_count != 0, NV_ERROR_BROKEN_STATE);

  nv_gpu_swapchain_create_info surface_create_info = nv_zero_init(nv_gpu_swapchain_create_info);
  surface_create_info.extent.width                 = rd->render_extent.width;
  surface_create_info.extent.height                = rd->render_extent.height;
  surface_create_info.present_mode                 = present_mode;
  surface_create_info.format                       = rd->swap_chain_image_format;
  surface_create_info.color_space                  = rd->swap_chain_color_space;
  surface_create_info.image_count                  = rd->swap_chain_image_count;

  nv_gpu_create_swapchain(rd->nvvkctx, &surface_create_info, &rd->swapchain);
  nv_assert_else_return(rd->swapchain != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  VkSampleCountFlagBits conf_samples;
  if (conf->samples == NOVA_SAMPLE_COUNT_MAX_SUPPORTED)
  {
    conf_samples = rd->nvvkctx->max_samples;
  }
  else
  {
    conf_samples = (VkSampleCountFlagBits)conf->samples;
  }
  const VkSampleCountFlagBits _samples = conf->multisampling_enable ? conf_samples : VK_SAMPLE_COUNT_1_BIT;
  rd->samples                          = _samples;

  if (conf->multisampling_enable)
  {
    rd->nvvkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING;
    rd->attachment_count++;
  }
  rd->nvvkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK;
  rd->nvvkctx->flag_register |= NVVK_PIPELINE_FLAGS_FORCE_CULLING;
  rd->attachment_count++;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = rd->nvvkctx->graphics_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  nv_error code = NV_ERROR_SUCCESS;
  if (nvvk_result_check(*rd->nvvkctx, vkCreateCommandPool(rd->nvvkctx->device, &cmdPoolCreateInfo, &rd->nvvkctx->vkalloc, &rd->command_pool)) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  const size_t frames_in_flight = 1 + (size_t)conf->buffer_mode;

  nv_renderer_frame_render_info data = nv_zero_init(nv_renderer_frame_render_info);
  for (size_t i = 0; i < frames_in_flight; i++)
  {
    nv_list_push_back(&rd->draw_cmd_buffers, &data);
    nv_list_push_back(&rd->render_data, &data);
  }

  nv_assert_else_return(rd->command_pool != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = frames_in_flight;
  cmdAllocInfo.commandPool                 = rd->command_pool;
  if (nvvk_result_check(*rd->nvvkctx, vkAllocateCommandBuffers(rd->nvvkctx->device, &cmdAllocInfo, (VkCommandBuffer*)nv_list_data(&rd->draw_cmd_buffers))) != VK_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  rd->depth_buffer_format = nv_format_to_vk_format(NOVA_FORMAT_D32); // replace (probably)
  nv_assert_else_return(rd->depth_buffer_format != VK_FORMAT_UNDEFINED, NV_ERROR_INVALID_RETVAL);

  nv_gpu_render_pass_create_info rpi = nv_zero_init(nv_gpu_render_pass_create_info);

  rpi.format              = rd->swap_chain_image_format;
  rpi.depth_buffer_format = nv_vk_format_to_nv_format(rd->depth_buffer_format);
  nv_assert_else_return(rpi.format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rpi.depth_buffer_format != NOVA_FORMAT_UNDEFINED, NV_ERROR_BROKEN_STATE);

  rpi.subpass = 0;
  rpi.samples = rd->samples;
  nv_gpu_create_render_pass(rd->nvvkctx, &rpi, &rd->render_pass, rd->nvvkctx->flag_register);

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
nv_renderer_init(struct nv_ctx_s* ctx, struct nvsm_ctx_t* nvsmctx, struct nvvk_driver_t* driver, const nv_renderer_config* conf, nv_renderer_t* dst)
{
  nv_assert_else_return(conf != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->initial_window_size.width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->initial_window_size.height != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(conf->samples != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvsmctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_driver_is_valid(driver) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->ctx) == true, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_renderer_t));

  dst->ctx     = ctx;
  dst->driver  = driver;
  dst->nvvkctx = driver->ctx;

  if (conf->multisampling_enable == 1)
  {
    nv_assert_else_return(conf->samples != NOVA_SAMPLE_COUNT_1_SAMPLES, NV_ERROR_INVALID_ARG);
  }

  // how many frames the renderer will render at once
  int frames_in_flight = (1 + (int)conf->buffer_mode);
  if (frames_in_flight <= 0)
  {
    frames_in_flight = 1;
  }

  nv_error code = NV_ERROR_SUCCESS;

  code = nv_list_init(sizeof(nv_draw_call_t), 4, nv_allocator_c, NULL, &dst->drawcalls);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  code = nv_list_init(sizeof(VkCommandBuffer), frames_in_flight, nv_allocator_c, NULL, &dst->draw_cmd_buffers);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  code = nv_list_init(sizeof(nv_renderer_frame_render_info), frames_in_flight, nv_allocator_c, NULL, &dst->render_data);
  nv_assert_else_return(code == NV_ERROR_SUCCESS, code);

  nv_assert_else_return(nv_list_is_valid(&dst->drawcalls), NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&dst->draw_cmd_buffers), NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&dst->render_data), NV_ERROR_BROKEN_STATE);

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

  if (!nv_vk_get_supported_format(dst->nvvkctx, dst->nvvkctx->phys_device, dst->nvvkctx->surface, &dst->swap_chain_image_format, &dst->swap_chain_color_space))
  {
    nv_log_and_abort("No supported format for display.\n");
  }

  dst->swap_chain_image_count = nv_vk_get_surface_image_count(dst->nvvkctx, dst->nvvkctx->phys_device, dst->nvvkctx->surface);
  nv_assert_else_return(dst->swap_chain_image_count > 0, NV_ERROR_INVALID_RETVAL);

  if ((code = nv_renderer_initialize_rendering_components(dst, conf)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  for (size_t i = 0; i < dst->swap_chain_image_count; i++)
  {
    const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

    const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&dst->render_data, i);
    nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);

    nvvk_result_check(*dst->nvvkctx, vkCreateSemaphore(dst->nvvkctx->device, &semaphoreCreateInfo, &driver->ctx->vkalloc, &data->render_finish_semaphore));
    nvvk_result_check(*dst->nvvkctx, vkCreateSemaphore(dst->nvvkctx->device, &semaphoreCreateInfo, &driver->ctx->vkalloc, &data->image_available_semaphore));
    nvvk_result_check(*dst->nvvkctx, vkCreateFence(dst->nvvkctx->device, &fenceCreateInfo, &driver->ctx->vkalloc, &data->in_flight_fence));

    nv_assert_else_return(data->render_finish_semaphore != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return(data->image_available_semaphore != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return(data->in_flight_fence != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
  }

  if ((code = nv_descriptor_pool_init(dst->nvvkctx, &g_pool)) != NV_ERROR_SUCCESS)
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
_nvvk_renderer_resize(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);

  vkDeviceWaitIdle(rd->nvvkctx->device);

  const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

  const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

  // adhoc method of resetting them
  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);
    nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);

    vkDestroySemaphore(rd->nvvkctx->device, data->image_available_semaphore, &rd->nvvkctx->vkalloc);
    vkDestroySemaphore(rd->nvvkctx->device, data->render_finish_semaphore, &rd->nvvkctx->vkalloc);
    vkDestroyFence(rd->nvvkctx->device, data->in_flight_fence, &rd->nvvkctx->vkalloc);

    nv_assert_else_return(data->depth_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
    nv_gpu_destroy_texture(rd->nvvkctx, &data->depth_image);

    data->image_available_semaphore = VK_NULL_HANDLE;
    data->render_finish_semaphore   = VK_NULL_HANDLE;
    data->in_flight_fence           = VK_NULL_HANDLE;
  }
  nv_assert_else_return(rd->depth_image_memory.memory != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_gpu_free_memory(rd->nvvkctx, &rd->depth_image_memory);

  if (rd->color_image.image)
  {
    nv_assert_else_return(rd->color_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
    nv_assert_else_return(rd->color_image_memory.memory != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

    nv_gpu_destroy_texture(rd->nvvkctx, &rd->color_image);
    nv_gpu_free_memory(rd->nvvkctx, &rd->color_image_memory);
  }

  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);
    nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);

    nv_assert_else_return(data->sc_image.image != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

    // as the view was silently smushed into the
    // structure, we just kinda smush it out as well.
    vkDestroyImageView(rd->nvvkctx->device, nv_gpu_texture_get_view(&data->sc_image), &rd->nvvkctx->vkalloc);

    nv_assert_else_return(data->color_framebuffer.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
    nv_gpu_destroy_framebuffer(rd->nvvkctx, &data->color_framebuffer);
  }
  nv_list_clear(&rd->render_data);

  i32 w, h;
  SDL_Vulkan_GetDrawableSize(rd->ctx->window, &w, &h);

  VkSurfaceCapabilitiesKHR surface_capabilities;
  nvvk_result_check(*rd->nvvkctx, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rd->nvvkctx->phys_device, rd->nvvkctx->surface, &surface_capabilities));

  const u32 min_width  = surface_capabilities.minImageExtent.width;
  const u32 min_height = surface_capabilities.minImageExtent.height;

  const u32 max_width  = surface_capabilities.maxImageExtent.width;
  const u32 max_height = surface_capabilities.maxImageExtent.height;

  w = NVM_CLAMP((u32)w, min_width, max_width);
  h = NVM_CLAMP((u32)h, min_height, max_height);

  rd->render_extent = (nv_extent2d){ w, h };

  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_INVALID_RETVAL);

  /* Can be NULL */
  VkSwapchainKHR old_swapchain = rd->swapchain;

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (rd->flags & NOVA_RENDERER_VSYNC_ENABLE)
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

  nv_gpu_swapchain_create_info swapchain_create_info = nv_zero_init(nv_gpu_swapchain_create_info);
  swapchain_create_info.extent.width                 = rd->render_extent.width;
  swapchain_create_info.extent.height                = rd->render_extent.height;
  swapchain_create_info.present_mode                 = present_mode;
  swapchain_create_info.format                       = rd->swap_chain_image_format;
  swapchain_create_info.color_space                  = rd->swap_chain_color_space;
  swapchain_create_info.image_count                  = rd->swap_chain_image_count;
  swapchain_create_info.old_swapchain                = old_swapchain;

  nv_gpu_create_swapchain(rd->nvvkctx, &swapchain_create_info, &rd->swapchain);
  nv_assert_else_return(rd->swapchain != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  if (old_swapchain)
  {
    vkDestroySwapchainKHR(rd->nvvkctx->device, old_swapchain, &rd->nvvkctx->vkalloc);
  }

  nv_list_resize(&rd->render_data, rd->swap_chain_image_count);

  nv_error code = NV_ERROR_SUCCESS;

  if ((code = create_optional_images(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = create_framebuffers_and_swapchain_image_views(rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  for (size_t i = 0; i < rd->swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, i);
    nvvk_result_check(*rd->nvvkctx, vkCreateSemaphore(rd->nvvkctx->device, &semaphoreCreateInfo, &rd->nvvkctx->vkalloc, &data->render_finish_semaphore));
    nvvk_result_check(*rd->nvvkctx, vkCreateSemaphore(rd->nvvkctx->device, &semaphoreCreateInfo, &rd->nvvkctx->vkalloc, &data->image_available_semaphore));
    nvvk_result_check(*rd->nvvkctx, vkCreateFence(rd->nvvkctx->device, &fenceCreateInfo, &rd->nvvkctx->vkalloc, &data->in_flight_fence));

    nv_assert_else_return(data->render_finish_semaphore != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return(data->image_available_semaphore != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
    nv_assert_else_return(data->in_flight_fence != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);
  }

  _nv_reset_frame_buffer_resized(rd->ctx);

  return NV_ERROR_SUCCESS;
}

bool
nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_list_is_valid(&rd->render_data), NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_BROKEN_STATE);

  nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, rd->frame);
  nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(data->in_flight_fence != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  vkWaitForFences(rd->nvvkctx->device, 1, &data->in_flight_fence, VK_TRUE, UINT64_MAX);

  const VkResult imageAcquireResult = vkAcquireNextImageKHR(rd->nvvkctx->device, rd->swapchain, UINT64_MAX, data->image_available_semaphore, VK_NULL_HANDLE, &rd->image_index);

  if (imageAcquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAcquireResult == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized(rd->ctx))
  {
    _nvvk_renderer_resize(rd);
    return false;
  }
  else if (imageAcquireResult != VK_SUCCESS && imageAcquireResult != VK_SUBOPTIMAL_KHR)
  {
    nv_log_error("Failed to acquire image from swapchain\n");
    return false;
  }

  const VkCommandBuffer drawBuffer = *(VkCommandBuffer*)nv_list_get(&rd->draw_cmd_buffers, rd->frame);
  nv_assert_else_return(drawBuffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  vkResetFences(rd->nvvkctx->device, 1, &data->in_flight_fence);

  // I do, in fact, care about my beloveds

  nv_renderer_frame_render_info* image_render_info = (nv_renderer_frame_render_info*)(nv_list_get(&rd->render_data, rd->image_index));
  nv_assert_else_return(image_render_info != NULL, NV_ERROR_BROKEN_STATE);

  VkFramebuffer framebuffer = image_render_info->color_framebuffer.handle;
  nv_assert_else_return(framebuffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_pass != 0, NV_ERROR_BROKEN_STATE);

  // Why was this static?
  VkRenderPassBeginInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass      = rd->render_pass,
    .framebuffer     = framebuffer,
    .renderArea      = (VkRect2D){ .extent = (VkExtent2D){ rd->render_extent.width, rd->render_extent.height }, .offset = nv_zero_init(VkOffset2D) },
    .clearValueCount = 2,
    .pClearValues =
        (VkClearValue[2]){
            { .color = (VkClearColorValue){ { clear_color.x, clear_color.y, clear_color.z, clear_color.w } } },
            { .depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 } },
        },
  };

  const VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL };

  vkBeginCommandBuffer(drawBuffer, &beginInfo);
  vkCmdBeginRenderPass(drawBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {
    .x        = 0.0f,
    .y        = 0.0f,
    .width    = (float)rd->render_extent.width,
    .height   = (float)rd->render_extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(drawBuffer, 0, 1, &viewport);

  VkRect2D scissor = {
    .offset = (VkOffset2D){ 0, 0 },
    .extent = (VkExtent2D){ (unsigned)rd->render_extent.width, (unsigned)rd->render_extent.height },
  };
  vkCmdSetScissor(drawBuffer, 0, 1, &scissor);

  return true;
}

#define V2F_TO_V3F(v) ((vec3f){ (v).x, (v).y, 0.0f })

nv_error
nv_renderer_end(nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_list_is_valid(&rd->render_data), NV_ERROR_INVALID_RETVAL);
  nv_assert_else_return(nv_ctx_is_valid(rd->ctx) == true, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_BROKEN_STATE);

  const VkCommandBuffer draw_cmd_buffer = nv_renderer_get_draw_buffer(rd);
  nv_assert_else_return(draw_cmd_buffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  for (size_t i = 0; i < nv_list_size(&rd->ctext->labels); i++)
  {
    ctext_label_t* label = nv_list_get(&rd->ctext->labels, i);

    nv_sprite_renderer* spr_rd = nv_object_get_sprite_renderer(label->obj);

    ctext_text_render_info_t r_info = ctext_init_text_render_info();
    r_info.position                 = V2F_TO_V3F(nv_object_get_position(label->obj));
    r_info.color                    = spr_rd->color;
    r_info.horizontal               = label->h_align;
    r_info.vertical                 = label->v_align;
    ctext_render(label->fnt, &r_info, "%s", label->text);
  }
  ctext_flush_renders(rd);

  // nvui internally checks whether it has been initialized or not
  nvui_render(rd);
  _nv_renderer_flush_renders(rd);

  vkCmdEndRenderPass(draw_cmd_buffer);
  vkEndCommandBuffer(draw_cmd_buffer);

  VkSubmitInfo submitInfo = nv_zero_init(VkSubmitInfo);
  submitInfo.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  const nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->render_data, rd->frame);
  nv_assert_else_return(data != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(data->image_available_semaphore != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(data->render_finish_semaphore != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  const VkSemaphore waitSemaphores[]   = { data->image_available_semaphore };
  const VkSemaphore signalSemaphores[] = { data->render_finish_semaphore };

  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.pWaitDstStageMask      = waitStages;

  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores    = waitSemaphores;

  const VkCommandBuffer buffers[] = { draw_cmd_buffer };
  submitInfo.commandBufferCount   = nv_arrlen(buffers);
  submitInfo.pCommandBuffers      = buffers;

  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores    = signalSemaphores;

  vkQueueSubmit(rd->nvvkctx->present_queue, 1, &submitInfo, data->in_flight_fence);

  VkPresentInfoKHR presentInfo   = nv_zero_init(VkPresentInfoKHR);
  presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  // This starts as soon as the semaphores are signalled,
  /* id est, As soon as the render finished semaphore is signalled, it is presented to the screen */
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.pImageIndices   = &rd->image_index;
  presentInfo.swapchainCount  = 1;
  presentInfo.pSwapchains     = &rd->swapchain;

  VkResult result = VK_SUCCESS;
  result          = vkQueuePresentKHR(rd->nvvkctx->present_queue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized(rd->ctx))
  {
    nv_error code = NV_ERROR_SUCCESS;

    if ((code = _nvvk_renderer_resize(rd)) != NV_ERROR_SUCCESS)
    {
      return code;
    }
  }

  rd->frame = (rd->frame + 1) % nv_renderer_get_max_frames_in_flight(rd);

  return NV_ERROR_SUCCESS;
}

// nvgfx ^^

// engine
static SDL_UNUSED const char* ValidationLayers[] = {
  "VK_LAYER_KHRONOS_validation",
};

/* TODO: Should these be hard coded? */
/* Configured by a config file maybe? */
/* Add a library to load configs? Hm.. */

static SDL_UNUSED const char* REQUIRED_INSTANCE_EXTENSIONS[]   = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_REPORT_EXTENSION_NAME, NULL };
static SDL_UNUSED const int   NUM_REQUIRED_INSTANCE_EXTENSIONS = nv_arrlen(REQUIRED_INSTANCE_EXTENSIONS) - 1;

static SDL_UNUSED const char* WANTED_INSTANCE_EXTENSIONS[] = {
  // VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  NULL
};
static SDL_UNUSED const int NUM_WANTED_INSTANCE_EXTENSIONS = nv_arrlen(WANTED_INSTANCE_EXTENSIONS) - 1;

static SDL_UNUSED const char* WANTED_DEVICE_EXTENSIONS[] = {
  // VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
  NULL
};
static SDL_UNUSED const int NUM_WANTED_DEVICE_EXTENSIONS = nv_arrlen(WANTED_DEVICE_EXTENSIONS) - 1;

static SDL_UNUSED const char* REQUIRED_DEVICE_EXTENSIONS[]   = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, NULL };
static SDL_UNUSED const int   NUM_REQUIRED_DEVICE_EXTENSIONS = nv_arrlen(REQUIRED_DEVICE_EXTENSIONS) - 1;

// we'll just request them as needed

static const VkPhysicalDeviceFeatures WantedFeatures = {
  .samplerAnisotropy = VK_TRUE,
};

static inline void
_VK_DEBUG_LOG(bool is_error_msg, const char* fmt, ...)
{
  const char* preceder = is_error_msg ? " vkerr: " : " vkdebug: ";
  va_list     args;
  va_start(args, fmt);
  nv_log_va(__FILE__, __LINE__, "_", preceder, is_error_msg, fmt, args);
  va_end(args);
}

VKAPI_ATTR VkBool32 VKAPI_CALL
nvvk_debug_messenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData)
{
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;

  _VK_DEBUG_LOG(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, "%s\n", pCallbackData->pMessage);

  return VK_FALSE;
}

static inline nv_list_t
setify(u32 i1, u32 i2, u32 i3, u32 i4)
{
  nv_list_t ret;
  nv_list_init(sizeof(u32), 4, nv_allocator_c, NULL, &ret);
  u32 nums[4] = { i1, i2, i3, i4 };
  for (int j = 0; j < (int)nv_arrlen(nums); j++)
  {
    const u32 e          = nums[j];
    bool      already_in = false;
    for (size_t i = 0; i < nv_list_size(&ret); i++)
    {
      u32* exists_ptr = (u32*)nv_list_get(&ret, i);
      if (!exists_ptr)
      {
        continue;
      }
      if (e == *exists_ptr)
      {
        already_in = true;
      }
    }
    if (!already_in)
    {
      nv_list_push_back(&ret, &e);
    }
  }
  return ret;
}

static inline bool
nvvk_validate_layers()
{
  uchar buffer[2048];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  if (nv_arrlen(ValidationLayers) == 0)
  {
    return true;
  }

  bool validation_layers_available = true;

  uint32_t vk_layer_count = 0;
  vkEnumerateInstanceLayerProperties(&vk_layer_count, NULL);

  nv_assert_else_return(vk_layer_count != 0, true);

  nv_list_t vk_layer_properties;

  /**
   * this doesn't use the stack allocator, intentionally. VkLayerProperties is a whopping 520 bytes
   * and will easily overflow the stack
   */
  nv_error code = nv_list_init(sizeof(VkLayerProperties), vk_layer_count, nv_allocator_c, NULL, &vk_layer_properties);

  if (code != NV_SUCCESS)
  {
    nv_log_error("list initialization failed : %s\n", nv_error_str(code));
    return false;
  }

  vkEnumerateInstanceLayerProperties(&vk_layer_count, (VkLayerProperties*)nv_list_data(&vk_layer_properties));

  for (int j = 0; j < (int)nv_arrlen(ValidationLayers); j++)
  {
    const char* layer       = ValidationLayers[j];
    bool        layer_found = false;
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      const VkLayerProperties* vk_layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
      nv_assert_and_exec(vk_layer != NULL, continue;);

      if (nv_strcmp(layer, vk_layer->layerName) == 0)
      {
        layer_found = true;
      }
    }
    if (!layer_found)
    {
      validation_layers_available = false;
    }
  }

  if (!validation_layers_available)
  {
    nv_log_error("Failed to initialize validation layers. Requested layers:\n");
    for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
    {
      nv_log_error("\t%s\n", ValidationLayers[i]);
    }

    nv_log_error("Available Layers:\n");
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      const VkLayerProperties* layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
      nv_assert_and_exec(layer != NULL, continue;);
      nv_log_error("\t%s\n", layer->layerName);
    }

    /* We add the missing layers next */
    nv_log_error("But instance asked for (i.e. are not available):\n");

    nv_list_t missing_layers;
    nv_list_init(sizeof(const char*), 16, nv_allocator_estack, &stack, &missing_layers);

    for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
    {
      const char* layer          = ValidationLayers[i];
      bool        layerAvailable = false;
      for (uint32_t j = 0; j < vk_layer_count; j++)
      {
        const VkLayerProperties* vk_layer = (VkLayerProperties*)nv_list_get(&vk_layer_properties, i);
        nv_assert_and_exec(vk_layer != NULL, continue;);

        if (nv_strcmp(layer, vk_layer->layerName) == 0)
        {
          layerAvailable = true;
          break;
        }
      }
      if (!layerAvailable)
      {
        nv_list_push_back(&missing_layers, &layer);
      }
    }
    for (size_t i = 0; i < nv_list_size(&missing_layers); i++)
    {
      const char* layer = *(const char**)nv_list_get(&missing_layers, i);
      if (!layer)
      {
        layer = "(NULL)";
      }
      nv_log_error("\t%s\n", layer);
    }

    nv_log_error("Validation layers have NOT been enabled! Do you have the vulkan-validation-layers package installed?\n");

    nv_list_destroy(&missing_layers);
  }

  nv_list_destroy(&vk_layer_properties);

  /* true, as program will exit if we failed validation */
  return validation_layers_available;
}

static inline void
nvvk_setup_debug_messenger(nvvk_ctx_t* nvvkctx)
{
  VkDebugUtilsMessengerCreateInfoEXT create_info = nv_zero_init(VkDebugUtilsMessengerCreateInfoEXT);
  create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = nvvk_debug_messenger;

  PFN_vkCreateDebugUtilsMessengerEXT _CreateDebugUtilsMessenger =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(nvvkctx->instance, "vkCreateDebugUtilsMessengerEXT");

  if (!_CreateDebugUtilsMessenger)
  {
    nv_log_error("vkCreateDebugUtilsMessengerEXT exported function pointer not found.\n"
                 " Either the " VK_EXT_DEBUG_UTILS_EXTENSION_NAME " extension was not loaded or your driver does not support it.");
    nv_log_error("The debug messenger failed to initialize.\n");
    return;
  }

  VkResult r;
  if ((r = _CreateDebugUtilsMessenger(nvvkctx->instance, &create_info, &nvvkctx->vkalloc, &nvvkctx->debug_messenger)) != VK_SUCCESS)
  {
    nv_log_error("Vulkan debug messenger could not start. err %i\n", r);
    return;
  }

  /* Submit a message just to notify the user */
  vkSubmitDebugUtilsMessageEXT(
      nvvkctx->instance,
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
      &(VkDebugUtilsMessengerCallbackDataEXT){
          .sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
          .pMessage = "The vulkan debug messenger has been set up",
      });
}

/* returned_valid_extensions contains a list of valid extensions */
static inline void
nvvk_get_valid_extensions(nv_ctx_t* ctx, nv_list_t* returned_valid_extensions)
{
  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(ctx->window, &SDLExtensionCount, NULL) == SDL_TRUE);

  const size_t sdl_extensions_size = sizeof(const char*) * SDLExtensionCount;
  const char** sdl_extensions      = nv_allocator_estack(&stack, NULL, NV_ALLOC_NEW_BLOCK, sdl_extensions_size);

  nv_assert(SDL_Vulkan_GetInstanceExtensions(ctx->window, &SDLExtensionCount, sdl_extensions) == SDL_TRUE);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
  // VkExtensionProperties is too big to fit on the stack
  nv_list_t vk_extensions;
  nv_list_init(sizeof(VkExtensionProperties), extensionCount, nv_allocator_c, NULL, &vk_extensions);
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, (VkExtensionProperties*)nv_list_data(&vk_extensions));

  for (size_t i = 0; i < NUM_REQUIRED_INSTANCE_EXTENSIONS; i++)
  {
    const char* ext = REQUIRED_INSTANCE_EXTENSIONS[i];
    nv_list_push_back(returned_valid_extensions, &ext);
  }

  for (u32 i = 0; i < SDLExtensionCount; i++)
  {
    const char* ext = sdl_extensions[i];
    nv_list_push_back(returned_valid_extensions, &ext);
  }

  for (u32 i = 0; i < extensionCount; i++)
  {
    const char* name = ((VkExtensionProperties*)nv_list_data(&vk_extensions))[i].extensionName;
    for (int j = 0; j < (int)NUM_WANTED_INSTANCE_EXTENSIONS; j++)
    {
      const char* want = WANTED_INSTANCE_EXTENSIONS[j];
      if (nv_strcmp(name, want) == 0)
      {
        nv_list_push_back(returned_valid_extensions, &name);
        break;
      }
    }
  }

  nv_allocator_estack(&stack, sdl_extensions, sdl_extensions_size, NV_ALLOC_FREE);
  nv_list_destroy(&vk_extensions);
}

static inline VkInstance
nvvk_create_instance(nvvk_ctx_t* nvvkctx, nv_ctx_t* ctx, const char* title)
{
  if (volkInitialize() != VK_SUCCESS)
  {
    nv_log_and_abort("Volk could not initialize. You probably don't have the \n"
                     "vulkan loader installed. "
                     "I can't do anything about that.");
  }

  VkApplicationInfo app_info = {
    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = title,
    .applicationVersion = 0,
    .pEngineName        = "NOVA",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_0,
  };

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(ctx->window, &SDLExtensionCount, NULL) == SDL_TRUE);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

  nv_list_t enabled_extensions;
  nv_list_init(
      sizeof(const char*),
      (NUM_REQUIRED_INSTANCE_EXTENSIONS + SDLExtensionCount + extensionCount + NUM_WANTED_INSTANCE_EXTENSIONS),
      nv_allocator_estack,
      &stack,
      &enabled_extensions);

  nvvk_get_valid_extensions(ctx, &enabled_extensions);

  VkInstanceCreateInfo instance_create_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledExtensionCount   = nv_list_size(&enabled_extensions),
    .ppEnabledExtensionNames = (const char**)nv_list_data(&enabled_extensions),
  };

  bool validation_layers_available = false;

#ifdef DEBUG

  validation_layers_available = nvvk_validate_layers();
  if (validation_layers_available)
  {
    instance_create_info.enabledLayerCount   = nv_arrlen(ValidationLayers);
    instance_create_info.ppEnabledLayerNames = ValidationLayers;
  }
  else
  {
    instance_create_info.enabledLayerCount   = 0;
    instance_create_info.ppEnabledLayerNames = NULL;
  }

#else

  instance_create_info.enabledLayerCount   = 0;
  instance_create_info.ppEnabledLayerNames = NULL;

#endif

  nvvk_result_check(*nvvkctx, vkCreateInstance(&instance_create_info, &nvvkctx->vkalloc, &nvvkctx->instance));

  /* Load the volk functions as soon as available, we need them to set up the debug messengers and stuff. */
  volkLoadInstance(nvvkctx->instance);

#ifdef DEBUG
  nvvk_setup_debug_messenger(nvvkctx);

  nv_log_info("Enabled validation layers: [ ");
  for (size_t i = 0; i < nv_arrlen(ValidationLayers); i++)
  {
    nv_printf("\"%s\", ", ValidationLayers[i]);
  }
  nv_printf(" ]\n");

  nv_log_info("Enabled instance extensions: [ ");
  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    nv_printf("\"%s\", ", *(const char**)nv_list_get(&enabled_extensions, i));
  }
  nv_printf(" ]\n");

#endif

  nv_list_destroy(&enabled_extensions);

  return nvvkctx->instance;
}

static inline void
nvvk_print_device_info(VkPhysicalDevice device)
{
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  const char* device_type_str;
  const char* device_driver_vendor;
  switch (properties.deviceType)
  {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type_str = "Discrete"; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type_str = "Integrated"; break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type_str = "Software/CPU"; break;
    default: device_type_str = "Unknown"; break;
  }

  switch (properties.vendorID)
  {
    case 0x1002: device_driver_vendor = "AMD"; break;
    case 0x10DE: device_driver_vendor = "NVIDIA"; break;
    case 0x8086: device_driver_vendor = "Intel"; break;
    default: device_driver_vendor = "Unknown Vendor"; break;
  }

  // I think it looks cleaner this way
  nv_log_info("(%s) %s\n", device_type_str, properties.deviceName);
  nv_log_info("Vulkan API Version: %u.%u.%u\n", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
  nv_log_info(
      "Driver Vendor: %s Driver Version: %u.%u.%u Device ID: %#x\n",
      device_driver_vendor,
      VK_VERSION_MAJOR(properties.driverVersion),
      VK_VERSION_MINOR(properties.driverVersion),
      VK_VERSION_PATCH(properties.driverVersion),
      properties.deviceID);
}

static inline VkPhysicalDevice
_nvvk_choose_physical_device(nvvk_ctx_t* nvvkctx, VkInstance instance, VkSurfaceKHR surface)
{
  nv_assert_else_return(nvvkctx != NULL, VK_NULL_HANDLE);
  nv_assert_else_return(instance != VK_NULL_HANDLE, VK_NULL_HANDLE);
  nv_assert_else_return(surface != VK_NULL_HANDLE, VK_NULL_HANDLE);

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  uint32_t phys_device_count = 0;

  VkResult r = VK_SUCCESS;

  if ((r = vkEnumeratePhysicalDevices(instance, &phys_device_count, NULL)) != VK_SUCCESS)
  {
    nv_log_and_abort("Error fetching physical devices. VkResult=%i\n", r);
  }

  if (phys_device_count == 0)
  {
    nv_log_and_abort("Huuuhhh??? No physical devices found? Are you running this"
                     "on a banana???\n");
  }

  nv_list_t physical_devices;
  nv_list_init(sizeof(VkPhysicalDevice), phys_device_count, nv_allocator_estack, &stack, &physical_devices);
  vkEnumeratePhysicalDevices(instance, &phys_device_count, (VkPhysicalDevice*)nv_list_data(&physical_devices));

  for (u32 i = 0; i < phys_device_count; i++)
  {
    if (nv_list_get(&physical_devices, i) == NULL)
    {
      nv_log_error("NULL physical device? Do you have enough memory?\n");
      continue;
    }
    const VkPhysicalDevice device = *(VkPhysicalDevice*)nv_list_get(&physical_devices, i);

    uint32_t format_count = 0;
    nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL));

    uint32_t present_mode_count = 0;
    nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL));

    bool extensionsAvailable = true;

    uint32_t extension_count = 0;
    nvvk_result_check(*nvvkctx, vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL));
    nv_list_t available_extensions;
    nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_c, NULL, &available_extensions);
    nvvk_result_check(*nvvkctx, vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&available_extensions)));

    for (size_t i = 0; i < NUM_WANTED_DEVICE_EXTENSIONS; i++)
    {
      const char* extension = REQUIRED_DEVICE_EXTENSIONS[i];
      bool        validated = false;
      for (u32 j = 0; j < extension_count; j++)
      {
        if (nv_strcmp(extension, ((VkExtensionProperties*)nv_list_data(&available_extensions))[j].extensionName) == 0)
        {
          validated = true;
        }
      }
      if (!validated)
      {
        nv_log_error("Failed to validate extension with name: %s\n", extension);
        extensionsAvailable = false;
      }
    }

    nv_list_destroy(&available_extensions);

    if (extensionsAvailable && format_count > 0 && present_mode_count > 0)
    {
      nvvk_print_device_info(device);
      nv_list_destroy(&physical_devices);
      return device;
    }
  }

  if (nv_list_front(&physical_devices) == NULL)
  {
    nv_log_error("No physical devices to run on.\n");
    return NULL;
  }
  VkPhysicalDevice fallback = *(VkPhysicalDevice*)nv_list_front(&physical_devices);

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(fallback, &properties);

  nv_log_error("No device found. Falling back to device(0) \"%s\".", properties.deviceName);

  nvvk_print_device_info(fallback);

  nv_list_destroy(&physical_devices);

  return fallback;
}

// WARNING: does not init available_extensions itself!!!
static inline void
nvvk_get_valid_device_extensions(nvvk_ctx_t* nvvkctx, nv_list_t* available_extensions)
{
  nv_assert_else_return(nvvkctx->phys_device != NULL, );

  u32 extension_count = 0;
  vkEnumerateDeviceExtensionProperties(nvvkctx->phys_device, NULL, &extension_count, NULL);
  nv_list_t extensions;
  nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_c, NULL, &extensions);
  vkEnumerateDeviceExtensionProperties(nvvkctx->phys_device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&extensions));

  for (size_t i = 0; i < NUM_WANTED_DEVICE_EXTENSIONS; i++)
  {
    const char* wanted = WANTED_DEVICE_EXTENSIONS[i];
    for (u32 i = 0; i < extension_count; i++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[i];
      if (nv_strcmp(wanted, ext.extensionName) == 0)
      {
        const char* ext_name_copy = nv_strdup(nv_allocator_c, NULL, ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
      }
    }
  }

  for (size_t i = 0; i < NUM_REQUIRED_DEVICE_EXTENSIONS; i++)
  {
    const char* required  = REQUIRED_DEVICE_EXTENSIONS[i];
    bool        validated = false;
    for (u32 i = 0; i < extension_count; i++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[i];
      if (nv_strcmp(required, ext.extensionName) == 0)
      {
        char* ext_name_copy = nv_strdup(nv_allocator_c, NULL, ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
        validated = true;
      }
    }

    if (!validated)
    {
      nv_log_error("Failed to validate required extension with name %s\n", required);
    }
  }

  nv_list_destroy(&extensions);
}

static inline void
nvvk_validate_queues(nvvk_ctx_t* nvvkctx, nv_list_t* queue_create_infos)
{
  nv_assert_else_return(nvvkctx->phys_device != VK_NULL_HANDLE, );
  nv_assert_else_return(nvvkctx->surface != VK_NULL_HANDLE, );

  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(nvvkctx->phys_device, &queue_count, NULL);
  nv_list_t queue_families;
  nv_list_init(sizeof(VkQueueFamilyProperties), queue_count, nv_allocator_c, NULL, &queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(nvvkctx->phys_device, &queue_count, (VkQueueFamilyProperties*)nv_list_data(&queue_families));

  // Clang loves complaining about these.
  u32 graphics_family = 0, present_family = 0, compute_family = 0, transfer_family = 0;
  (void)graphics_family, (void)present_family, (void)compute_family, (void)transfer_family;

  bool found_graphics_family = false, found_present_family = false, found_compute_family = false, found_transfer_family = false;

  u32 i = 0;
  for (int j = 0; j < (int)nv_list_size(&queue_families); j++)
  {
    const VkQueueFamilyProperties queue_family    = ((VkQueueFamilyProperties*)nv_list_data(&queue_families))[j];
    VkBool32                      present_support = false;
    nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceSupportKHR(nvvkctx->phys_device, i, nvvkctx->surface, &present_support));

    if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphics_family       = i;
      found_graphics_family = true;
    }
    if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      compute_family       = i;
      found_compute_family = true;
    }
    if (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
      transfer_family       = i;
      found_transfer_family = true;
    }
    if (present_support)
    {
      present_family       = i;
      found_present_family = true;
    }
    if (found_graphics_family && found_compute_family && found_present_family && found_transfer_family)
    {
      break;
    }

    i++;
  }

  nv_list_t unique_queue_families = setify(graphics_family, present_family, compute_family, transfer_family);

  /**
   * Vulkan gives errores sometimes even though the spec states that if queueCount is 1,
   * Only 1 element of pQueuePriorities may be checked. Seems like an issue with the validation layers
   * Should I submit a bug report? Nah. They can deal with it.
   * It doesn not seem to be a bug with them, but the fact that this float variable is local and it goes out of scope
   * FIXED
   */
  // const float queue_priorities[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

  for (size_t i = 0; i < nv_list_size(&unique_queue_families); i++)
  {
    VkDeviceQueueCreateInfo queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = ((u32*)nv_list_data(&unique_queue_families))[i],
      .queueCount       = 1,
      .pQueuePriorities = NULL,
    };
    nv_list_push_back(queue_create_infos, &queue_info);
  }

  nv_list_destroy(&queue_families);
  nv_list_destroy(&unique_queue_families);
}

static inline VkDevice
nvvk_create_device(nvvk_ctx_t* nvvkctx)
{
  nv_assert_else_return(nvvkctx->phys_device != VK_NULL_HANDLE, NULL);
  nv_assert_else_return(nvvkctx->surface != VK_NULL_HANDLE, NULL);

  nv_list_t enabled_extensions;
  nv_list_init(sizeof(const char*), NUM_WANTED_DEVICE_EXTENSIONS + NUM_WANTED_DEVICE_EXTENSIONS, nv_allocator_c, NULL, &enabled_extensions);
  nvvk_get_valid_device_extensions(nvvkctx, &enabled_extensions);

  nv_list_t queue_create_infos;
  nv_list_init(sizeof(VkDeviceQueueCreateInfo), 0, nv_allocator_c, NULL, &queue_create_infos);
  nvvk_validate_queues(nvvkctx, &queue_create_infos);

  const float queue_priority = 1.0F;
  for (size_t i = 0; i < nv_list_size(&queue_create_infos); i++)
  {
    VkDeviceQueueCreateInfo* info = (VkDeviceQueueCreateInfo*)nv_list_get(&queue_create_infos, i);
    info->pQueuePriorities        = &queue_priority;
  }

  VkDeviceCreateInfo deviceCreateInfo = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount    = nv_list_size(&queue_create_infos),
    .pQueueCreateInfos       = (const VkDeviceQueueCreateInfo*)nv_list_data(&queue_create_infos),
    .enabledExtensionCount   = nv_list_size(&enabled_extensions),
    .ppEnabledExtensionNames = (const char* const*)nv_list_data(&enabled_extensions),
    .pEnabledFeatures        = &WantedFeatures,
  };

  nvvk_result_check(*nvvkctx, vkCreateDevice(nvvkctx->phys_device, &deviceCreateInfo, &nvvkctx->vkalloc, &nvvkctx->device));

#ifndef NDEBUG
  nv_log_info(" Enabled device extensions: [ ");
  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    nv_printf("\"%s\", ", *(const char**)nv_list_get(&enabled_extensions, i));
  }
  nv_printf(" ]\n");
#endif

  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    const char* ext_name_allocated = *(const char**)nv_list_get(&enabled_extensions, i);
    nv_free((void*)ext_name_allocated);
  }
  nv_list_destroy(&enabled_extensions);
  nv_list_destroy(&queue_create_infos);

  return nvvkctx->device;
}

static inline nv_error
_nvvk_ctx_setup_queues(nvvk_ctx_t* nvvkctx)
{
  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(nvvkctx->phys_device, &queue_count, NULL);
  nv_assert_else_return(queue_count != 0, NV_ERROR_EXTERNAL);

  nv_list_t queue_families;
  nv_list_init(sizeof(VkQueueFamilyProperties), queue_count, nv_allocator_c, NULL, &queue_families);
  nv_assert_else_return(nv_list_data(&queue_families) != NULL, NV_ERROR_EXTERNAL);

  vkGetPhysicalDeviceQueueFamilyProperties(nvvkctx->phys_device, &queue_count, (VkQueueFamilyProperties*)nv_list_data(&queue_families));

  u32 graphics_family             = 0;
  u32 graphics_and_compute_family = 0;
  u32 present_family              = 0;
  u32 compute_family              = 0;
  u32 transfer_family             = 0;

  bool found_graphics_family             = false;
  bool found_graphics_and_compute_family = false;
  bool found_present_family              = false;
  bool found_compute_family              = false;
  bool found_transfer_family             = false;

  u32 family_index = 0;
  for (u32 j = 0; j < queue_count; j++)
  {
    const VkQueueFamilyProperties queueFamily = ((VkQueueFamilyProperties*)nv_list_data(&queue_families))[j];
    VkBool32                      presentSupport;
    nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceSupportKHR(nvvkctx->phys_device, family_index, nvvkctx->surface, &presentSupport));

    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      graphics_and_compute_family       = family_index;
      found_graphics_and_compute_family = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphics_family       = family_index;
      found_graphics_family = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      compute_family       = family_index;
      found_compute_family = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
      transfer_family       = family_index;
      found_transfer_family = true;
    }
    if (presentSupport)
    {
      present_family       = family_index;
      found_present_family = true;
    }
    if (found_graphics_family && found_graphics_and_compute_family && found_present_family && found_compute_family && found_transfer_family)
    {
      break;
    }

    family_index++;
  }

  nvvkctx->graphics_family_index             = graphics_family;
  nvvkctx->compute_family_index              = compute_family;
  nvvkctx->transfer_family_index             = transfer_family;
  nvvkctx->present_family_index              = present_family;
  nvvkctx->graphics_and_compute_family_index = graphics_and_compute_family;

  vkGetDeviceQueue(nvvkctx->device, nvvkctx->graphics_family_index, 0, &nvvkctx->graphics_queue);
  vkGetDeviceQueue(nvvkctx->device, nvvkctx->compute_family_index, 0, &nvvkctx->compute_queue);
  vkGetDeviceQueue(nvvkctx->device, nvvkctx->transfer_family_index, 0, &nvvkctx->transfer_queue);
  vkGetDeviceQueue(nvvkctx->device, nvvkctx->present_family_index, 0, &nvvkctx->present_queue);
  vkGetDeviceQueue(nvvkctx->device, nvvkctx->graphics_and_compute_family_index, 0, &nvvkctx->graphics_and_compute_queue);

  nv_list_destroy(&queue_families);
  return NV_ERROR_SUCCESS;
}

nv_error
nvvk_ctx_init(nv_ctx_t* nvctx, nvvk_ctx_t* dst)
{
  nv_assert_else_return(nv_ctx_is_valid(nvctx) != false, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nvvk_ctx_t));

  dst->vkalloc = (VkAllocationCallbacks){
    .pUserData             = dst,
    .pfnAllocation         = nvvk_alloc,
    .pfnReallocation       = nvvk_realloc,
    .pfnFree               = nvvk_free,
    .pfnInternalAllocation = nvvk_internal_allocation,
    .pfnInternalFree       = nvvk_internal_free,
  };

  dst->result_fn = _nvvk_default_result_check_fn;

  dst->instance = nvvk_create_instance(dst, nvctx, SDL_GetWindowTitle(nvctx->window));
  nv_assert_else_return(dst->instance != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  if (SDL_Vulkan_CreateSurface(nvctx->window, dst->instance, &dst->surface) != SDL_TRUE)
  {
    nv_log_and_abort("Surface creation failed.\nSDL reports: %s\n", SDL_GetError());
  }
  nv_assert_else_return(dst->surface != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  dst->phys_device = _nvvk_choose_physical_device(dst, dst->instance, dst->surface);
  nv_assert_else_return(dst->phys_device != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  dst->device = nvvk_create_device(dst);
  nv_assert_else_return(dst->device != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  volkLoadDevice(dst->device);

  nv_error code = NV_SUCCESS;
  if ((code = _nvvk_ctx_setup_queues(dst)) != NV_SUCCESS)
  {
    return code;
  }

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dst->phys_device, &props);

  dst->max_anisotropy = props.limits.maxSamplerAnisotropy;

  // TODO: is this correct?
  dst->supports_multisampling = true;

  const VkSampleCountFlags vk_samples = props.limits.framebufferColorSampleCounts;
  if (vk_samples & VK_SAMPLE_COUNT_64_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_64_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_32_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_32_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_16_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_16_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_8_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_8_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_4_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_4_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_2_BIT)
  {
    dst->max_samples = VK_SAMPLE_COUNT_2_BIT;
  }
  else
  {
    dst->max_samples            = VK_SAMPLE_COUNT_1_BIT;
    dst->supports_multisampling = false;
  }

  nv_memset(dst->cmd_buffers_in_use, false, NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT);

  return NV_SUCCESS;
}

void
nvvk_ctx_destroy(nvvk_ctx_t* ctx)
{
  if (!ctx)
  {
    return;
  }

  vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debug_messenger, &ctx->vkalloc);
  vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
  vkDestroyDevice(ctx->device, &ctx->vkalloc);
  vkDestroyInstance(ctx->instance, &ctx->vkalloc);

  volkFinalize();

  nv_bzero(ctx, sizeof(nvvk_ctx_t));
}

// engine

// ctext vv

/* I have no idea what any of this is */

static inline void
_ctext_load_font_upload_glyph_atlas(nvvk_ctx_t* nvvkctx, nv_renderer_t* rd, const nv_texture_atlas_t* atlas, cfont_t* dst)
{
  nv_gpu_texture_create_info image_info = {
    .format      = NOVA_FORMAT_R8,
    .samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .type        = VK_IMAGE_TYPE_2D,
    .usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .extent      = (nv_extent3D){ .width = atlas->width, .height = atlas->height, .depth = 1 },
    .arraylayers = 1,
    .miplevels   = 1,
  };
  nv_gpu_create_texture(rd->nvvkctx, &image_info, &dst->texture);

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(nvvkctx->device, nv_gpu_texture_get(&dst->texture), &imageMemoryRequirements);

  nv_gpu_allocate_memory(nvvkctx, imageMemoryRequirements.size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &dst->texture_mem);
  nv_gpu_bind_texture_to_memory(rd->nvvkctx, &dst->texture_mem, 0, &dst->texture);

  const size_t atlas_w = atlas->width;
  const size_t atlas_h = atlas->height;

  nv_image_t atlas_img = (nv_image_t){ .width = atlas_w, .height = atlas_h, .format = NOVA_FORMAT_R8, .data = (unsigned char*)atlas->data };
  nv_gpu_write_to_texture(rd->driver, &dst->texture, &atlas_img);

  const nv_gpu_sampler_create_info sampler_info = {
    .filter        = NV_FILTER_LINEAR,
    .mipmap_filter = NV_FILTER_LINEAR,
    .address_mode  = NV_TEXTURE_ADDRESS_MODE_REPEAT,
  };
  nv_gpu_create_sampler(rd->driver, &sampler_info, &dst->sampler);
}

static inline void
_ctext_load_font_update_descriptors(nvvk_ctx_t* nvvkctx, nv_ctext_module* ctext, cfont_t* dst)
{
  const VkDescriptorImageInfo ctext_bitmap_image_info = {
    .sampler     = nv_gpu_sampler_get(dst->sampler),
    .imageView   = nv_gpu_texture_get_view(&dst->texture),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet writeSet = { .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet          = ctext->desc_set->set,
                                    .dstBinding      = 0,
                                    .descriptorCount = 1,
                                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo      = &ctext_bitmap_image_info };
  for (size_t i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    writeSet.dstArrayElement = i;
    nv_descriptor_set_submit_write(nvvkctx, ctext->desc_set, &writeSet);
  }
}

void
ctext_load_font(nvvk_ctx_t* nvvkctx, nv_renderer_t* rdr, const char* font_path, int scale, cfont_t* dst)
{
  if (!rdr || !dst)
  {
    nv_log_error("rdr or dst is NULL!\n");
    return;
  }

  if (scale <= 0)
  {
    nv_log_error("attempting to load a font with 0 fontscale.\n");
    return;
  }

  *dst = nv_zero_init(cfont_t);

  fontc_file_t f_file;
  if (fontc_load_font(font_path, scale, &f_file) != 0)
  {
    nv_log_error("There was an error loading the font file. Skipping\n");
    return;
  }

  // Store a pointer to the font for future reference
  *(cfont_t**)nv_list_push_empty(&rdr->ctext->fonts) = dst;

  dst->rd = rdr;

  nv_hashmap_init(256, sizeof(u32), sizeof(ctext_glyph_t), nv_hash_murmur3, nv_allocator_c, NULL, &dst->glyph_map);
  nv_list_init(sizeof(ctext_drawcall_t), 4, nv_allocator_c, NULL, &dst->drawcalls);

  nv_texture_atlas_t atlas;

  dst->line_height = f_file.header.line_height;
  dst->space_width = f_file.header.space_width;
  atlas.width      = f_file.header.bmpwidth;
  atlas.height     = f_file.header.bmpheight;
  atlas.data       = f_file.bitmap;

  for (size_t i = 0; i < f_file.header.numglyphs; i++)
  {
    ctext_glyph_t glyph = {
      .x0      = f_file.glyphs[i].x0,
      .x1      = f_file.glyphs[i].x1,
      .y0      = f_file.glyphs[i].y0,
      .y1      = f_file.glyphs[i].y1,
      .l       = f_file.glyphs[i].l,
      .r       = f_file.glyphs[i].r,
      .b       = f_file.glyphs[i].b,
      .t       = f_file.glyphs[i].t,
      .advance = f_file.glyphs[i].advance,
    };
    u32 codepoint = f_file.glyphs[i].codepoint;
    nv_hashmap_insert(&dst->glyph_map, &codepoint, &glyph, NULL);
  }

  _ctext_load_font_upload_glyph_atlas(nvvkctx, rdr, &atlas, dst);
  _ctext_load_font_update_descriptors(nvvkctx, rdr->ctext, dst);

  fontc_clean_font_file(&f_file);

  const size_t initial_font_gpu_buffer_size = 1024;

  dst->allocated_size = initial_font_gpu_buffer_size;

  nv_error code = nv_gpu_buffer_init(rdr->driver, initial_font_gpu_buffer_size, 4, NV_GPU_BUFFER_VERTEX_BUFFER_BIT | NV_GPU_BUFFER_INDEX_BUFFER_BIT, &dst->buffer);
  nv_assert_else_return(code == NV_SUCCESS, );

  code = nv_gpu_buffer_init(rdr->driver, initial_font_gpu_buffer_size, 4, NV_GPU_BUFFER_TRANSIENT_BIT | NV_GPU_BUFFER_PERSISTENT_MAPPED, &dst->staging_buffer);
  nv_assert_else_return(code == NV_SUCCESS, );

  if (ctext_validate_font(dst) != 0)
  {
    // nv_log_error("Broken font. Something has gone horribly wrong\n");
    return;
  }
}

int
ctext_validate_font(const cfont_t* fnt)
{
  // If the renderer of the font has died, die along with the renderer.
  if (!fnt || !fnt->rd)
  {
    return -1;
  }
  // TODO: Add support for a constant canary across the whole project?
  if (fnt->glyph_map.canary != CONT_CANARY || fnt->drawcalls.canary != CONT_CANARY)
  {
    return -1;
  }
  return 0;
}

void
ctext_destroy_font(nvvk_ctx_t* nvvkctx, cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  nv_gpu_destroy_texture(nvvkctx, &fnt->texture);
  nv_gpu_free_memory(nvvkctx, &fnt->texture_mem);

  if (fnt->buffer.buffer != VK_NULL_HANDLE)
  {
    // nv_gpu_destroy_buffer(nvvkctx, &fnt->buffer);
    nv_gpu_buffer_destroy(&fnt->buffer);
    // nv_gpu_free_memory(nvvkctx, &fnt->buffer_mem);
  }
  if (fnt->staging_buffer.buffer != VK_NULL_HANDLE)
  {
    nv_gpu_buffer_destroy(&fnt->staging_buffer);
  }

  nv_list_destroy(&fnt->drawcalls);
  nv_hashmap_destroy(&fnt->glyph_map);

  nv_bzero(fnt, sizeof(cfont_t));
}

static inline bool
_ctext_font_resize_buffer(nvvk_ctx_t* nvvkctx, cfont_t* fnt, size_t new_buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return false;
  }

  size_t new_allocation_size = 0;

  if (fnt->allocated_size < new_buffer_size)
  {
    new_allocation_size = NV_MAX(fnt->allocated_size * 2, new_buffer_size);
  }
  else if (new_buffer_size < (fnt->allocated_size / 3))
  {
    new_allocation_size = NV_MAX(fnt->allocated_size / 3, new_buffer_size);
  }
  else
  {
    return false;
  }

  vkDeviceWaitIdle(nvvkctx->device);

  if (fnt->buffer.buffer != VK_NULL_HANDLE)
  {
    nv_gpu_buffer_destroy(&fnt->buffer);
  }
  if (fnt->staging_buffer.buffer != VK_NULL_HANDLE)
  {
    nv_gpu_buffer_destroy(&fnt->staging_buffer);
  }

  nv_error code = nv_gpu_buffer_init(fnt->rd->driver, new_allocation_size, 4, NV_GPU_BUFFER_VERTEX_BUFFER_BIT | NV_GPU_BUFFER_INDEX_BUFFER_BIT, &fnt->buffer);
  nv_assert_else_return(code == NV_SUCCESS, false);

  code = nv_gpu_buffer_init(fnt->rd->driver, new_allocation_size, 4, NV_GPU_BUFFER_TRANSIENT_BIT | NV_GPU_BUFFER_PERSISTENT_MAPPED, &fnt->staging_buffer);
  nv_assert_else_return(code == NV_SUCCESS, false);

  fnt->allocated_size = new_allocation_size;
  fnt->to_render      = 0;

  return true;
}

static inline void
_ctext_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  const VkCommandBuffer cmd       = nv_renderer_get_draw_buffer(rd);
  const VkDeviceSize    offsets[] = { 0 };

  struct push_constants pc = nv_zero_init(struct push_constants);

  const VkPipeline       pipeline        = g_Pipelines.ctext.pipeline;
  const VkPipelineLayout pipeline_layout = g_Pipelines.ctext.pipeline_layout;

  const VkDescriptorSet sets[]           = { camera.sets->set, rd->ctext->desc_set->set };
  const uint32_t        camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);

  // Viewport && scissor are set by renderer so no need to set them here
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
  vkCmdBindVertexBuffers(cmd, 0, 1, &fnt->buffer.buffer, offsets);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindIndexBuffer(cmd, fnt->buffer.buffer, fnt->index_buffer_offset, VK_INDEX_TYPE_UINT32);

  size_t offset = 0;
  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);

    pc.model = drawcall->model;
    pc.scale = drawcall->scale;
    pc.color = drawcall->color;
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

    vkCmdDrawIndexed(cmd, drawcall->index_count, 1, 0, (int32_t)offset, 0);
    offset += drawcall->vertex_count;
  }
}

static nv_list_t
split_string_by_lines(const char* str)
{
  nv_list_t    result;
  char*        substr  = NULL;
  const size_t str_len = nv_strlen(str);
  size_t       i_start = 0;

  nv_list_init(sizeof(char*), 16, nv_allocator_c, NULL, &result);

  // FIXED: consecutive newlines not being considered
  // they are now added as a single NULL terminator
  for (size_t i = 0; i < str_len; i++)
  {
    if ((str[i] == '\n' || str[i] == '\r') && (i - i_start) > 0)
    {
      const size_t sub_len = i - i_start;
      if (sub_len > 0)
      {
        substr = nv_substr(str, i_start, sub_len);
      }
      else
      {
        substr = "";
      }
      nv_list_push_back(&result, (void*)&substr);
      i_start = i + 1;
    }
  }

  const size_t rest_len = str_len - i_start;
  if (rest_len > 0)
  {
    substr = nv_substr(str, i_start, rest_len);
    nv_list_push_back(&result, (void*)&substr);
  }

  return result;
}

// static nv_list_t
// split_string_by_lines(char *str)
// {
//   nv_list_t result;
//   nv_list_init(sizeof(char*), 16, nv_allocator_c,  NULL, &result);

//   char *line_start = str;
//   for (size_t i = 0; str[i] != '\0'; i++)
//   {
//     if (str[i] == '\n' || str[i] == '\r')
//     {
//       str[i] = '\0';
//       nv_list_push_back(&result, &line_start);
//       line_start = &str[i + 1];
//     }
//   }
//   nv_list_push_back(&result, &line_start);

//   return result;
// }

// Get the unscaled size of the string
// Warning: slow
void
ctext_get_text_size(const cfont_t* fnt, const char* str, flt_t* w, flt_t* h)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    *w = FLT_MAX;
    *h = FLT_MAX;
    return;
  }

  flt_t width = 0.0f, height = fnt->line_height, prev_width = 0.0f;

  bool is_new_line = true;

  u32 codepoint = 0;

  while (*str)
  {
    codepoint = (u32)*str;
    switch (*str)
    {
      case ' ':
        width += fnt->space_width;
        is_new_line = 0;
        break;
      case '\t':
        width += fnt->space_width * 4.0f;
        is_new_line = 0;
        break;
      case '\n':
      case '\r':
        if (!is_new_line)
        {
          height += fnt->line_height;
          prev_width = NV_MAX(width, prev_width);
        }
        width       = 0.0f;
        is_new_line = 1;
        break;
      default:
      {
        const ctext_glyph_t* glyph = (ctext_glyph_t*)nv_hashmap_find(&fnt->glyph_map, &codepoint, NULL);
        if (!glyph)
        {
          break;
        }
        width += glyph->advance;
        is_new_line = 0;
        break;
      }
    }
    str++;
  }

  if (!is_new_line)
  {
    prev_width = NV_MAX(width, prev_width);
  }

  if (w)
  {
    *w = prev_width;
  }

  if (h)
  {
    *h = -height;
  }
}

// TODO: Implement instancing: Very hard
static size_t
_ctext_render_line(const cfont_t* fnt, const char* str, const ctext_drawcall_t* drawcall, flt_t scale, flt_t zpos, const size_t glyph_iter, flt_t x, flt_t y)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return -1;
  }

  u32 codepoint = 0;

  size_t iter = 0;

  const size_t string_length = nv_strlen(str);
  for (size_t i = 0; i < string_length; i++)
  {
    if (str[i] == ' ')
    {
      x += fnt->space_width * scale;
      continue;
    }
    if (str[i] == '\t')
    {
      x += fnt->space_width * 4.0f * scale;
      continue;
    }

    codepoint                  = (u32)str[i];
    const ctext_glyph_t* glyph = (const ctext_glyph_t*)nv_hashmap_find(&fnt->glyph_map, &codepoint, NULL);
    if (!glyph)
    {
      nv_log_info("no glyph when rendering char (%i|ASCII:%c)\n", str[i], (char)str[i]);
      break;
    }
    const flt_t glyph_x0 = (glyph->x0 * scale) + x;
    const flt_t glyph_x1 = (glyph->x1 * scale) + x;
    const flt_t glyph_y0 = (glyph->y0 * scale) + y;
    const flt_t glyph_y1 = (glyph->y1 * scale) + y;

    const size_t          index_offset = (fnt->chars_drawn + iter) * 4;
    ctext_glyph_vertex_t* v_out        = drawcall->vertices + ((glyph_iter + iter) * 4);

    // clang-format off
    v_out[0] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y0, zpos}, (vec2f){glyph->l, glyph->b} };
    v_out[1] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y0, zpos}, (vec2f){glyph->r, glyph->b} };
    v_out[2] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y1, zpos}, (vec2f){glyph->r, glyph->t} };
    v_out[3] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y1, zpos}, (vec2f){glyph->l, glyph->t} };
    // clang-format on

    u32* i_out = drawcall->indices + ((glyph_iter + iter) * 6);
    i_out[0]   = index_offset;
    i_out[1]   = index_offset + 1;
    i_out[2]   = index_offset + 2;
    i_out[3]   = index_offset + 2;
    i_out[4]   = index_offset + 3;
    i_out[5]   = index_offset;

    x += glyph->advance * scale;
    iter++;
  }

  return iter;
}

static inline size_t
_ctext_get_effective_length(const char* buf, size_t buflen)
{
  size_t len = 0;
  for (size_t i = 0; i < buflen; i++)
  {
    const char c = buf[i];
    // I've tried to use isprint here
    // It causes some weird artefacts for some damned reason.
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
    {
      len++;
    }
  }
  return len;
}

static inline int
_ctext_gen_vertices(cfont_t* fnt, ctext_drawcall_t* drawcall, const ctext_text_render_info_t* pInfo, const char* str)
{
  if (!str || *str == 0) // nv_strlen == 0
  {
    return 1;
  }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return -1;
  }

  nv_list_t lines;

  flt_t text_w = 0.0f;
  flt_t text_h = 0.0f;
  flt_t scale  = 0.0f;
  flt_t ypos   = 0.0f;
  flt_t xpos   = 0.0f;

  const size_t old_chars_drawn    = fnt->chars_drawn;
  size_t       actual_chars_drawn = 0;

  lines = split_string_by_lines(str);
  if (nv_list_size(&lines) == 0)
  {
    return 0;
  }

  scale = pInfo->scale;
  ctext_get_text_size(fnt, str, &text_w, NULL);
  text_h = -fnt->line_height * ((flt_t)nv_list_size(&lines) - 1.0f);

  if (pInfo->scale_for_fit)
  {
    flt_t scale_x = (pInfo->bbox.x) / text_w;
    flt_t scale_y = (pInfo->bbox.y) / text_h;
    // multiply with normal scale to get new scale
    scale *= NV_MIN(scale_x, scale_y);
  }
  drawcall->scale = scale;

  text_w *= scale;
  text_h *= scale;

  ypos = pInfo->position.y;
  switch (pInfo->vertical)
  {
    case CTEXT_VERT_ALIGN_CENTER: ypos += (text_h + fnt->line_height * scale) / 2.0f; break;
    case CTEXT_VERT_ALIGN_BOTTOM: ypos += text_h; break;
    case CTEXT_VERT_ALIGN_TOP: ypos += fnt->line_height * scale; break;
    default: nv_log_error("Invalid vertical alignment. Specified (int)%u. (Implement?)\n", pInfo->vertical); break;
  }
  for (size_t i = 0; i < nv_list_size(&lines); i++, ypos += fnt->line_height * scale)
  {
    // render_line returns the number of chars DRAWN. not the number of
    // characters in the string.
    const char* line = ((char**)nv_list_data(&lines))[i];
    if (!*line)
    {
      continue;
    }

    ctext_get_text_size(fnt, line, &text_w, &text_h);
    text_w *= scale;

    xpos = pInfo->position.x;
    switch (pInfo->horizontal)
    {
      case CTEXT_HORI_ALIGN_CENTER: xpos -= text_w / 2.0f; break;
      case CTEXT_HORI_ALIGN_RIGHT: xpos -= text_w; break;
      case CTEXT_HORI_ALIGN_LEFT: break;
      default:
        __builtin_unreachable();
        nv_log_error("Invalid horizontal alignment. Specified (int)%u. (Implement?)\n", pInfo->horizontal);
        break;
    }
    actual_chars_drawn = fnt->chars_drawn - old_chars_drawn;
    fnt->chars_drawn += _ctext_render_line(
        fnt,
        line,
        drawcall,
        scale,
        pInfo->position.z,
        // This gives us the number of characters this function call
        // has drawn. only this call.
        NV_MAX(actual_chars_drawn, 0),
        xpos,
        ypos);
  }

  for (size_t i = 0; i < nv_list_size(&lines); i++)
  {
    char* line = ((char**)nv_list_data(&lines))[i];
    nv_free(line);
  }
  nv_list_destroy(&lines);

  return 0;
}

// TODO: Replace with a better system
// that renders the characters all at once.
static inline void
_ctext_render_and_submit_drawcall(cfont_t* fnt, const ctext_text_render_info_t* pInfo, char* buffer, size_t buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    nv_log_error("Broken font\n");
    return;
  }

  size_t effective_length = _ctext_get_effective_length(buffer, buffer_size);
  if (effective_length == 0)
  {
    return;
  }

  const size_t vertex_count    = effective_length * 4;
  const size_t index_count     = effective_length * 6;
  const size_t allocation_size = (vertex_count * sizeof(ctext_glyph_vertex_t)) + (index_count * sizeof(u32));

  void* allocation = nv_calloc(allocation_size);

  ctext_drawcall_t drawcall = nv_zero_init(ctext_drawcall_t);
  drawcall.vertices         = (ctext_glyph_vertex_t*)allocation;
  drawcall.index_offset     = (vertex_count * sizeof(ctext_glyph_vertex_t));
  drawcall.indices          = (u32*)((uchar*)allocation + drawcall.index_offset);

  drawcall.color = pInfo->color;
  drawcall.scale = pInfo->scale;
  drawcall.model = pInfo->model;

  drawcall.vertex_count = effective_length * 4;
  drawcall.index_count  = effective_length * 6;

  // can we not have a bounds check before making the vertices?
  // probably not..
  if (_ctext_gen_vertices(fnt, &drawcall, pInfo, buffer) != 0)
  {
    nv_free(allocation);
    return;
  }

  nv_list_push_back(&fnt->drawcalls, &drawcall);
}

void
ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...)
{
  if (!fnt || !pInfo || !fmt)
  {
    return;
  }

  // if (!nv_async_is_task_complete(&fnt->load_task)) { return; }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  char stack[1024];

  char*  buffer      = NULL;
  size_t buffer_size = 0;

  va_list args;
  va_start(args, fmt);

  buffer_size = nv_vsnprintf(args, NULL, SIZE_MAX, fmt);

  if (buffer_size < 1024)
  {
    buffer = nv_malloc(buffer_size + 1);
  }
  else
  {
    buffer = stack;
  }
  nv_assert(buffer != NULL);

  va_end(args);
  va_start(args, fmt);

  nv_vsnprintf(args, buffer, buffer_size, fmt);
  buffer[buffer_size] = 0;

  va_end(args);

  _ctext_render_and_submit_drawcall(fnt, pInfo, buffer, buffer_size);

  nv_free(buffer);

  fnt->rendered_this_frame = 1;
}

static inline void
_ctext_upload_vertices_and_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  u32 total_vertex_byte_size = 0;
  u32 total_index_count      = 0;

  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);
    total_vertex_byte_size += drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    total_index_count += drawcall->index_count;
  }

  if (total_vertex_byte_size == 0 || total_index_count == 0)
  {
    return;
  }

  const u32 total_index_byte_size = total_index_count * sizeof(u32);
  const u32 total_buffer_size     = total_index_byte_size + total_vertex_byte_size;

  const bool fnt_buffer_resized = _ctext_font_resize_buffer(rd->nvvkctx, fnt, total_buffer_size);

  if (fnt->to_render && !fnt_buffer_resized)
  {
    _ctext_render_drawcalls(rd, fnt);
  }

  uint8_t* write_cache = (uint8_t*)nv_calloc(total_buffer_size);
  if (write_cache == NULL)
  {
    return;
  }

  // this may be dumb but I am too

  u32 vertex_copy_iterator = 0;
  u32 index_copy_iterator  = 0;
  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);
    nv_memcpy(write_cache + vertex_copy_iterator, drawcall->vertices, drawcall->vertex_count * sizeof(ctext_glyph_vertex_t));
    nv_memcpy(write_cache + total_vertex_byte_size + index_copy_iterator, drawcall->indices, drawcall->index_count * sizeof(u32));
    vertex_copy_iterator += drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    index_copy_iterator += drawcall->index_count * sizeof(u32);
  }

  fnt->index_buffer_offset = total_vertex_byte_size;
  fnt->index_count         = total_index_count;
  fnt->to_render           = true;

  nv_gpu_buffer_write_data(&fnt->staging_buffer, write_cache, total_buffer_size, 0);
  nv_gpu_buffer_flush_mapped_memory(&fnt->staging_buffer);

  nv_gpu_buffer_copy(&fnt->buffer, &fnt->staging_buffer, total_buffer_size, 0, 0);

  nv_free(write_cache);
}

void
_ctext_flush_font(nv_renderer_t* rd, cfont_t* fnt)
{
  if (!fnt->rendered_this_frame)
  {
    return;
  }
  else
  {
    fnt->rendered_this_frame = 0;
  }

  _ctext_upload_vertices_and_render_drawcalls(rd, fnt);
  fnt->chars_drawn = 0;

  for (size_t i = 0; i < nv_list_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->drawcalls, i);
    if (drawcall && drawcall->vertices)
    {
      nv_free(drawcall->vertices);
    }
  }
  nv_list_clear(&fnt->drawcalls);
}

void
ctext_flush_renders(nv_renderer_t* rd)
{
  for (size_t i = 0; i < nv_list_size(&rd->ctext->fonts); i++)
  {
    cfont_t* fnt = *(cfont_t**)nv_list_get(&rd->ctext->fonts, i);
    _ctext_flush_font(rd, fnt);
  }
}

nv_error
ctext_init(struct nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);

  rd->ctext = nv_calloc(sizeof(nv_ctext_module));
  nv_assert_else_return(rd->ctext != NULL, NV_ERROR_MALLOC_FAILED);

  nv_ctext_module* ctext = rd->ctext;

  nv_error code = NV_ERROR_SUCCESS;

  if ((code = nv_list_init(sizeof(cfont_t*), 4, nv_allocator_c, NULL, &ctext->fonts)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = nv_list_init(sizeof(ctext_label_t), 4, nv_allocator_c, NULL, &ctext->labels)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  nv_assert_else_return(nv_list_is_valid(&ctext->fonts), NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_list_is_valid(&ctext->labels), NV_ERROR_BROKEN_STATE);

  const VkDescriptorSetLayoutBinding bindings[] = {
    // binding; descriptorType; descriptorCount; stageFlags;
    // pImmutableSamplers;
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CTEXT_MAX_FONT_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
  };

  nv_allocate_descriptor_set(rd->nvvkctx, &g_pool, bindings, nv_arrlen(bindings), &ctext->desc_set);
  nv_assert_else_return(ctext->desc_set != NULL, NV_ERROR_INVALID_RETVAL);

  nv_assert_else_return(nv_sprite_get_sampler(&rd->sprite_empty) != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_sprite_get_vk_image_view(&rd->sprite_empty) != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  const VkDescriptorImageInfo empty_img_info = {
    .sampler     = nv_sprite_get_sampler(&rd->sprite_empty),
    .imageView   = nv_sprite_get_vk_image_view(&rd->sprite_empty),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet write_set = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = ctext->desc_set->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &empty_img_info,
  };
  for (size_t i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    write_set.dstArrayElement = i;
    if (nv_descriptor_set_submit_write(rd->nvvkctx, ctext->desc_set, &write_set) != NV_ERROR_SUCCESS)
    {
      return NV_ERROR_INVALID_RETVAL;
    }
  }

  return NV_ERROR_SUCCESS;
}

void
ctext_shutdown(struct nv_renderer_t* rd)
{
  if (!rd || !rd->ctext)
  {
    return;
  }
  nv_list_destroy(&rd->ctext->fonts);
  nv_list_destroy(&rd->ctext->labels);
  nv_free(rd->ctext);
}

flt_t
ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox)
{
  if (!fnt || !str || ctext_validate_font(fnt) != 0)
  {
    return 0.0F;
  }

  flt_t width, height;
  ctext_get_text_size(fnt, str, &width, &height);

  flt_t scale_x = bbox.x / width;
  flt_t scale_y = bbox.y / height;
  return fminf(scale_x, scale_y);
}

// ctext ^^

// nv_descriptors vv
int
nv_descriptor_set_submit_write(nvvk_ctx_t* nvvkctx, nv_descriptor_set_t* set, const VkWriteDescriptorSet* write)
{
  nv_assert_else_return(nvvkctx != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(write != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(set != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(set->writes != NULL, NV_ERROR_BROKEN_STATE);

  set->writes = nv_realloc(set->writes, (set->nwrites + 1) * sizeof(VkWriteDescriptorSet));
  nv_assert_else_return(set->writes != NULL, NV_ERROR_MALLOC_FAILED);

  if (!nv_memcpy(&set->writes[set->nwrites], write, sizeof(VkWriteDescriptorSet)))
  {
    return NV_ERROR_EXTERNAL;
  }
  set->nwrites++;
  vkUpdateDescriptorSets(nvvkctx->device, 1, write, 0, 0);
  return 0;
}

void
nv_descriptor_set_destroy(nvvk_ctx_t* nvvkctx, nv_descriptor_set_t* set)
{
  vkDestroyDescriptorSetLayout(nvvkctx->device, set->layout, &nvvkctx->vkalloc);
  nv_free(set->writes);
  nv_free(set);
}

void
nv_descriptor_pool_destroy(nvvk_ctx_t* nvvkctx, nv_descriptor_pool_t* pool)
{
  for (size_t i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_destroy(nvvkctx, pool->sets[i]);
  }
  nv_free(pool->sets);
  vkDestroyDescriptorPool(nvvkctx->device, pool->pool, &nvvkctx->vkalloc);
}

int
_nv_descriptor_pool_allocate(nvvk_ctx_t* nvvkctx, nv_descriptor_pool_t* pool)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);

  VkDescriptorPoolSize allocations[11]     = { 0 };
  size_t               descriptors_written = 0;

  for (size_t i = 0; i < 11; i++)
  {
    if (pool->descriptors[i].capacity == 0)
    {
      continue;
    }
    allocations[descriptors_written] = (VkDescriptorPoolSize){ pool->descriptors[i].type, pool->descriptors[i].capacity };
    descriptors_written++;
  }

  if (descriptors_written == 0)
  {
    return 0;
  }

  bool need_more_max_sets = (pool->nsets + 1) > pool->max_child_sets;
  if (need_more_max_sets)
  {
    pool->max_child_sets = NV_MAX(pool->max_child_sets * 2, 1);
  }

  VkDescriptorPoolCreateInfo poolInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = pool->max_child_sets, .poolSizeCount = descriptors_written, .pPoolSizes = allocations
  };

  VkDescriptorPool new_pool;
  nvvk_result_check(*nvvkctx, vkCreateDescriptorPool(nvvkctx->device, &poolInfo, &nvvkctx->vkalloc, &new_pool));
  if (!new_pool)
  {
    return -1;
  }

  VkDescriptorSet* new_sets = nv_malloc(sizeof(VkDescriptorSet) * NV_MAX(pool->nsets, 1));
  nv_assert_else_return(new_sets != NULL, NV_ERROR_MALLOC_FAILED);

  if (pool->nsets > 0)
  {
    VkDescriptorSetLayout* layouts = nv_malloc(sizeof(VkDescriptorSetLayout) * pool->nsets);
    nv_assert_else_return(layouts != NULL, NV_ERROR_MALLOC_FAILED);

    for (size_t i = 0; i < pool->nsets; i++)
    {
      if (pool->sets[i]->layout == VK_NULL_HANDLE)
      {
        i = NV_MAX(i - 1, 0);
        continue;
      }
      layouts[i] = pool->sets[i]->layout;
    }

    VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
    setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool              = new_pool;
    setAllocInfo.descriptorSetCount          = pool->nsets;
    setAllocInfo.pSetLayouts                 = layouts;
    nvvk_result_check(*nvvkctx, vkAllocateDescriptorSets(nvvkctx->device, &setAllocInfo, new_sets));
    nv_assert_else_return(new_sets != NULL, NV_ERROR_INVALID_RETVAL);

    nv_free(layouts);
  }

  size_t ncopies = 0;
  for (size_t i = 0; i < pool->nsets; i++)
  {
    ncopies += pool->sets[i]->nwrites;
  }
  VkCopyDescriptorSet* copies = nv_malloc(sizeof(VkCopyDescriptorSet) * NV_MAX(ncopies, 1));
  nv_assert_else_return(copies != NULL, NV_ERROR_MALLOC_FAILED);

  nv_assert(pool->sets != NULL);

  ncopies = 0;
  for (size_t i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_t* old_set = pool->sets[i];

    for (size_t writei = 0; writei < old_set->nwrites; writei++)
    {
      if (new_sets[i] == VK_NULL_HANDLE)
      {
        continue;
      }

      VkWriteDescriptorSet* write = &old_set->writes[writei];
      copies[ncopies]             = (VkCopyDescriptorSet){
                    .sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
                    .srcSet          = old_set->set,
                    .srcBinding      = write->dstBinding,
                    .srcArrayElement = write->dstArrayElement,
                    .dstSet          = new_sets[i],
                    .dstBinding      = write->dstBinding,
                    .dstArrayElement = write->dstArrayElement,
                    .descriptorCount = write->descriptorCount,
      };
      ncopies++;
    }
    old_set->set = new_sets[i];
  }
  vkUpdateDescriptorSets(nvvkctx->device, 0, NULL, ncopies, copies);

  if (pool->pool)
  {
    vkDestroyDescriptorPool(nvvkctx->device, pool->pool, &nvvkctx->vkalloc);
  }
  pool->pool = new_pool;

  if (new_sets)
  {
    nv_free(new_sets);
  }
  if (copies)
  {
    nv_free(copies);
  }
  return 0;
}

int
nv_descriptor_pool_init(nvvk_ctx_t* nvvkctx, nv_descriptor_pool_t* dst)
{
  *dst                                 = nv_zero_init(nv_descriptor_pool_t);
  nv_descriptor_pool_size pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, 0 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 0, 0 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 0, 0 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0, 0 },
  };
  nv_memcpy(dst->descriptors, pool_sizes, sizeof(pool_sizes));
  dst->sets           = nv_malloc(sizeof(nv_descriptor_set_t*));
  dst->max_child_sets = 1;
  dst->nsets          = 0;
  if (_nv_descriptor_pool_allocate(nvvkctx, dst) != 0)
  {
    return -1;
  }
  return 0;
}

int
nv_allocate_descriptor_set(nvvk_ctx_t* nvvkctx, nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst)
{
  nv_assert_else_return(pool != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(bindings != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nbindings > 0, NV_ERROR_INVALID_ARG);

  bool need_realloc = 0;
  for (size_t i = 0; i < 11; i++)
  {
    for (int j = 0; j < nbindings; j++)
    {
      nv_descriptor_pool_size* descriptor = &pool->descriptors[i];
      if (descriptor->type == bindings[j].descriptorType)
      {
        descriptor->capacity = NV_MAX(descriptor->capacity * 2, (int)bindings[j].descriptorCount + descriptor->capacity);
        need_realloc         = 1;
      }
    }
  }

  if (need_realloc || ((pool->nsets + 1) > pool->max_child_sets))
  {
    if ((pool->nsets + 1) > pool->max_child_sets)
    {
      pool->max_child_sets = NV_MAX(pool->max_child_sets * 2, 1);
      pool->sets           = nv_realloc(pool->sets, pool->max_child_sets * sizeof(nv_descriptor_set_t));
      nv_assert_else_return(pool->sets != NULL, NV_ERROR_MALLOC_FAILED);
    }

    _nv_descriptor_pool_allocate(nvvkctx, pool);
  }

  nv_descriptor_set_t* set = nv_calloc(sizeof(nv_descriptor_set_t));
  nv_assert_else_return(set != NULL, NV_ERROR_MALLOC_FAILED);

  pool->sets[pool->nsets] = set;
  pool->nsets++;

  (*dst) = set;

  set->pool   = pool;
  set->writes = nv_malloc(sizeof(VkWriteDescriptorSet));
  nv_assert_else_return(set->writes != NULL, NV_ERROR_MALLOC_FAILED);

  VkDescriptorSetLayoutCreateInfo layoutinfo = nv_zero_init(VkDescriptorSetLayoutCreateInfo);
  layoutinfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutinfo.pBindings                       = bindings;
  layoutinfo.bindingCount                    = nbindings;
  nvvk_result_check(*nvvkctx, vkCreateDescriptorSetLayout(nvvkctx->device, &layoutinfo, &nvvkctx->vkalloc, &set->layout));
  nv_assert_else_return(set->layout != VK_NULL_HANDLE, NV_ERROR_EXTERNAL);

  VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
  setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAllocInfo.descriptorPool              = pool->pool;
  setAllocInfo.descriptorSetCount          = 1;
  setAllocInfo.pSetLayouts                 = &set->layout;
  nvvk_result_check(*nvvkctx, vkAllocateDescriptorSets(nvvkctx->device, &setAllocInfo, &set->set));
  if (set->set == VK_NULL_HANDLE)
  {
    return -1;
  }

  return 0;
}
// nv_descriptors ^^

static inline nv_error
__BakeUnlitPipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
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
  nvvk_result_check(*rd->nvvkctx, vkCreateDescriptorSetLayout(rd->nvvkctx->device, &layoutinfo, &rd->nvvkctx->vkalloc, &g_Pipelines.unlit.descriptor_layout));

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "Unlit/vert", &vertex) == 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "Unlit/frag", &fragment) == 0, NV_ERROR_EXTERNAL);

  nv_assert(vertex != NULL && fragment != NULL);

  const nvsm_shader_t*        shaders[] = { vertex, fragment };
  const VkDescriptorSetLayout layouts[] = { camera.sets->layout, g_Pipelines.unlit.descriptor_layout };

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);

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

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.format                      = rd->swap_chain_image_format;
  pc.subpass                     = 0;
  pc.render_pass                 = nv_renderer_get_render_pass(rd);

  pc.n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.p_attribute_descriptions = attributeDescriptions;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.p_push_constants = pushConstants;

  pc.n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.p_binding_descriptions = bindingDescriptions;

  pc.n_shaders = nv_arrlen(shaders);
  pc.p_shaders = shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.p_descriptor_layouts = layouts;

  pc.extent.width  = RenderExtent.width;
  pc.extent.height = RenderExtent.height;
  pc.samples       = rd->samples;
  nv_gpu_create_pipeline_layout(rd->nvvkctx, &pc, &g_Pipelines.unlit.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.unlit.pipeline_layout;
  nv_gpu_create_graphics_pipeline(rd->nvvkctx, &pc, &g_Pipelines.unlit.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
__BakeCtextPipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  const VkVertexInputAttributeDescription attributeDescriptions[] = {
    // location; binding; format; offset;
    { 0, 0, nv_format_to_vk_format(NOVA_FORMAT_RGB32), 0 },            // pos
    { 1, 0, nv_format_to_vk_format(NOVA_FORMAT_RG32), sizeof(vec3f) }, // uv
  };

  const VkVertexInputBindingDescription bindingDescriptions[] = {
    // binding; stride; inputRate
    { 0, sizeof(vec3f) + sizeof(vec2f), VK_VERTEX_INPUT_RATE_VERTEX },
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "ctext/vert", &vertex) == 0, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "ctext/frag", &fragment) == 0, NV_ERROR_EXTERNAL);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.sets->layout, rd->ctext->desc_set->layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.format                      = rd->swap_chain_image_format;
  pc.subpass                     = 0;
  pc.render_pass                 = nv_renderer_get_render_pass(rd);

  pc.n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.p_attribute_descriptions = attributeDescriptions;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.p_push_constants = pushConstants;

  pc.n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.p_binding_descriptions = bindingDescriptions;

  pc.n_shaders = nv_arrlen(shaders);
  pc.p_shaders = (const struct nvsm_shader_t* const*)shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.p_descriptor_layouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.extent.width                = RenderExtent.width;
  pc.extent.height               = RenderExtent.height;
  pc.blend_state                 = &blend;
  pc.samples                     = rd->samples;

  nv_gpu_create_pipeline_layout(rd->nvvkctx, &pc, &g_Pipelines.ctext.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.ctext.pipeline_layout;
  nv_gpu_create_graphics_pipeline(rd->nvvkctx, &pc, &g_Pipelines.ctext.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

static inline nv_error
__BakeDebugLinePipeline(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  struct line_push_constants
  {
    mat4f model;
    vec4f color;
    vec2f line_begin;
    vec2f line_end;
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert_else_return(nvsm_load_shader(ctx, "Debug/Line/vert", &vertex) == NV_SUCCESS, NV_ERROR_EXTERNAL);
  nv_assert_else_return(nvsm_load_shader(ctx, "Debug/Line/frag", &fragment) == NV_SUCCESS, NV_ERROR_EXTERNAL);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.sets->layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.format                      = rd->swap_chain_image_format;

  pc.topology    = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pc.render_pass = nv_renderer_get_render_pass(rd);

  pc.n_attribute_descriptions = 0;
  pc.p_attribute_descriptions = NULL;

  pc.n_push_constants = nv_arrlen(pushConstants);
  pc.p_push_constants = pushConstants;

  pc.n_binding_descriptions = 0;
  pc.p_binding_descriptions = NULL;

  pc.n_shaders = nv_arrlen(shaders);
  pc.p_shaders = (const struct nvsm_shader_t* const*)shaders;

  pc.n_descriptor_layouts = nv_arrlen(layouts);
  pc.p_descriptor_layouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.extent.width                = RenderExtent.width;
  pc.extent.height               = RenderExtent.height;
  pc.blend_state                 = &blend;
  pc.samples                     = rd->samples;

  nv_gpu_create_pipeline_layout(rd->nvvkctx, &pc, &g_Pipelines.line.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.line.pipeline_layout;
  nv_gpu_create_graphics_pipeline(rd->nvvkctx, &pc, &g_Pipelines.line.pipeline, 0);

  return NV_ERROR_SUCCESS;
}

nv_error
nv_vk_bake_global_pipelines(nvsm_ctx_t* ctx, nv_renderer_t* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->nvvkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(rd->render_extent.width != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_extent.height != 0, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(rd->render_pass != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(camera.sets != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(camera.sets->layout != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  nv_error code = NV_ERROR_SUCCESS;

  if ((code = __BakeUnlitPipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = __BakeDebugLinePipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  if ((code = __BakeCtextPipeline(ctx, rd)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  return NV_ERROR_SUCCESS;
}

static inline void
nv_vk_destroy_pipeline(nvvk_ctx_t* nvvkctx, nv_vk_pipeline_t* pipeline)
{
  if (!pipeline)
  {
    return;
  }

  vkDestroyPipeline(nvvkctx->device, pipeline->pipeline, &nvvkctx->vkalloc);
  vkDestroyPipelineLayout(nvvkctx->device, pipeline->pipeline_layout, &nvvkctx->vkalloc);
  vkDestroyDescriptorSetLayout(nvvkctx->device, pipeline->descriptor_layout, &nvvkctx->vkalloc);
}

void
nv_vk_destroy_global_pipelines(nvvk_ctx_t* nvvkctx)
{
  nv_vk_pipeline_t pipelines[] = {
    g_Pipelines.unlit,
    g_Pipelines.ctext,
    g_Pipelines.line,
  };
  for (size_t i = 0; i < nv_arrlen(pipelines); i++)
  {
    nv_vk_destroy_pipeline(nvvkctx, &pipelines[i]);
  }
}

void
nv_gpu_create_graphics_pipeline(nvvk_ctx_t* nvvkctx, const nv_gpu_pipeline_create_info* pCreateInfo, VkPipeline* dstPipeline, u32 flags)
{
  NVVK_REQUIRED_PTR(nvvkctx->device);
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

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount   = pCreateInfo->n_binding_descriptions,
    .pVertexBindingDescriptions      = pCreateInfo->p_binding_descriptions,
    .vertexAttributeDescriptionCount = pCreateInfo->n_attribute_descriptions,
    .pVertexAttributeDescriptions    = pCreateInfo->p_attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .topology               = pCreateInfo->topology,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewportState = {
    .x        = 0,
    .y        = 0,
    .width    = (flt_t)(pCreateInfo->extent.width),
    .height   = (flt_t)(pCreateInfo->extent.height),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D scissor = {
    .offset = (VkOffset2D){ 0, 0 },
    .extent = pCreateInfo->extent,
  };

  VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {
    .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext         = NULL,
    .flags         = 0,
    .viewportCount = 1,
    .pViewports    = &viewportState,
    .scissorCount  = 1,
    .pScissors     = &scissor,
  };

  VkPipelineRasterizationStateCreateInfo rasterizerPipelineStateCreateInfo = {
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

  VkPipelineMultisampleStateCreateInfo multisamplerPipelineStageCreateInfo = {
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
    const nv_gpu_pipeline_blend_state* blendState = pCreateInfo->blend_state;

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

  VkPipelineColorBlendStateCreateInfo colorblendState = {
    .sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext             = NULL,
    .flags             = 0,
    .logicOpEnable     = VK_FALSE,
    .logicOp           = VK_LOGIC_OP_COPY,
    .attachmentCount   = 1,
    .pAttachments      = &colorblendAttachmentState,
    .blendConstants[0] = 0.0f,
    .blendConstants[1] = 0.0f,
    .blendConstants[2] = 0.0f,
    .blendConstants[3] = 0.0f,
  };

  VkPipelineShaderStageCreateInfo* shader_infos = (VkPipelineShaderStageCreateInfo*)nv_calloc(pCreateInfo->n_shaders * sizeof(VkPipelineShaderStageCreateInfo));
  for (size_t i = 0; i < pCreateInfo->n_shaders; i++)
  {
    if (!pCreateInfo->p_shaders[i] || pCreateInfo->p_shaders[i]->module == VK_NULL_HANDLE)
    {
      continue;
    }

    const VkShaderStageFlagBits stage = (VkShaderStageFlagBits)_nvsm_shader_stage_from_string(pCreateInfo->p_shaders[i]->stage);
    if ((int)stage == -1)
    {
      nv_log_error("Error in parsing stage for shader %i(%p): \"%s\"", i, pCreateInfo->p_shaders[i], pCreateInfo->p_shaders[i]->stage);
      continue;
    }

    shader_infos[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_infos[i].stage  = stage;
    shader_infos[i].module = (VkShaderModule)pCreateInfo->p_shaders[i]->module;
    shader_infos[i].pName  = "main";
  }

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {
    .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount          = pCreateInfo->n_shaders,
    .pStages             = shader_infos,
    .pVertexInputState   = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState      = &viewportStateCreateInfo,
    .pRasterizationState = &rasterizerPipelineStateCreateInfo,
    .pMultisampleState   = &multisamplerPipelineStageCreateInfo,
    .pColorBlendState    = &colorblendState,
    .layout              = pCreateInfo->pipeline_layout,
    .renderPass          = pCreateInfo->render_pass,
    .subpass             = pCreateInfo->subpass,
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

  nvvk_result_check(*nvvkctx, vkCreateGraphicsPipelines(nvvkctx->device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, &nvvkctx->vkalloc, dstPipeline));

  nv_free(shader_infos);
}

void
nv_gpu_create_render_pass(nvvk_ctx_t* nvvkctx, nv_gpu_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags)
{
  NVVK_REQUIRED_PTR(nvvkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstRenderPass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    NVVK_NOT_EQUAL_TO(pCreateInfo->depth_buffer_format, NOVA_FORMAT_UNDEFINED);
  }

  VkAttachmentDescription colorAttachmentDescription = {
    .flags          = 0,
    .format         = nv_format_to_vk_format(pCreateInfo->format),
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
      .format         = nv_format_to_vk_format(pCreateInfo->depth_buffer_format),
      .samples        = pCreateInfo->samples,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    // if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING))
    // 	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // else
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    depthAttachmentRef.attachment = nv_list_size(&attachments);
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
      .format         = nv_format_to_vk_format(pCreateInfo->format),
      .samples        = VK_SAMPLE_COUNT_1_BIT,
      .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    colorAttachmentResolveRef.attachment = nv_list_size(&attachments);
    colorAttachmentResolveRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    nv_list_push_back(&attachments, &colorAttachmentResolve);

    subpass.pResolveAttachments = &colorAttachmentResolveRef;
  }

  VkRenderPassCreateInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext           = NULL,
    .flags           = 0,
    .attachmentCount = nv_list_size(&attachments),
    .pAttachments    = (const VkAttachmentDescription*)nv_list_data(&attachments),
    .subpassCount    = 1,
    .pSubpasses      = &subpass,
    .dependencyCount = 0,
    .pDependencies   = NULL,
  };
  nvvk_result_check(*nvvkctx, vkCreateRenderPass(nvvkctx->device, &renderPassInfo, &nvvkctx->vkalloc, dstRenderPass));

  nv_list_destroy(&attachments);
}

void
nv_gpu_create_pipeline_layout(nvvk_ctx_t* nvvkctx, nv_gpu_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout)
{
  NVVK_REQUIRED_PTR(nvvkctx->device);
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

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = pCreateInfo->n_descriptor_layouts,
    .pSetLayouts            = pCreateInfo->p_descriptor_layouts,
    .pushConstantRangeCount = pCreateInfo->n_push_constants,
    .pPushConstantRanges    = pCreateInfo->p_push_constants,
  };
  nvvk_result_check(*nvvkctx, vkCreatePipelineLayout(nvvkctx->device, &pipelineLayoutCreateInfo, &nvvkctx->vkalloc, dstLayout));
}

static inline const char*
_nv_gpu_present_mode_to_string(VkPresentModeKHR present_mode)
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
nv_gpu_create_swapchain(nvvk_ctx_t* nvvkctx, nv_gpu_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain)
{
  NVVK_REQUIRED_PTR(nvvkctx->device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.height, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->image_count, 0);

  /* Used to check for errors or unavailable settings */
  /* These are the variables passed to the create function*/
  VkPresentModeKHR   present_mode   = pCreateInfo->present_mode;
  VkSurfaceFormatKHR surface_format = (VkSurfaceFormatKHR){ nv_format_to_vk_format(pCreateInfo->format), pCreateInfo->color_space };

  uchar buffer[1024];

  nv_alloc_estack_t stack = nv_zero_init(nv_alloc_estack_t);
  stack.buffer            = buffer;
  stack.buffer_size       = sizeof(buffer);

  u32 present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(nvvkctx->phys_device, nvvkctx->surface, &present_mode_count, NULL);
  VkPresentModeKHR* present_modes = nv_allocator_estack(&stack, NULL, NV_ALLOC_NEW_BLOCK, present_mode_count * sizeof(VkPresentModeKHR));
  nv_assert_else_return(present_modes != NULL, );
  vkGetPhysicalDeviceSurfacePresentModesKHR(nvvkctx->phys_device, nvvkctx->surface, &present_mode_count, present_modes);

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
    nv_log_warning(
        "Present mode %s unavailable. Using %s.\n", _nv_gpu_present_mode_to_string(pCreateInfo->present_mode), _nv_gpu_present_mode_to_string(fallback_present_mode));
    present_mode = fallback_present_mode;
  }

  u32 surface_format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(nvvkctx->phys_device, nvvkctx->surface, &surface_format_count, NULL);
  VkSurfaceFormatKHR* surface_formats = nv_allocator_estack(&stack, NULL, NV_ALLOC_NEW_BLOCK, sizeof(VkSurfaceFormatKHR) * surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(nvvkctx->phys_device, nvvkctx->surface, &surface_format_count, surface_formats);

  const VkSurfaceFormatKHR* fallback = &surface_formats[0];

  bool found_surface_format = false;
  for (u32 i = 0; i < surface_format_count; i++)
  {
    const VkSurfaceFormatKHR* vk_surface_format = &surface_formats[i];
    const VkFormat            vk_format         = nv_format_to_vk_format(pCreateInfo->format);

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

  VkSwapchainCreateInfoKHR swapChainCreateInfo = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext                 = NULL,
    .flags                 = 0,
    .surface               = nvvkctx->surface,
    .minImageCount         = pCreateInfo->image_count,
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
  nvvk_result_check(*nvvkctx, vkCreateSwapchainKHR(nvvkctx->device, &swapChainCreateInfo, &nvvkctx->vkalloc, dstSwapchain));
}

nv_gpu_pipeline_blend_state
nv_gpu_init_pipeline_blend_state(nv_gpu_pipeline_blend_preset preset)
{
  nv_gpu_pipeline_blend_state ret = nv_zero_init(nv_gpu_pipeline_blend_state);
  ret.color_write_mask            = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

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
    nvvk_ctx_t*           nvvkctx,
    size_t                size,
    VkBufferUsageFlags    usageFlags,
    VkMemoryPropertyFlags propertyFlags,
    VkBuffer*             dstBuffer,
    VkDeviceMemory*       retMem,
    bool                  externallyAllocated)
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
  nvvk_result_check(*nvvkctx, vkCreateBuffer(nvvkctx->device, &bufferCreateInfo, &nvvkctx->vkalloc, &newBuffer));

  VkMemoryRequirements bufferMemoryRequirements;
  vkGetBufferMemoryRequirements(nvvkctx->device, newBuffer, &bufferMemoryRequirements);

  if (!externallyAllocated)
  {
    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = bufferMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(nvvkctx, bufferMemoryRequirements.memoryTypeBits, propertyFlags);
    nvvk_result_check(*nvvkctx, vkAllocateMemory(nvvkctx->device, &allocInfo, &nvvkctx->vkalloc, &newMemory));

    nvvk_result_check(*nvvkctx, vkBindBufferMemory(nvvkctx->device, newBuffer, newMemory, 0));
    *retMem = newMemory;
  }

  *dstBuffer = newBuffer;
}

void
nv_vk_stage_buffer_transfer(nvvk_driver_t* driver, VkBuffer dst, void* data, size_t size)
{
  nv_gpu_buffer_t staging_buffer;
  nv_gpu_buffer_init(driver, size, 4, NV_GPU_BUFFER_MAPPABLE | NV_GPU_BUFFER_TRANSIENT_BIT, &staging_buffer);

  nv_gpu_buffer_write_data(&staging_buffer, data, size, 0);
  nv_gpu_buffer_flush_writes(&staging_buffer);

  const VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = size };

  VkCommandBuffer cmd = nv_vk_begin_command_buffer(driver);
  vkCmdCopyBuffer(cmd, staging_buffer.buffer, dst, 1, &copy);
  nvvk_result_check(*driver->ctx, nv_vk_end_command_buffer(driver, cmd, driver->ctx->transfer_queue, 1));

  nv_gpu_buffer_destroy(&staging_buffer);
}

u32
nv_vk_get_mem_type(nvvk_ctx_t* nvvkctx, const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties)
{
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(nvvkctx->phys_device, &properties);

  for (u32 i = 0; i < properties.memoryTypeCount; i++)
  {
    if ((memoryTypeBits & (1 << i)) && (properties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties)
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

static inline void
_nv_update_command_buffers_status(nvvk_ctx_t* nvvkctx)
{
  for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    const VkResult result = vkGetFenceStatus(nvvkctx->device, nvvkctx->cmd_buffer_fences[i]);

    if (result == VK_SUCCESS)
    {
      vkResetFences(nvvkctx->device, 1, &nvvkctx->cmd_buffer_fences[i]);
    }

    /* If the buffer is in use, then the fence will be waiting and so result will be VK_NOT_READY */
    nvvkctx->cmd_buffers_in_use[i] = (result != VK_NOT_READY);
  }
}

VkCommandBuffer
nv_vk_begin_command_buffer(nvvk_driver_t* driver)
{
  nvvk_ctx_t* nvvkctx = driver->ctx;

  if (nvvkctx->cmd_pool == VK_NULL_HANDLE)
  {
    VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
    cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.queueFamilyIndex        = nvvkctx->graphics_family_index;
    cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    nvvk_result_check(*nvvkctx, vkCreateCommandPool(nvvkctx->device, &cmdPoolCreateInfo, &nvvkctx->vkalloc, &nvvkctx->cmd_pool));

    VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
    cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount          = NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT;
    cmdAllocInfo.commandPool                 = nvvkctx->cmd_pool;
    nvvk_result_check(*nvvkctx, vkAllocateCommandBuffers(nvvkctx->device, &cmdAllocInfo, nvvkctx->cmd_buffers));

    VkFenceCreateInfo fence_create_info = (VkFenceCreateInfo){
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
    };
    for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
    {
      nvvk_result_check(*nvvkctx, vkCreateFence(nvvkctx->device, &fence_create_info, &nvvkctx->vkalloc, &nvvkctx->cmd_buffer_fences[i]));
    }
  }

  _nv_update_command_buffers_status(nvvkctx);

  VkCommandBuffer free_cmd_buffer = VK_NULL_HANDLE;

  for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    if (nvvkctx->cmd_buffers_in_use[i] == false)
    {
      free_cmd_buffer = nvvkctx->cmd_buffers[i];
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
nv_vk_end_command_buffer(nvvk_driver_t* driver, VkCommandBuffer cmd, VkQueue queue, bool waitForExecution)
{
  nvvk_ctx_t* nvvkctx = driver->ctx;

  size_t cmd_index = SIZE_MAX;
  for (size_t i = 0; i < NOVA_GPU_COMMAND_BUFFER_CACHE_COUNT; i++)
  {
    if (nvvkctx->cmd_buffers[i] == cmd)
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

  VkFence fence = nvvkctx->cmd_buffer_fences[cmd_index];
  nv_assert_else_return(fence != VK_NULL_HANDLE, VK_ERROR_UNKNOWN);

  res = vkQueueSubmit(queue, 1, &submitInfo, fence);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  if (waitForExecution)
  {
    res = vkWaitForFences(driver->ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (res != VK_SUCCESS)
    {
      return res;
    }
    vkResetFences(driver->ctx->device, 1, &fence);
  }

  _nv_update_command_buffers_status(nvvkctx);

  return VK_SUCCESS;
}

void
nv_vk_load_binary_file(const char* path, u8* dst, u32* dstSize)
{
  nv_assert(path != NULL);
  nv_assert(dstSize != NULL);

  FILE* f         = NULL;
  long  file_size = -1;

  f = fopen(path, "rb");
  if (!f)
  {
    nv_log_error("fopen error: %s\n", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fseek(f, 0, SEEK_END) != 0)
  {
    nv_log_error("fseek error: %s\n", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  file_size = ftell(f);

  if (dst == NULL)
  {
    goto CLEANUP_AND_RETURN;
  }

  rewind(f);
  if (errno != 0)
  {
    nv_log_error("error in rewind?? %s\n", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fread(dst, file_size, 1, f) != 1)
  {
    nv_log_error("fread error %s\n", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

CLEANUP_AND_RETURN:
  *dstSize = file_size;
  if (f)
    NOVA_CALL_FILE_FN(fclose(f));
}

void
nv_vk_stage_image_transfer(struct nvvk_driver_t* driver, VkImage dst, const void* data, size_t width, size_t height, size_t image_size)
{
  nvvk_ctx_t* nvvkctx = driver->ctx;

  VkBuffer       stagingBuffer       = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(nvvkctx->device, dst, &mem_req);

  const VkBufferCreateInfo stagingBufferInfo = {
    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size        = mem_req.size,
    .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  nvvk_result_check(*nvvkctx, vkCreateBuffer(nvvkctx->device, &stagingBufferInfo, &nvvkctx->vkalloc, &stagingBuffer));

  VkMemoryRequirements stagingBufferRequirements;
  vkGetBufferMemoryRequirements(nvvkctx->device, stagingBuffer, &stagingBufferRequirements);

  const VkMemoryAllocateInfo stagingBufferAllocInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize  = stagingBufferRequirements.size,
    .memoryTypeIndex = nv_vk_get_mem_type(nvvkctx, stagingBufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  nvvk_result_check(*nvvkctx, vkAllocateMemory(nvvkctx->device, &stagingBufferAllocInfo, &nvvkctx->vkalloc, &stagingBufferMemory));
  nvvk_result_check(*nvvkctx, vkBindBufferMemory(nvvkctx->device, stagingBuffer, stagingBufferMemory, 0));

  void* stagingBufferMapped;
  nvvk_result_check(*nvvkctx, vkMapMemory(nvvkctx->device, stagingBufferMemory, 0, stagingBufferRequirements.size, 0, &stagingBufferMapped));
  nv_memcpy(stagingBufferMapped, data, image_size);
  vkUnmapMemory(nvvkctx->device, stagingBufferMemory);

  const VkCommandBuffer cmd = nv_vk_begin_command_buffer(driver);

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

  VkBufferImageCopy region = {
    .bufferOffset      = 0,
    .bufferRowLength   = 0,
    .bufferImageHeight = 0,
    .imageSubresource  = (VkImageSubresourceLayers){ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
    .imageOffset       = (VkOffset3D){ 0, 0, 0 },
    .imageExtent       = (VkExtent3D){ width, height, 1 },
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

  nv_vk_end_command_buffer(driver, cmd, nvvkctx->transfer_queue, true);

  vkDestroyBuffer(nvvkctx->device, stagingBuffer, &nvvkctx->vkalloc);
  vkFreeMemory(nvvkctx->device, stagingBufferMemory, &nvvkctx->vkalloc);
}

void
nv_vk_create_texture_from_memory(struct nvvk_driver_t* driver, u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_vk_create_texture_empty(driver->ctx, width, height, format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, NULL, dst, dstMem);
  nv_vk_stage_image_transfer(driver, *dst, buffer, width, height, width * height * nv_format_get_bytes_per_pixel(format));
}

// If the format given is oki then it'll just return it
// otherwise it gives you the next best optoin
nv_format
nv_vk_get_supported_format_for_draw(nvvk_ctx_t* nvvkctx, nv_format fmt)
{
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(nvvkctx->phys_device, nv_format_to_vk_format(fmt), &formatProperties);

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
    nvvk_ctx_t*           nvvkctx,
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
  vkGetPhysicalDeviceProperties(nvvkctx->phys_device, &device_properties);

  const VkPhysicalDeviceLimits* limits = &device_properties.limits;
  nv_assert_else_return(width <= limits->maxImageDimension2D, );
  nv_assert_else_return(height <= limits->maxImageDimension2D, );

  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
  {
    format = nv_vk_get_supported_format_for_draw(nvvkctx, format);
  }

  VkImageCreateInfo imageCreateInfo = nv_zero_init(VkImageCreateInfo);
  imageCreateInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType         = VK_IMAGE_TYPE_2D;
  imageCreateInfo.extent.width      = width;
  imageCreateInfo.extent.height     = height;
  imageCreateInfo.extent.depth      = 1;
  imageCreateInfo.mipLevels         = 1;
  imageCreateInfo.arrayLayers       = 1;
  imageCreateInfo.format            = nv_format_to_vk_format(format);
  imageCreateInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage             = usage;
  imageCreateInfo.samples           = samples;
  imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(*nvvkctx, vkCreateImage(nvvkctx->device, &imageCreateInfo, &nvvkctx->vkalloc, dst));

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(nvvkctx->device, *dst, &imageMemoryRequirements);

  if (image_size)
  {
    *image_size = imageMemoryRequirements.size;
  }

  // allow for preallocated memory.
  if (dstMem != NULL)
  {
    const u32 localDeviceMemoryIndex = nv_vk_get_mem_type(nvvkctx, imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = imageMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = localDeviceMemoryIndex;

    nvvk_result_check(*nvvkctx, vkAllocateMemory(nvvkctx->device, &allocInfo, &nvvkctx->vkalloc, dstMem));
    nvvk_result_check(*nvvkctx, vkBindImageMemory(nvvkctx->device, *dst, *dstMem, 0));
  }
}

u8*
nv_vk_create_texture_from_disk(struct nvvk_driver_t* driver, const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_image_t tex = nv_zero_init(nv_image_t);

  nv_error code = nv_image_load(path, &tex);
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
nv_vk_get_supported_format(nvvk_ctx_t* nvvkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dst_format, VkColorSpaceKHR* dst_color_space)
{
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);
  NVVK_REQUIRED_PTR(dst_format);
  NVVK_REQUIRED_PTR(dst_color_space);

  u32 formatCount = 0;
  nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, VK_NULL_HANDLE));
  nv_list_t surface_formats;
  nv_list_init(sizeof(VkSurfaceFormatKHR), formatCount, nv_allocator_c, NULL, &surface_formats);
  nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, (VkSurfaceFormatKHR*)nv_list_data(&surface_formats)));

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
    *dst_format      = nv_vk_format_to_nv_format(selected_format.format);
    *dst_color_space = selected_format.colorSpace;

    return VK_TRUE;
  }

  return VK_FALSE;
}

u32
nv_vk_get_surface_image_count(nvvk_ctx_t* nvvkctx, VkPhysicalDevice phys_device, VkSurfaceKHR surface)
{
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  nvvk_result_check(*nvvkctx, vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface, &surfaceCapabilities));

  u32 requestedImageCount = surfaceCapabilities.minImageCount + 1;
  if (requestedImageCount < surfaceCapabilities.maxImageCount)
  {
    requestedImageCount = surfaceCapabilities.maxImageCount;
  }

  return requestedImageCount;
}

// NOVA_GPU_OBJECTS

// these parameters should be replaced
// properties should be replaced by usage. Like NOVA_GPU_MEMORY_USAGE_GPU,
// CPU_TO_GPU, GPU_TO_CPU, etc.
void
nv_gpu_allocate_memory(nvvk_ctx_t* nvvkctx, size_t size, nv_gpu_memory_usage usage, nv_gpu_memory_t* dst)
{
  nv_assert_else_return(size > 0, );
  nv_assert_else_return(usage != 0, );
  nv_assert_else_return(dst != NULL, );

  (*dst)                                 = nv_zero_init(nv_gpu_memory_t);
  dst->size                              = size;
  dst->usage                             = usage;
  const VkMemoryPropertyFlags properties = (VkMemoryPropertyFlags)usage;

  VkMemoryAllocateInfo alloc_info = nv_zero_init(VkMemoryAllocateInfo);
  alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize       = size;

  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(nvvkctx->phys_device, &mem_properties);

  for (u32 i = 0; i < mem_properties.memoryTypeCount; i++)
  {
    if ((properties & mem_properties.memoryTypes[i].propertyFlags) == properties)
    {
      alloc_info.memoryTypeIndex = i;
      break;
    }
  }

  nvvk_result_check(*nvvkctx, vkAllocateMemory(nvvkctx->device, &alloc_info, &nvvkctx->vkalloc, &dst->memory));
}

void
nv_gpu_map_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* memory, VkDeviceSize size, size_t offset, void** out)
{
  nv_assert(memory != NULL);
  nv_assert(out != NULL);
  nv_assert(size != 0);

  if (!(memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE))
  {
    nv_log_error("Memory usage does not have NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE \n"
                 "Use nv_gpu_write_to_buffer() instead.");
    *out = NULL;
    return;
  }
  if (memory->map_size != 0)
  {
    *out = memory->mapped;
    return;
  }
  if (vkMapMemory(nvvkctx->device, memory->memory, offset, size, 0, &memory->mapped) != VK_SUCCESS)
  {
    nv_log_error("Memory could not be mapped for write\n");
    memory->map_size   = 0;
    memory->map_offset = 0;
    memory->mapped     = NULL;
    *out               = NULL;
    return;
  }
  memory->map_size   = size;
  memory->map_offset = offset;
  *out               = memory->mapped;
}

void
nv_gpu_unmap_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* memory)
{
  if (!(memory->mapped != NULL && "Memory was not mapped"))
  {
    NULL;
  }

  if (!(memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_COHERENT))
  {
    VkMappedMemoryRange range = {
      .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = memory->memory,
      .offset = memory->map_offset,
      .size   = memory->map_size,
    };
    vkFlushMappedMemoryRanges(nvvkctx->device, 1, &range);
  }
  memory->map_offset = 0;
  memory->map_size   = 0;
  memory->mapped     = NULL;
  vkUnmapMemory(nvvkctx->device, memory->memory);
}

void
nv_gpu_free_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* mem)
{
  if (mem && mem->memory)
  {
    vkFreeMemory(nvvkctx->device, mem->memory, NULL);
  }
}

void
nv_gpu_copy_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* dst, nv_gpu_memory_t* src, size_t size, size_t dst_offset, size_t src_offset)
{
  nv_assert_else_return(dst != NULL, );
  nv_assert_else_return(src != NULL, );
  nv_assert_else_return(src->mapped == NULL, );
  nv_assert_else_return(dst->size >= size, );
  nv_assert_else_return(src->size >= size, );
  nv_assert_else_return((size + src_offset) < src->size, );
  nv_assert_else_return((size + dst_offset) < dst->size, );

  if (src->mapped)
  {
    nv_gpu_unmap_memory(nvvkctx, src);
  }

  if (dst->mapped)
  {
    nv_gpu_unmap_memory(nvvkctx, dst);
  }

  void* dst_mapped = NULL;
  void* src_mapped = NULL;
  nv_gpu_map_memory(nvvkctx, dst, size, dst_offset, &dst_mapped);
  nv_gpu_map_memory(nvvkctx, src, size, src_offset, &dst_mapped);

  nv_memmove(dst_mapped, src_mapped, size);

  nv_gpu_unmap_memory(nvvkctx, dst);
  nv_gpu_unmap_memory(nvvkctx, src);
}

// I think these given an error when, say offset is too big so maybe we can
// check their return values?
// void
// nv_gpu_bind_buffer_to_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* mem, size_t offset, nv_gpu_buffer_t* buffer)
// {
//   buffer->memory = mem;
//   buffer->offset = offset;

//   nvvk_result_check(*nvvkctx, vkBindBufferMemory(nvvkctx->device, buffer->buffer, mem->memory, offset));
// }

void
nv_gpu_texture_attach_view(nv_gpu_texture* tex, VkImageView view)
{
  tex->view = view;
}

void
nv_gpu_bind_texture_to_memory(nvvk_ctx_t* nvvkctx, nv_gpu_memory_t* mem, size_t offset, nv_gpu_texture* tex)
{
  tex->memory = mem;
  tex->offset = offset;
  nvvk_result_check(*nvvkctx, vkBindImageMemory(nvvkctx->device, tex->image, mem->memory, offset));

  VkImageAspectFlags aspect = 0;
  if (nv_format_has_depth_channel(tex->format))
  {
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  else if (nv_format_has_color_channel(tex->format))
  {
    aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  if (nv_format_has_stencil_channel(tex->format))
  {
    aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkImageSubresourceRange subresourceRange = {
    .aspectMask = aspect, .baseMipLevel = 0, .levelCount = VK_REMAINING_MIP_LEVELS, .baseArrayLayer = 0, .layerCount = VK_REMAINING_ARRAY_LAYERS
  };

  nv_format dst_format = tex->format;
  dst_format           = nv_vk_get_supported_format_for_draw(nvvkctx, dst_format);

  VkImageViewCreateInfo view_info = {
    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image            = tex->image,
    .viewType         = (VkImageViewType)tex->type,
    .format           = nv_format_to_vk_format(dst_format),
    .subresourceRange = subresourceRange,
    .components.r     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.g     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.b     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.a     = VK_COMPONENT_SWIZZLE_IDENTITY,
  };
  nvvk_result_check(*nvvkctx, vkCreateImageView(nvvkctx->device, &view_info, &nvvkctx->vkalloc, &tex->view));
}

void
nv_gpu_destroy_texture(nvvk_ctx_t* nvvkctx, nv_gpu_texture* tex)
{
  vkDeviceWaitIdle(nvvkctx->device);
  if (!tex->view)
  {
    nv_log_info("Attempt to destroy an image view which is NULL\n");
    return;
  }
  if (!tex->image)
  {
    nv_log_info("Attempt to destroy an image which is NULL\n");
    return;
  }
  vkDestroyImage(nvvkctx->device, tex->image, &nvvkctx->vkalloc);
  vkDestroyImageView(nvvkctx->device, tex->view, &nvvkctx->vkalloc);
}

// size_t
// nv_gpu_get_buffer_size(const nv_gpu_buffer_t* buffer)
// {
//   return buffer->size;
// }

// void
// nv_gpu_buffer_readback(nvvk_ctx_t* nvvkctx, const nv_gpu_buffer_t* buffer, void* dst)
// {
//   if (!(buffer->memory->usage & NOVA_GPU_BUFFER_USAGE_TRANSFER_SOURCE))
//   {
//     nv_log_error("Cannot readback from buffer that is not transfer source\n");
//     return;
//   }

//   nv_gpu_buffer_t staging;
//   nv_gpu_memory_t staging_mem;
//   nv_gpu_create_buffer(nvvkctx, buffer->size, NOVA_GPU_ALIGNMENT_UNNECESSARY, NOVA_GPU_BUFFER_USAGE_TRANSFER_DESTINATION, &staging);
//   nv_gpu_allocate_memory(nvvkctx, buffer->size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &staging_mem);
//   nv_gpu_bind_buffer_to_memory(nvvkctx, &staging_mem, 0, &staging);

//   VkCommandBuffer cmd = nv_vk_begin_command_buffer(nvvkctx);

//   VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = buffer->size };
//   vkCmdCopyBuffer(cmd, buffer->buffer, staging.buffer, 1, &copy);

//   nv_vk_end_command_buffer(nvvkctx, cmd, nvvkctx->transfer_queue, 1);

//   void* mapped = NULL;
//   nv_gpu_map_memory(nvvkctx, &staging_mem, buffer->size, 0, &mapped);
//   nv_assert(mapped != NULL);
//   nv_memcpy(dst, mapped, buffer->size);
//   nv_gpu_unmap_memory(nvvkctx, &staging_mem);

//   nv_gpu_destroy_buffer(nvvkctx, &staging);
//   nv_gpu_free_memory(nvvkctx, &staging_mem);
// }

void
nv_gpu_get_texture_size(const nv_gpu_texture* tex, size_t* w, size_t* h)
{
  if (w)
  {
    *w = tex->extent.width;
  }
  if (h)
  {
    *h = tex->extent.height;
  }
}

void
nv_gpu_create_sampler(nvvk_driver_t* driver, const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler_t** dst)
{
  nv_assert_else_return(driver != NULL, );
  nv_assert_else_return(pInfo != NULL, );
  nv_assert_else_return(dst != NULL, );

  for (size_t i = 0; i < nv_list_size(&driver->samplers); i++)
  {
    if (!nv_list_get(&driver->samplers, i))
    {
      continue;
    }

    nv_gpu_sampler_t* cache = ((nv_gpu_sampler_t*)nv_list_get(&driver->samplers, i));
    if (!cache)
    {
      continue;
    }

    if (cache != NULL && cache->filter == pInfo->filter && cache->mipmap_filter == pInfo->mipmap_filter && cache->address_mode == pInfo->address_mode
        && cache->max_anisotropy == pInfo->max_anisotropy && cache->mip_lod_bias == pInfo->mip_lod_bias && cache->min_lod == pInfo->min_lod && cache->max_lod == pInfo->max_lod
        && cache->vksampler != VK_NULL_HANDLE)
    {
      *dst = cache;
      return;
    }
  }

  nv_gpu_sampler_t* new_sampler = (nv_gpu_sampler_t*)nv_list_push_empty(&driver->samplers);

  *new_sampler = (nv_gpu_sampler_t){
    .filter         = pInfo->filter,
    .mipmap_filter  = pInfo->mipmap_filter,
    .address_mode   = pInfo->address_mode,
    .max_anisotropy = pInfo->max_anisotropy,
    .mip_lod_bias   = pInfo->mip_lod_bias,
    .min_lod        = pInfo->min_lod,
    .max_lod        = pInfo->max_lod,
  };

  /**
   * Store the pointer to the newly created sampler into dst.
   */
  *dst = new_sampler;

  VkPhysicalDeviceProperties device_properties = nv_zero_init(VkPhysicalDeviceProperties);
  vkGetPhysicalDeviceProperties(driver->ctx->phys_device, &device_properties);

  const VkPhysicalDeviceLimits* limits = &device_properties.limits;
  nv_assert_else_return(nv_list_size(&driver->samplers) <= limits->maxSamplerAllocationCount, );

  VkSamplerCreateInfo samplerInfo = nv_zero_init(VkSamplerCreateInfo);
  samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter           = (VkFilter)pInfo->filter;
  samplerInfo.minFilter           = (VkFilter)pInfo->filter;
  samplerInfo.mipmapMode          = (VkSamplerMipmapMode)pInfo->mipmap_filter;
  samplerInfo.addressModeU        = (VkSamplerAddressMode)pInfo->address_mode;
  samplerInfo.addressModeV        = (VkSamplerAddressMode)pInfo->address_mode;
  samplerInfo.addressModeW        = (VkSamplerAddressMode)pInfo->address_mode;
  samplerInfo.anisotropyEnable    = pInfo->max_anisotropy > 1.0f;
  samplerInfo.maxLod              = pInfo->max_lod;
  samplerInfo.minLod              = pInfo->min_lod;
  nvvk_result_check(*driver->ctx, vkCreateSampler(driver->ctx->device, &samplerInfo, &driver->ctx->vkalloc, &new_sampler->vksampler));
}

void
nv_gpu_write_to_texture(struct nvvk_driver_t* driver, nv_gpu_texture* tex, const nv_image_t* src)
{
  if (!(tex->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
  {
    nv_log_error("Cannot write to an image which does not have usage \n"
                 "VK_IMAGE_USAGE_TRANSFER_DST_BIT");
  }

  const size_t tex_size = tex->extent.width * tex->extent.height * nv_format_get_bytes_per_pixel(tex->format);
  // if (tex->memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE) {
  //     void *mapped;
  //     nv_gpu_map_memory(tex->memory, tex_size, tex->offset, &mapped);
  //     memcpy(mapped, src->data, tex_size);
  //     nv_gpu_unmap_memory(tex->memory);
  //     return;
  // }

  nv_vk_stage_image_transfer(driver, tex->image, src->data, src->width, src->height, tex_size);
}

VkImage
nv_gpu_texture_get(const nv_gpu_texture* tex)
{
  return tex ? tex->image : VK_NULL_HANDLE;
}

VkImageView
nv_gpu_texture_get_view(const nv_gpu_texture* tex)
{
  return tex ? tex->view : VK_NULL_HANDLE;
}

VkSampler
nv_gpu_sampler_get(const nv_gpu_sampler_t* sampler)
{
  return sampler ? sampler->vksampler : VK_NULL_HANDLE;
}

void
nv_gpu_create_texture(nvvk_ctx_t* nvvkctx, const nv_gpu_texture_create_info* pInfo, nv_gpu_texture* dst)
{
  nv_assert_else_return(pInfo != NULL, );
  nv_assert_else_return(pInfo->arraylayers != 0, );
  nv_assert_else_return(pInfo->format != 0, );
  nv_assert_else_return(pInfo->miplevels != 0, );
  nv_assert_else_return(pInfo->samples != 0, );
  nv_assert_else_return(pInfo->type != 0, );
  nv_assert_else_return(dst != NULL, );

  nv_bzero(dst, sizeof(nv_gpu_texture));

  nv_format fmt = pInfo->format;
  if (pInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT)
  {
    fmt = nv_vk_get_supported_format_for_draw(nvvkctx, fmt);
  }

  dst->type   = pInfo->type;
  dst->format = pInfo->format;
  dst->extent = (VkExtent3D){ pInfo->extent.width, pInfo->extent.height, pInfo->extent.depth };

  VkImageUsageFlags usage = 0;

  switch (pInfo->usage)
  {
    case NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE: usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_COLOR_TEXTURE: usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE:
    case NOVA_GPU_TEXTURE_USAGE_STENCIL_TEXTURE: usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_STORAGE_TEXTURE: usage |= VK_IMAGE_USAGE_STORAGE_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_INPUT_ATTACHMENT: usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_RESOLVE_TEXTURE: usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT: usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_PRESENTATION: usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    default: nv_log_error("Unknown texture usage: %u\n", pInfo->usage); break;
  }
  dst->usage = usage;

  if (pInfo->usage & NOVA_GPU_TEXTURE_USAGE_PRESENTATION)
  {
    return;
  }

  VkImageCreateInfo imageCreateInfo = nv_zero_init(VkImageCreateInfo);
  imageCreateInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType         = pInfo->type;
  imageCreateInfo.extent            = (VkExtent3D){ pInfo->extent.width, pInfo->extent.height, pInfo->extent.depth };
  imageCreateInfo.mipLevels         = pInfo->miplevels;
  imageCreateInfo.arrayLayers       = pInfo->arraylayers;
  imageCreateInfo.format            = nv_format_to_vk_format(fmt);
  imageCreateInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage             = usage;
  imageCreateInfo.samples           = (VkSampleCountFlagBits)pInfo->samples;
  imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(*nvvkctx, vkCreateImage(nvvkctx->device, &imageCreateInfo, &nvvkctx->vkalloc, &dst->image));
}

uint32_t
nv_format_to_vk_format(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_R8: return VK_FORMAT_R8_UNORM;
    case NOVA_FORMAT_RG8: return VK_FORMAT_R8G8_UNORM;
    case NOVA_FORMAT_RGB8: return VK_FORMAT_R8G8B8_UNORM;
    case NOVA_FORMAT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;

    case NOVA_FORMAT_BGR8: return VK_FORMAT_B8G8R8_UNORM;
    case NOVA_FORMAT_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;

    case NOVA_FORMAT_RGB16: return VK_FORMAT_R16G16B16_UNORM;
    case NOVA_FORMAT_RGBA16: return VK_FORMAT_R16G16B16A16_UNORM;
    case NOVA_FORMAT_RG32: return VK_FORMAT_R32G32_SFLOAT;
    case NOVA_FORMAT_RGB32: return VK_FORMAT_R32G32B32_SFLOAT;
    case NOVA_FORMAT_RGBA32: return VK_FORMAT_R32G32B32A32_SFLOAT;

    case NOVA_FORMAT_R8_SINT: return VK_FORMAT_R8_SINT;
    case NOVA_FORMAT_RG8_SINT: return VK_FORMAT_R8G8_SINT;
    case NOVA_FORMAT_RGB8_SINT: return VK_FORMAT_R8G8B8_SINT;
    case NOVA_FORMAT_RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;

    case NOVA_FORMAT_R8_UINT: return VK_FORMAT_R8_UINT;
    case NOVA_FORMAT_RG8_UINT: return VK_FORMAT_R8G8_UINT;
    case NOVA_FORMAT_RGB8_UINT: return VK_FORMAT_R8G8B8_UINT;
    case NOVA_FORMAT_RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;

    case NOVA_FORMAT_R8_SRGB: return VK_FORMAT_R8_SRGB;
    case NOVA_FORMAT_RG8_SRGB: return VK_FORMAT_R8G8_SRGB;
    case NOVA_FORMAT_RGB8_SRGB: return VK_FORMAT_R8G8B8_SRGB;
    case NOVA_FORMAT_RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;

    case NOVA_FORMAT_BGR8_SRGB: return VK_FORMAT_B8G8R8_SRGB;
    case NOVA_FORMAT_BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;

    case NOVA_FORMAT_D16: return VK_FORMAT_D16_UNORM;
    case NOVA_FORMAT_D24: return VK_FORMAT_D24_UNORM_S8_UINT;
    case NOVA_FORMAT_D32: return VK_FORMAT_D32_SFLOAT;
    case NOVA_FORMAT_D24_S8: return VK_FORMAT_D24_UNORM_S8_UINT;
    case NOVA_FORMAT_D32_S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;

    case NOVA_FORMAT_BC1: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case NOVA_FORMAT_BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
    case NOVA_FORMAT_BC7: return VK_FORMAT_BC7_UNORM_BLOCK;

    default: return VK_FORMAT_UNDEFINED;
  }
}

nv_format
nv_vk_format_to_nv_format(VkFormat_ format)
{
  switch (format)
  {
    case VK_FORMAT_R8_UNORM: return NOVA_FORMAT_R8;
    case VK_FORMAT_R8G8_UNORM: return NOVA_FORMAT_RG8;
    case VK_FORMAT_R8G8B8_UNORM: return NOVA_FORMAT_RGB8;
    case VK_FORMAT_R8G8B8A8_UNORM: return NOVA_FORMAT_RGBA8;

    case VK_FORMAT_B8G8R8_UNORM: return NOVA_FORMAT_BGR8;
    case VK_FORMAT_B8G8R8A8_UNORM: return NOVA_FORMAT_BGRA8;

    case VK_FORMAT_R16G16B16_UNORM: return NOVA_FORMAT_RGB16;
    case VK_FORMAT_R16G16B16A16_UNORM: return NOVA_FORMAT_RGBA16;
    case VK_FORMAT_R32G32_SFLOAT: return NOVA_FORMAT_RG32;
    case VK_FORMAT_R32G32B32_SFLOAT: return NOVA_FORMAT_RGB32;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return NOVA_FORMAT_RGBA32;

    case VK_FORMAT_R8_SINT: return NOVA_FORMAT_R8_SINT;
    case VK_FORMAT_R8G8_SINT: return NOVA_FORMAT_RG8_SINT;
    case VK_FORMAT_R8G8B8_SINT: return NOVA_FORMAT_RGB8_SINT;
    case VK_FORMAT_R8G8B8A8_SINT: return NOVA_FORMAT_RGBA8_SINT;

    case VK_FORMAT_R8_SRGB: return NOVA_FORMAT_R8_SRGB;
    case VK_FORMAT_R8G8_SRGB: return NOVA_FORMAT_RG8_SRGB;
    case VK_FORMAT_R8G8B8_SRGB: return NOVA_FORMAT_RGB8_SRGB;
    case VK_FORMAT_R8G8B8A8_SRGB: return NOVA_FORMAT_RGBA8_SRGB;

    case VK_FORMAT_B8G8R8_SRGB: return NOVA_FORMAT_BGR8_SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return NOVA_FORMAT_BGRA8_SRGB;

    case VK_FORMAT_R8_UINT: return NOVA_FORMAT_R8_UINT;
    case VK_FORMAT_R8G8_UINT: return NOVA_FORMAT_RG8_UINT;
    case VK_FORMAT_R8G8B8_UINT: return NOVA_FORMAT_RGB8_UINT;
    case VK_FORMAT_R8G8B8A8_UINT: return NOVA_FORMAT_RGBA8_UINT;

    case VK_FORMAT_D16_UNORM: return NOVA_FORMAT_D16;
    case VK_FORMAT_D32_SFLOAT: return NOVA_FORMAT_D32;
    case VK_FORMAT_D24_UNORM_S8_UINT: return NOVA_FORMAT_D24_S8;
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return NOVA_FORMAT_D32_S8;

    case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return NOVA_FORMAT_BC1;
    case VK_FORMAT_BC3_UNORM_BLOCK: return NOVA_FORMAT_BC3;
    case VK_FORMAT_BC7_UNORM_BLOCK: return NOVA_FORMAT_BC7;

    default: return NOVA_FORMAT_UNDEFINED;
  }
}

// SPRITE

nv_error
nv_sprite_load_from_memory(nvvk_driver_t* driver, const unsigned char* data, size_t w, size_t h, nv_format fmt, nv_sprite_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(w != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(h != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(fmt != NOVA_FORMAT_UNDEFINED, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_sprite_t));

  dst->rcount = 1;

  fmt      = nv_vk_get_supported_format_for_draw(driver->ctx, fmt);
  dst->fmt = fmt;

  nv_gpu_texture_create_info tex_info = {
    .format      = fmt,
    .samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .type        = VK_IMAGE_TYPE_2D,
    .usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .extent      = (nv_extent3D){ .width = w, .height = h, .depth = 1 },
    .arraylayers = 1,
    .miplevels   = 1,
  };
  nv_gpu_create_texture(driver->ctx, &tex_info, &dst->tex);

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(driver->ctx->device, nv_gpu_texture_get(&dst->tex), &mem_req);

  nv_gpu_allocate_memory(driver->ctx, mem_req.size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &dst->mem);
  nv_gpu_bind_texture_to_memory(driver->ctx, &dst->mem, 0, &dst->tex);

  const nv_image_t img = (const nv_image_t){ .width = w, .height = h, .format = fmt, .data = (unsigned char*)data };
  nv_gpu_write_to_texture(driver, &dst->tex, &img);

  nv_gpu_sampler_create_info sampler_info = {
    .filter         = NV_FILTER_NEAREST,
    .mipmap_filter  = NV_FILTER_NEAREST,
    .address_mode   = NV_TEXTURE_ADDRESS_MODE_REPEAT,
    .max_anisotropy = 1.0f,
    .mip_lod_bias   = 0.0f,
    .min_lod        = 0.0f,
    .max_lod        = VK_LOD_CLAMP_NONE,
  };
  nv_gpu_create_sampler(driver, &sampler_info, &dst->sampler);

  VkDescriptorSetLayoutBinding binding = (VkDescriptorSetLayoutBinding){
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  nv_allocate_descriptor_set(driver->ctx, &g_pool, &binding, 1, &dst->set);

  VkDescriptorImageInfo desc_img = {
    .sampler     = nv_gpu_sampler_get(dst->sampler),
    .imageView   = nv_sprite_get_vk_image_view(dst),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = dst->set->set,
    .dstBinding      = 0,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &desc_img,
  };
  nv_descriptor_set_submit_write(driver->ctx, dst->set, &write);

  return NV_SUCCESS;
}

nv_error
nv_sprite_load_from_disk(nvvk_driver_t* driver, const char* path, nv_sprite_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->ctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(path != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_sprite_t));

  nv_image_t tex = nv_zero_init(nv_image_t);

  nv_error code = nv_image_load(path, &tex);
  nv_assert_else_return(code == NV_SUCCESS, code);

  // TODO: should these be io errors?
  // I mean a read from the disk probably failed
  // TODO: this will be fixed by returning an error code from image loading function

  nv_assert_else_return(tex.width != 0, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.height != 0, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.data != NULL, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.format != NOVA_FORMAT_UNDEFINED, NV_ERROR_IO_ERROR);

  code = nv_sprite_load_from_memory(driver, tex.data, tex.width, tex.height, tex.format, dst);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  dst->rcount = 1;
  if (tex.data)
  {
    nv_free(tex.data);
  }

  return code;
}

void
nv_sprite_destroy(nvvk_ctx_t* nvvkctx, nv_sprite_t* spr)
{
  if (!spr)
  {
    return;
  }

  nv_gpu_destroy_texture(nvvkctx, &spr->tex);
  nv_gpu_free_memory(nvvkctx, &spr->mem);
}

void
nv_sprite_lock(nv_sprite_t* spr)
{
  spr->rcount++;
}

void
nv_sprite_release(nvvk_ctx_t* nvvkctx, nv_sprite_t* spr)
{
  spr->rcount--;
  if (spr->rcount <= 0)
  {
    nv_sprite_destroy(nvvkctx, spr);
  }
}

void
nv_sprite_get_dimensions(const nv_sprite_t* spr, size_t* w, size_t* h)
{
  if (w)
  {
    *w = spr->w;
  }
  if (h)
  {
    *h = spr->h;
  }
}

VkImage
nv_sprite_get_vk_image(const nv_sprite_t* spr)
{
  return nv_gpu_texture_get(&spr->tex);
}

VkImageView
nv_sprite_get_vk_image_view(const nv_sprite_t* spr)
{
  return nv_gpu_texture_get_view(&spr->tex);
}

VkDescriptorSet
nv_sprite_get_descriptor_set(const nv_sprite_t* spr)
{
  return spr->set->set;
}

VkSampler
nv_sprite_get_sampler(const nv_sprite_t* spr)
{
  return nv_gpu_sampler_get(spr->sampler);
}

nv_format
nv_sprite_get_format(const nv_sprite_t* spr)
{
  return spr->fmt;
}
// SPRITE

void
nv_camera_destroy(nvvk_ctx_t* nvvkctx, nv_camera_t* cam)
{
  // nv_descriptor_set_t_destroy(cam->sets);
  nv_gpu_buffer_destroy(&cam->ub);
  nv_gpu_free_memory(nvvkctx, &cam->mem);
}

nv_error
nv_camera_init(nvvk_driver_t* driver, nv_camera_t* cam)
{
  nvvk_ctx_t* const nvvkctx = driver->ctx;

  const flt_t ortho_w = 10.0F, ortho_h = 10.0F;
  *cam = (nv_camera_t){
    .perspective = nv_zero_init(mat4),
    .ortho_size  = (vec2){ ortho_w, ortho_h },
    .ortho       = m4ortho(-ortho_w, ortho_w, -ortho_h, ortho_h, 0.1f, 100.0f),
    .position    = (vec3){ 0.0f, 0.0f, 10.0f },
    .actual_pos  = (vec3){ 0.0f, 0.0f, 10.0f },
    .front       = (vec3){ 0.0f, 0.0f, 1.0f },
    .up          = (vec3){ 0.0f, 1.0f, 0.0f },
    .right       = (vec3){ 1.0f, 0.0f, 0.0f },

    // These angles should not be in radians because they're converted at update() time.
    .yaw_degrees   = -90.0F,
    .pitch_degrees = 0.0F,
    .fov           = 90.0f,

    .near_clip = 0.1F,
    .far_clip  = 1000.0F,
  };

  VkPhysicalDeviceProperties phys_device_properties;
  vkGetPhysicalDeviceProperties(nvvkctx->phys_device, &phys_device_properties);

  VkDeviceSize ub_align = phys_device_properties.limits.minUniformBufferOffsetAlignment;
  nv_assert_else_return(ub_align != 0, NV_ERROR_INVALID_RETVAL);

  // the size of a single uniform buffer.
  size_t ub_size = _align_up_size(sizeof(nv_camera_uniform_buffer), ub_align);
  nv_assert_else_return(ub_size != 0, NV_ERROR_INVALID_RETVAL);

  // nv_gpu_create_buffer(nvvkctx, ub_size * CAMERA_FAKE_BUFFER_COUNT, ub_align, NOVA_GPU_BUFFER_USAGE_UNIFORM_BUFFER, &cam->ub);
  nv_gpu_buffer_init(driver, ub_size * CAMERA_FAKE_BUFFER_COUNT, ub_align, NV_GPU_BUFFER_UNIFORM_BUFFER_BIT, &cam->ub);
  nv_assert_else_return(cam->ub.buffer != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  // nv_gpu_allocate_memory(nvvkctx, ub_size * CAMERA_FAKE_BUFFER_COUNT, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_COHERENT, &cam->mem);
  // nv_assert_else_return(cam->mem.memory != VK_NULL_HANDLE, NV_ERROR_INVALID_RETVAL);

  // nv_gpu_bind_buffer_to_memory(nvvkctx, &cam->mem, 0, &cam->ub);

  VkDescriptorSetLayoutBinding bindings[] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL } };
  if (nv_allocate_descriptor_set(nvvkctx, &g_pool, bindings, 1, &cam->sets) != NV_ERROR_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_assert_else_return(cam->ub.buffer != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(cam->sets != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(cam->sets->set != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(ub_size != 0, NV_ERROR_BROKEN_STATE);

  VkDescriptorBufferInfo bufferinfo = {
    .buffer = cam->ub.buffer,
    .offset = 0,
    .range  = (VkDeviceSize)ub_size,
  };

  VkWriteDescriptorSet write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = cam->sets->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    .pBufferInfo     = &bufferinfo,
  };
  if (nv_descriptor_set_submit_write(nvvkctx, cam->sets, &write) != NV_ERROR_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_gpu_buffer_map_memory(&cam->ub, ub_size * CAMERA_FAKE_BUFFER_COUNT, 0, (void**)&cam->mem_mapped);
  nv_assert_else_return(cam->mem_mapped != NULL, NV_ERROR_EXTERNAL);

  return NV_ERROR_SUCCESS;
}

mat4
nv_camera_get_projection(nv_camera_t* cam)
{
  return cam->perspective;
}

mat4
nv_camera_get_view(nv_camera_t* cam)
{
  return cam->view;
}

vec3
nv_camera_get_up_vector(nv_camera_t* cam)
{
  return cam->up;
}

vec3
nv_camera_get_front_vector(nv_camera_t* cam)
{
  return cam->front;
}

void
nv_camera_rotate(nv_camera_t* cam, flt_t yaw_, flt_t pitch_)
{
  cam->yaw_degrees += yaw_;
  cam->pitch_degrees -= pitch_;

  cam->yaw_degrees = SDL_fmodf(cam->yaw_degrees, 360.0f);

  const flt_t bound  = 89.9f;
  cam->pitch_degrees = NVM_CLAMP(cam->pitch_degrees, -bound, bound);
}

void
nv_camera_move(nv_camera_t* cam, const vec3 amt)
{
  cam->actual_pos = v3add(cam->actual_pos, v3muls(cam->right, amt.x));
  cam->actual_pos = v3add(cam->actual_pos, v3muls(cam->up, amt.y));
  cam->actual_pos = v3add(cam->actual_pos, v3muls(cam->front, amt.z));
}

void
nv_camera_set_position(nv_camera_t* cam, const vec3 pos)
{
  cam->actual_pos = pos;
}

void
nv_camera_update(nv_camera_t* cam, struct nv_renderer_t* rd)
{
  const flt_t yaw_rads = NVM_DEG2RAD(cam->yaw_degrees), pitch_rads = NVM_DEG2RAD(cam->pitch_degrees);
  const flt_t cospitch = SDL_cosf(pitch_rads);
  vec3        new_front;
  new_front.x = SDL_cosf(yaw_rads) * cospitch;
  new_front.y = SDL_sinf(pitch_rads);
  new_front.z = SDL_sinf(yaw_rads) * cospitch;

  const vec3 world_up = (vec3){ 0.0f, 1.0f, 0.0f };

  cam->front = v3normalize(new_front);
  cam->right = v3normalize(v3cross(cam->front, world_up));
  cam->up    = v3normalize(v3cross(cam->right, cam->front));

  cam->view = m4lookat(cam->actual_pos, v3add(cam->actual_pos, cam->front), cam->up);

  cam->position = cam->actual_pos;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  const flt_t       aspect       = (flt_t)RenderExtent.width / (flt_t)RenderExtent.height;
  cam->perspective               = m4perspective(cam->fov, aspect, cam->near_clip, cam->far_clip);

  nv_camera_uniform_buffer ub = nv_zero_init(nv_camera_uniform_buffer);
  NV_MATRIX_COPY(ub.perspective, cam->perspective);
  NV_MATRIX_COPY(ub.ortho, cam->ortho);
  NV_MATRIX_COPY(ub.view, cam->view);
  cam->mem_mapped[nv_renderer_get_frame(rd)] = ub;
}

vec2
nv_camera_get_global_mouse_position(nv_input_ctx_t* inputctx, const nv_camera_t* cam)
{
  vec2 ortho_pos = v2mulv(nv_input_get_mouse_position(inputctx), cam->ortho_size);
  return v2add(ortho_pos, (vec2){ cam->position.x, cam->position.y });
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
nv_gpu_create_framebuffer(nvvk_ctx_t* nvvkctx, const nv_gpu_framebuffer_create_info_t* pCreateInfo, nv_gpu_framebuffer_t* dst)
{
  nv_assert_else_return(pCreateInfo != NULL, -1);
  nv_assert_else_return(pCreateInfo->attachments != NULL, -1);
  nv_assert_else_return(pCreateInfo->num_attachments != 0, -1);
  nv_assert_else_return(pCreateInfo->extent.width != 0, -1);
  nv_assert_else_return(pCreateInfo->extent.height != 0, -1);
  nv_assert_else_return(pCreateInfo->num_layers != 0, -1);
  nv_assert_else_return(pCreateInfo->pass != VK_NULL_HANDLE, -1);
  nv_assert_else_return(dst != NULL, -1);

  nv_bzero(dst, sizeof(nv_gpu_framebuffer_t));

  dst->attachments     = pCreateInfo->attachments;
  dst->num_attachments = pCreateInfo->num_attachments;
  dst->pass            = pCreateInfo->pass;
  dst->extent          = pCreateInfo->extent;
  dst->num_layers      = pCreateInfo->num_layers;

  uchar             stack[1000];
  nv_alloc_estack_t estack = {
    .buffer      = stack,
    .buffer_size = sizeof(stack),
  };

  nv_list_t attachments;
  nv_list_init(sizeof(VkImageView), 6, nv_allocator_estack, &estack, &attachments);

  for (size_t i = 0; i < pCreateInfo->num_attachments; i++)
  {
    VkImageView view = nv_gpu_texture_get_view(pCreateInfo->attachments[i]);
    nv_list_push_back(&attachments, &view);
  }

  VkFramebufferCreateInfo framebufferInfo = nv_zero_init(VkFramebufferCreateInfo);
  framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass              = pCreateInfo->pass;
  framebufferInfo.attachmentCount         = nv_list_size(&attachments);
  framebufferInfo.pAttachments            = (VkImageView*)nv_list_data(&attachments);
  framebufferInfo.width                   = pCreateInfo->extent.width;
  framebufferInfo.height                  = pCreateInfo->extent.height;
  framebufferInfo.layers                  = 1;
  nvvk_result_check(*nvvkctx, vkCreateFramebuffer(nvvkctx->device, &framebufferInfo, &nvvkctx->vkalloc, &dst->handle));

  nv_list_destroy(&attachments);

  return 0;
}

void
nv_gpu_destroy_framebuffer(nvvk_ctx_t* nvvkctx, nv_gpu_framebuffer_t* dst)
{
  if (!dst)
  {
    return;
  }

  vkDestroyFramebuffer(nvvkctx->device, dst->handle, &nvvkctx->vkalloc);
}
