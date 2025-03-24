#include "GPU/vk.h"

#include "GPU/buffer.h"
#include "GPU/fbf.h"
#include "GPU/memory.h"
#include "GPU/pipeline.h"
#include "GPU/texture.h"
#include "GPU/vkstdafx.h"
#include "common/image.h"
#include "common/mem.h"
#include "containers/atlas.h"
#include "containers/hashmap.h"
#include "containers/list.h"
#include "containers/string.h"
#include "engine/camera.h"
#include "engine/ctext.h"
#include "engine/engine.h"
#include "engine/fontc.h"
#include "engine/input.h"
#include "engine/renderer.h"
#include "engine/shadermanager.h"
#include "engine/shadermanagerdev.h"
#include "engine/sprite_renderer.h"
#include "engine/ui.h"
#include "std/stdafx.h"
#include "std/string.h"

#include <SDL2/SDL_vulkan.h>
#include <math.h>
#include <vulkan/vulkan_core.h>

#define HAS_FLAG(flag) ((nv_gpu_vk_flag_register & flag) || (flags & flag))
#define STR(s) #s

// vk.h
VkCommandPool   cmd_pool = VK_NULL_HANDLE;
VkCommandBuffer buffer   = VK_NULL_HANDLE;

nv_gpu_result_check_fn _nvvk_result_fn = _nvvk_default_result_check_fn; // To not cause NULLptr dereference.
                                                                        // SetResultCheckFunc also checks for NULLptr
                                                                        // and handles it.
u32 nv_gpu_vk_flag_register = 0;
// vk.h

// nv_pipelines.h
nv_baked_pipelines g_Pipelines;

VkPipeline      base_pipeline = NULL;
VkPipelineCache cache         = NULL;

// nv_pipelines.h

// renderer.h

nv_descriptor_pool_t g_pool;
nv_camera_t          camera;

// renderer.h

// nvgfx vv

nvvk_context_t nvvk_context;

nv_extent2d
nv_get_window_size(void)
{
  int ww, wh;
  SDL_GetWindowSize(nvvk_context.window, &ww, &wh);
  return (nv_extent2d){ ww, wh };
}

typedef struct quad_vertex          quad_vertex;
typedef struct nv_line_vertex_t     nv_line_vertex_t;
typedef struct ctext_glyph_vertex_t ctext_glyph_vertex_t;

struct quad_vertex
{
  vec3f m_position;
  vec2f m_tex_coords;
};

struct nv_line_vertex_t
{
  vec2 m_position;
};

static quad_vertex quad_vertices[4];

static const uint32_t quad_indices[] = { 0, 1, 2, 0, 2, 3 };

// ctext
struct ctext_glyph_vertex_t
{
  vec3f m_pos;
  vec2f m_uv;
};

struct ctext_drawcall_t
{
  mat4f                 m_model;
  size_t                m_vertex_count;
  size_t                m_index_count;
  size_t                m_index_offset;
  ctext_glyph_vertex_t* m_vertices;
  u32*                  m_indices;
  vec4f                 m_color;
  flt_t                 m_scale;
};

struct ctext_label_t
{
  ctext_hori_align m_h_align;
  ctext_vert_align m_v_align;
  flt_t            m_scale;
  int              m_index;
  nv_string_t      m_text;
  cfont_t*         m_fnt;
  nv_object*       m_obj;
};

struct push_constants
{
  mat4f m_model;
  vec4f m_color;
  vec4f m_outline_color;
  flt_t m_scale;
};

typedef enum ctext_err_t
{
  CTEXT_ERR_SUCCESS,
  CTEXT_ERR_Invalid_glyph, // May also mean that there just isn't a glyph
  CTEXT_ERR_WRONG_CACHE,   // This cache is not the one you're looking for
  CTEXT_ERR_FILE_ERROR,
} ctext_err_t;
// ctext

u32
nv_renderer_get_frame(const nv_renderer_t* rd)
{
  return rd->m_frame;
}

VkCommandBuffer
nv_renderer_get_draw_buffer(const nv_renderer_t* rd)
{
  return *(VkCommandBuffer*)nv_list_get(&rd->m_draw_cmd_buffers, rd->m_frame);
}

VkRenderPass
nv_renderer_get_render_pass(const nv_renderer_t* rd)
{
  return rd->m_render_pass;
}

nv_extent2d
nv_renderer_get_render_extent(const nv_renderer_t* rd)
{
  return rd->m_render_extent;
}

u32
nv_renderer_get_max_frames_in_flight(const nv_renderer_t* rd)
{
  return 1U + (u32)rd->m_buffer_mode;
}

#define ABSF(x) ((x >= 0.0f) ? (x) : -(x))

bool
nv_Quad_Visible(const vec3* pos, const vec3* siz)
{
  const flt_t half_width  = siz->x * 0.5f;
  const flt_t half_height = siz->y * 0.5f;
  const flt_t deltax      = pos->x - camera.m_position.x;
  const flt_t deltay      = pos->y - camera.m_position.y;
  const flt_t dx          = ABSF(deltax); // delta x & y
  const flt_t dy          = ABSF(deltay); //

  return (dx <= (half_width + camera.m_ortho_size.x) && dy <= (half_height + camera.m_ortho_size.y));
}

void
nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer)
{
  nv_draw_call_t drawcall = { .m_type     = NOVA_DRAWCALL_QUAD,
                              .m_layer    = layer,
                              .m_drawcall = { .m_quad = { .m_spr = spr, .m_tex_multiplier = tex_coord_multiplier, .m_pos = position, .m_siz = size, .m_col = color } } };
  nv_list_push_back(&rd->m_drawcalls, &drawcall);
}

void
nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer)
{
  nv_draw_call_t drawcall = { .m_type = NOVA_DRAWCALL_LINE, .m_layer = layer, .m_drawcall = { .m_line = { .m_begin = start, .m_end = end, .m_col = color } } };
  nv_list_push_back(&rd->m_drawcalls, &drawcall);
}

// thjs will just sort the array from small layer to big layer :>
int
__drawcall_compar(const void* obj1, const void* obj2)
{
  const nv_draw_call_t* call1 = obj1;
  const nv_draw_call_t* call2 = obj2;
  return ((int)call1->m_layer - (int)call2->m_layer) + ((int)call1->m_type - (int)call2->m_type);
}

void
_nv_renderer_flush_renders(nv_renderer_t* rd)
{
  const uint32_t        camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);
  const VkCommandBuffer cmd              = nv_renderer_get_draw_buffer(rd);
  const VkDescriptorSet camera_set       = camera.m_sets->m_set;

  bool bound_quad_state = 0;

  nv_list_sort(&rd->m_drawcalls, __drawcall_compar);

  nv_draw_call_type state = NOVA_DRAWCALL_INVALID;

  for (int i = 0; i < (int)nv_list_size(&rd->m_drawcalls); i++)
  {
    const nv_draw_call_t* drawcall = &((nv_draw_call_t*)nv_list_data(&rd->m_drawcalls))[i];

    if (drawcall->m_type == NOVA_DRAWCALL_QUAD)
    {
      // if (!nv_Quad_Visible(&drawcall->m_drawcall.quad.pos,
      // &drawcall->m_drawcall.quad.siz)) {
      //     continue;
      // }

      if (state != NOVA_DRAWCALL_QUAD)
      {
        VkDeviceSize offsets = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.m_unlit.m_pipeline);

        if (!bound_quad_state)
        {
          vkCmdBindVertexBuffers(cmd, 0, 1, &rd->m_quad_vb.m_buffer, &offsets);
          vkCmdBindIndexBuffer(cmd, rd->m_quad_vb.m_buffer, sizeof(quad_vertices), VK_INDEX_TYPE_UINT32);
        }

        state            = NOVA_DRAWCALL_QUAD;
        bound_quad_state = 1;
      }

      struct push_constants
      {
        mat4f m_model;
        vec4f m_color;
        vec2f m_tex_multiplier; // Multiplied with the tex coords
      } pc;

      mat4f scale         = m4fscale(m4finit(1.0f), v3fmuls(drawcall->m_drawcall.m_quad.m_siz, 2.0f));
      mat4f rotate        = m4finit(1.0f);
      mat4f translate     = m4ftranslate(m4finit(1.0f), drawcall->m_drawcall.m_quad.m_pos);
      pc.m_model          = m4fmul(translate, m4fmul(rotate, scale));
      pc.m_color          = drawcall->m_drawcall.m_quad.m_col;
      pc.m_tex_multiplier = drawcall->m_drawcall.m_quad.m_tex_multiplier;
      vkCmdPushConstants(cmd, g_Pipelines.m_unlit.m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

      VkDescriptorSet       sprite_set = nv_sprite_get_descriptor_set(drawcall->m_drawcall.m_quad.m_spr);
      const VkDescriptorSet sets[]     = { camera_set, sprite_set };

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.m_unlit.m_pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
      vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }
    else if (drawcall->m_type == NOVA_DRAWCALL_LINE)
    {
      if (state != NOVA_DRAWCALL_LINE)
      {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.m_line.m_pipeline);

        state = NOVA_DRAWCALL_LINE;
      }

      const VkDescriptorSet sets[] = { camera_set };
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.m_line.m_pipeline_layout, 0, 1, sets, 1, &camera_ub_offset);

      struct line_push_constants
      {
        mat4f m_model;
        vec4f m_color;
        vec2f m_line_begin;
        vec2f m_line_end;
      } pc;
      pc.m_model      = m4finit(1.0f);
      pc.m_color      = drawcall->m_drawcall.m_line.m_col;
      pc.m_line_begin = (vec2f){ drawcall->m_drawcall.m_line.m_begin.x, drawcall->m_drawcall.m_line.m_begin.y };
      pc.m_line_end   = (vec2f){ drawcall->m_drawcall.m_line.m_end.x, drawcall->m_drawcall.m_line.m_end.y };
      vkCmdPushConstants(cmd, g_Pipelines.m_line.m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants), &pc);

      vkCmdDraw(cmd, 2, 1, 0, 0);
    }
  }

  nv_list_clear(&rd->m_drawcalls);
}

void
nv_renderer_prepare_quad_renderer(nv_renderer_t* rd)
{
  nv_memcpy(
      quad_vertices,
      (const quad_vertex[4]){ (const quad_vertex){ .m_position = (vec3f){ +0.5f, +0.5f, 0.0f }, .m_tex_coords = (vec2f){ 1.0f, 0.0f } },
                              (const quad_vertex){ .m_position = (vec3f){ -0.5f, +0.5f, 0.0f }, .m_tex_coords = (vec2f){ 0.0f, 0.0f } },
                              (const quad_vertex){ .m_position = (vec3f){ -0.5f, -0.5f, 0.0f }, .m_tex_coords = (vec2f){ 0.0f, 1.0f } },
                              (const quad_vertex){ .m_position = (vec3f){ +0.5f, -0.5f, 0.0f }, .m_tex_coords = (vec2f){ 1.0f, 1.0f } } },
      sizeof(quad_vertices));

  nv_gpu_create_buffer(
      sizeof(quad_vertices) + sizeof(quad_indices), NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &rd->m_quad_vb);
  nv_gpu_allocate_memory(sizeof(quad_vertices) + sizeof(quad_indices), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &rd->m_quad_memory);
  nv_gpu_bind_buffer_to_memory(&rd->m_quad_memory, 0, &rd->m_quad_vb);

  void* data = nv_malloc(sizeof(quad_vertices) + sizeof(quad_indices));
  nv_memcpy(data, quad_vertices, sizeof(quad_vertices));
  nv_memcpy((char*)data + sizeof(quad_vertices), quad_indices, sizeof(quad_indices));
  nv_gpu_write_to_buffer(&rd->m_quad_vb, sizeof(quad_vertices) + sizeof(quad_indices), data, 0);

  nv_free(data);
}

void
nv_renderer_destroy(nv_renderer_t* rd)
{
  if (!rd)
  {
    return;
  }

  vkDeviceWaitIdle(nvvk_context.device);

  // we were only making frames_in_flight fences and it was working for some
  // reason!! that was the reason we were getting errors!
  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);

    // as the view was silently smushed into the
    // structure, we just kinda smush it out as well.
    vkDestroyImageView(nvvk_context.device, nv_gpu_texture_get_view(&data->m_sc_image), NOVA_VK_ALLOCATOR);

    nv_gpu_destroy_texture(&data->m_depth_image);

    nv_gpu_destroy_framebuffer(&data->m_color_framebuffer);

    vkDestroySemaphore(nvvk_context.device, data->m_image_available_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroySemaphore(nvvk_context.device, data->m_render_finish_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroyFence(nvvk_context.device, data->m_in_flight_fence, NOVA_VK_ALLOCATOR);
  }
  for (int i = 0; i < (int)nv_list_size(&rd->m_samplers); i++)
  {
    nv_gpu_sampler* samp = *(nv_gpu_sampler**)nv_list_get(&rd->m_samplers, i);
    vkDestroySampler(nvvk_context.device, samp->m_vksampler, NOVA_VK_ALLOCATOR);
  }

  nv_sprite_destroy(nv_sprite_empty);

  nv_camera_destroy(&camera);

  nv_vk_destroy_global_pipelines();
  nv_descriptor_pool_destroy(&g_pool);

  ctext_shutdown(rd);

  nv_list_destroy(&rd->m_samplers);
  nv_list_destroy(&rd->m_drawcalls);
  nv_list_destroy(&rd->m_render_data);

  nv_gpu_free_memory(&rd->m_depth_image_memory);

  nv_gpu_destroy_buffer(&rd->m_quad_vb);
  nv_gpu_free_memory(&rd->m_quad_memory);

  vkFreeCommandBuffers(nvvk_context.device, cmd_pool, 1, &buffer);
  vkDestroyCommandPool(nvvk_context.device, cmd_pool, NOVA_VK_ALLOCATOR);

  vkFreeCommandBuffers(nvvk_context.device, rd->m_command_pool, nv_list_size(&rd->m_draw_cmd_buffers), (VkCommandBuffer*)nv_list_data(&rd->m_draw_cmd_buffers));
  vkDestroyCommandPool(nvvk_context.device, rd->m_command_pool, NOVA_VK_ALLOCATOR);
  nv_list_destroy(&rd->m_draw_cmd_buffers);

  nvsm_shutdown();

  vkDestroySwapchainKHR(nvvk_context.device, rd->m_swapchain, NOVA_VK_ALLOCATOR);

  if (rd->m_flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    nv_gpu_destroy_texture(&rd->m_color_image);
    nv_gpu_free_memory(&rd->m_color_image_memory);
  }
  vkDestroyRenderPass(nvvk_context.device, rd->m_render_pass, NOVA_VK_ALLOCATOR);

  vkDestroyDebugUtilsMessengerEXT(nvvk_context.instance, nvvk_context.debug_messenger, NOVA_VK_ALLOCATOR);
  vkDestroySurfaceKHR(nvvk_context.instance, nvvk_context.surface, NULL);
  SDL_DestroyWindow(nvvk_context.window);
  vkDestroyDevice(nvvk_context.device, NOVA_VK_ALLOCATOR);
  vkDestroyInstance(nvvk_context.instance, NOVA_VK_ALLOCATOR);
}

void
create_optional_images(nv_renderer_t* rd)
{
  nvvk_result_check(vkGetSwapchainImagesKHR(nvvk_context.device, rd->m_swapchain, &nvvk_context.swap_chain_image_count, NULL));
  VkImage* swapchainImages = (VkImage*)nv_malloc(nvvk_context.swap_chain_image_count * sizeof(VkImage));
  nvvk_result_check(vkGetSwapchainImagesKHR(nvvk_context.device, rd->m_swapchain, &nvvk_context.swap_chain_image_count, swapchainImages));

  nv_list_resize(&rd->m_render_data, nvvk_context.swap_chain_image_count);

  if (rd->m_flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    size_t color_image_size = 0;
    nv_vk_create_texture_empty(
        rd->m_render_extent.m_width,
        rd->m_render_extent.m_height,
        nvvk_context.swap_chain_image_format,
        nvvk_context.samples,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        &color_image_size,
        &rd->m_color_image.m_image,
        NULL);
    nv_gpu_allocate_memory(color_image_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &rd->m_color_image_memory);
    nv_gpu_bind_texture_to_memory(&rd->m_color_image_memory, 0, &rd->m_color_image);
  }

  // attachment vector will be like <color resolve, depth attachment, swapchain
  // image>
  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);
    data->m_sc_image.m_image            = swapchainImages[i];

    const nv_gpu_texture_create_info image_info = {
      .m_format      = NOVA_FORMAT_D32,
      .m_samples     = nvvk_context.samples,
      .m_type        = VK_IMAGE_TYPE_2D,
      .m_usage       = NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE,
      .m_extent      = (nv_extent3D){ .m_width = rd->m_render_extent.m_width, .m_height = rd->m_render_extent.m_height, .m_depth = 1 },
      .m_arraylayers = 1,
      .m_miplevels   = 1,
    };
    nv_gpu_create_texture(&image_info, &data->m_depth_image);

    if (i == 0)
    {
      VkMemoryRequirements memReqs = nv_zero_init(VkMemoryRequirements);
      vkGetImageMemoryRequirements(nvvk_context.device, nv_gpu_texture_get(&data->m_depth_image), &memReqs);

      rd->m_shadow_image_size = memReqs.size;
      nv_gpu_allocate_memory(rd->m_shadow_image_size * nvvk_context.swap_chain_image_count, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &rd->m_depth_image_memory);
    }

    nv_gpu_bind_texture_to_memory(&rd->m_depth_image_memory, i * rd->m_shadow_image_size, &data->m_depth_image);
  }

  nv_free(swapchainImages);
}

void
create_framebuffers_and_swapchain_image_views(nv_renderer_t* rd)
{
  nv_list_t attachments;
  nv_list_init(sizeof(nv_gpu_texture*), 3, nv_allocator_get_default(), &attachments);

  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);

    // we don't know anything about the swapchain_image, as it's a swapchain
    // image so we have to manually create the image view;
    // we will later smush in the view using nv_gpu_texture_attach_view
    VkImageViewCreateInfo imageViewCreateInfo           = nv_zero_init(VkImageViewCreateInfo);
    imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image                           = nv_gpu_texture_get(&data->m_sc_image);
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = nv_format_to_vk_format(nvvk_context.swap_chain_image_format);
    imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;
    VkImageView vioew;
    nvvk_result_check(vkCreateImageView(nvvk_context.device, &imageViewCreateInfo, NOVA_VK_ALLOCATOR, &vioew));

    nv_gpu_texture_attach_view(&data->m_sc_image, vioew);

    nv_list_clear(&attachments);
    if (rd->m_flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
    {
      nv_list_push_set(&attachments, (nv_gpu_texture*[]){ &rd->m_color_image, &data->m_depth_image, &data->m_sc_image }, 3);
    }
    else
    {
      nv_list_push_set(&attachments, (nv_gpu_texture*[]){ &data->m_sc_image, &data->m_depth_image }, 2);
    }

    nv_gpu_framebuffer_create_info_t framebuffer_info = nv_zero_init(nv_gpu_framebuffer_create_info_t);
    framebuffer_info.m_attachments                    = (nv_gpu_texture**)nv_list_data(&attachments);
    framebuffer_info.m_num_attachments                = nv_list_size(&attachments);
    framebuffer_info.m_extent                         = rd->m_render_extent;
    framebuffer_info.m_num_layers                     = 1;
    framebuffer_info.m_pass                           = rd->m_render_pass;
    if (nv_gpu_create_framebuffer(&framebuffer_info, &data->m_color_framebuffer) != 0)
    {
      nv_push_error("Error in framebuffer creation.");
    }
  }

  nv_list_destroy(&attachments);
}

void
nv_renderer_initialize_graphics_singleton(void)
{
  if (!nv_vk_get_supported_format(nvvk_context.phys_device, nvvk_context.surface, &nvvk_context.swap_chain_image_format, &nvvk_context.swap_chain_color_space))
  {
    nv_log_and_abort("No supported format for display.\n");
  }
  nvvk_context.swap_chain_image_count = nv_vk_get_surface_image_count(nvvk_context.phys_device, nvvk_context.surface);

  u32 queueCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(nvvk_context.phys_device, &queueCount, NULL);
  nv_list_t queueFamilies;
  nv_list_init(sizeof(VkQueueFamilyProperties), queueCount, nv_allocator_get_default(), &queueFamilies);
  vkGetPhysicalDeviceQueueFamilyProperties(nvvk_context.phys_device, &queueCount, (VkQueueFamilyProperties*)nv_list_data(&queueFamilies));

  u32  graphicsFamily = 0, graphicsAndComputeFamily = 0, presentFamily = 0, computeFamily = 0, transferFamily = 0;
  bool foundGraphicsFamily = false, foundGraphicsAndComputeFamily = false, foundPresentFamily = false, foundComputeFamily = false, foundTransferFamily = false;

  u32 i = 0;
  for (u32 j = 0; j < queueCount; j++)
  {
    const VkQueueFamilyProperties queueFamily = ((VkQueueFamilyProperties*)nv_list_data(&queueFamilies))[j];
    VkBool32                      presentSupport;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceSupportKHR(nvvk_context.phys_device, i, nvvk_context.surface, &presentSupport));

    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      graphicsAndComputeFamily      = i;
      foundGraphicsAndComputeFamily = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
    {
      graphicsFamily      = i;
      foundGraphicsFamily = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
    {
      computeFamily      = i;
      foundComputeFamily = true;
    }
    if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
    {
      transferFamily      = i;
      foundTransferFamily = true;
    }
    if (presentSupport)
    {
      presentFamily      = i;
      foundPresentFamily = true;
    }
    if (foundGraphicsFamily && foundGraphicsAndComputeFamily && foundPresentFamily && foundComputeFamily && foundTransferFamily)
    {
      break;
    }

    i++;
  }

  nvvk_context.graphics_family_index             = graphicsFamily;
  nvvk_context.compute_family_index              = computeFamily;
  nvvk_context.transfer_family_index             = transferFamily;
  nvvk_context.present_family_index              = presentFamily;
  nvvk_context.graphics_and_compute_family_index = graphicsAndComputeFamily;

  vkGetDeviceQueue(nvvk_context.device, nvvk_context.graphics_family_index, 0, &nvvk_context.graphics_queue);
  vkGetDeviceQueue(nvvk_context.device, nvvk_context.compute_family_index, 0, &nvvk_context.compute_queue);
  vkGetDeviceQueue(nvvk_context.device, nvvk_context.transfer_family_index, 0, &nvvk_context.transfer_queue);
  vkGetDeviceQueue(nvvk_context.device, nvvk_context.present_family_index, 0, &nvvk_context.present_queue);
  vkGetDeviceQueue(nvvk_context.device, nvvk_context.graphics_and_compute_family_index, 0, &nvvk_context.graphics_and_compute_queue);

  nv_list_destroy(&queueFamilies);
}

void
nv_renderer_initialize_rendering_components(nv_renderer_t* rd, const nv_renderer_config* conf)
{
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (conf->m_vsync_enabled)
  {
    switch (conf->m_buffer_mode)
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

  nv_gpu_swapchain_create_info surface_create_info = nv_zero_init(nv_gpu_swapchain_create_info);
  surface_create_info.m_extent.width               = rd->m_render_extent.m_width;
  surface_create_info.m_extent.height              = rd->m_render_extent.m_height;
  surface_create_info.m_present_mode               = present_mode;
  surface_create_info.m_format                     = nvvk_context.swap_chain_image_format;
  surface_create_info.m_color_space                = nvvk_context.swap_chain_color_space;
  surface_create_info.m_image_count                = nvvk_context.swap_chain_image_count;
  nv_gpu_create_swapchain(&surface_create_info, &rd->m_swapchain);

  VkSampleCountFlagBits conf_samples;
  if (conf->m_samples == NOVA_SAMPLE_COUNT_MAX_SUPPORTED)
  {
    conf_samples = nvvk_context.MAX_SAMPLES;
  }
  else
  {
    conf_samples = (VkSampleCountFlagBits)conf->m_samples;
  }
  const VkSampleCountFlagBits _samples = conf->m_multisampling_enable ? conf_samples : VK_SAMPLE_COUNT_1_BIT;
  nvvk_context.samples                 = _samples;

  if (conf->m_multisampling_enable)
  {
    nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING;
    rd->m_attachment_count++;
  }
  nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK;
  nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_CULLING;
  rd->m_attachment_count++;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = nvvk_context.graphics_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  nvvk_result_check(vkCreateCommandPool(nvvk_context.device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &rd->m_command_pool));

  const int frames_in_flight = 1 + (int)conf->m_buffer_mode;

  nv_renderer_frame_render_info data = nv_zero_init(nv_renderer_frame_render_info);
  for (int i = 0; i < frames_in_flight; i++)
  {
    nv_list_push_back(&rd->m_draw_cmd_buffers, &data);
    nv_list_push_back(&rd->m_render_data, &data);
  }

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = frames_in_flight;
  cmdAllocInfo.commandPool                 = rd->m_command_pool;
  nvvk_result_check(vkAllocateCommandBuffers(nvvk_context.device, &cmdAllocInfo, (VkCommandBuffer*)nv_list_data(&rd->m_draw_cmd_buffers)));

  rd->m_depth_buffer_format = nv_format_to_vk_format(NOVA_FORMAT_D32); // replace (probably)

  nv_gpu_render_pass_create_info rpi = nv_zero_init(nv_gpu_render_pass_create_info);
  rpi.m_format                       = nvvk_context.swap_chain_image_format;
  rpi.m_depth_buffer_format          = nv_vk_format_to_nv_format(rd->m_depth_buffer_format);
  rpi.m_subpass                      = 0;
  rpi.m_samples                      = nvvk_context.samples;
  nv_gpu_create_render_pass(&rpi, &rd->m_render_pass, nv_gpu_vk_flag_register);

  create_optional_images(rd);
  create_framebuffers_and_swapchain_image_views(rd);
}

int
nv_renderer_init(const nv_renderer_config* conf, nv_renderer_t* dst)
{
  nv_assert_and_ret(conf != NULL, -1);
  nv_assert_and_ret(conf->m_initial_window_size.m_width != 0, -1);
  nv_assert_and_ret(conf->m_initial_window_size.m_height != 0, -1);
  nv_assert_and_ret(conf->m_samples != 0, -1);
  nv_assert_and_ret(dst != NULL, -1);

  nv_bzero(dst, sizeof(nv_renderer_t));

  if (conf->m_multisampling_enable == 1)
  {
    nv_push_error("config nvvk_context.samples must not be 1 if multisampling is enabled.");
    nv_assert(conf->m_samples != NOVA_SAMPLE_COUNT_1_SAMPLES);
  }

  // how many frames the renderer will render at once
  int frames_in_flight = (1 + (int)conf->m_buffer_mode);
  if (frames_in_flight <= 0)
  {
    frames_in_flight = 1;
  }

  nv_list_init(sizeof(nv_gpu_sampler*), 4, nv_allocator_get_default(), &dst->m_samplers);
  nv_list_init(sizeof(nv_draw_call_t), 4, nv_allocator_get_default(), &dst->m_drawcalls);
  nv_list_init(sizeof(VkCommandBuffer), frames_in_flight, nv_allocator_get_default(), &dst->m_draw_cmd_buffers);
  nv_list_init(sizeof(nv_renderer_frame_render_info), frames_in_flight, nv_allocator_get_default(), &dst->m_render_data);

  nv_assert(nv_list_is_initialized(&dst->m_samplers) == 0);
  nv_assert(nv_list_is_initialized(&dst->m_drawcalls) == 0);
  nv_assert(nv_list_is_initialized(&dst->m_draw_cmd_buffers) == 0);
  nv_assert(nv_list_is_initialized(&dst->m_render_data) == 0);

  if (conf->m_multisampling_enable)
  {
    dst->m_flags |= NOVA_RENDERER_MULTISAMPLING_ENABLE;
  }
  if (conf->m_window_resizable)
  {
    dst->m_flags |= NOVA_RENDERER_WINDOW_RESIZABLE;
  }
  if (conf->m_vsync_enabled)
  {
    dst->m_flags |= NOVA_RENDERER_VSYNC_ENABLE;
  }
  dst->m_buffer_mode = conf->m_buffer_mode;

  dst->m_render_extent.m_width  = conf->m_initial_window_size.m_width;
  dst->m_render_extent.m_height = conf->m_initial_window_size.m_height;

  nv_renderer_initialize_graphics_singleton();
  nv_renderer_initialize_rendering_components(dst, conf);

  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

    const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&dst->m_render_data, i);
    nvvk_result_check(vkCreateSemaphore(nvvk_context.device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->m_render_finish_semaphore));
    nvvk_result_check(vkCreateSemaphore(nvvk_context.device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->m_image_available_semaphore));
    nvvk_result_check(vkCreateFence(nvvk_context.device, &fenceCreateInfo, NOVA_VK_ALLOCATOR, &data->m_in_flight_fence));
  }

  nv_descriptor_pool_init(&g_pool);

  const unsigned char empty_data[3] = { 255, 255, 255 }; // fill rgb with 255 so it's white
  nv_sprite_empty                   = nv_sprite_load_from_memory(dst, empty_data, 1, 1, NOVA_FORMAT_RGB8);
  nv_assert(nv_sprite_empty != NULL);

  nv_log_info("loaded backup sprite\n");

  nv_camera_init(&camera);

  ctext_init(dst);
  nv_vk_bake_global_pipelines(dst);
  nv_renderer_prepare_quad_renderer(dst);

  return 0;
}

void
_nvvk_renderer_resize(nv_renderer_t* rd)
{
  vkDeviceWaitIdle(nvvk_context.device);

  const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

  const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

  // adhoc method of resetting them
  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);
    vkDestroySemaphore(nvvk_context.device, data->m_image_available_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroySemaphore(nvvk_context.device, data->m_render_finish_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroyFence(nvvk_context.device, data->m_in_flight_fence, NOVA_VK_ALLOCATOR);

    nv_gpu_destroy_texture(&data->m_depth_image);

    data->m_image_available_semaphore = NULL;
    data->m_render_finish_semaphore   = NULL;
    data->m_in_flight_fence           = NULL;
  }
  nv_gpu_free_memory(&rd->m_depth_image_memory);

  if (rd->m_color_image.m_image)
  {
    nv_gpu_destroy_texture(&rd->m_color_image);
    nv_gpu_free_memory(&rd->m_color_image_memory);
  }

  for (u32 i = 0; i < nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);
    vkDestroyImageView(
        nvvk_context.device,
        nv_gpu_texture_get_view(&data->m_sc_image),
        NOVA_VK_ALLOCATOR); // as the view was silently smushed into the
                            // structure, we just kinda smush it out as well.
    nv_gpu_destroy_framebuffer(&data->m_color_framebuffer);
  }
  nv_list_clear(&rd->m_render_data);

  i32 w, h;
  SDL_Vulkan_GetDrawableSize(nvvk_context.window, &w, &h);

  VkSurfaceCapabilitiesKHR surface_capabilities;
  nvvk_result_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(nvvk_context.phys_device, nvvk_context.surface, &surface_capabilities));

  const u32 min_width  = surface_capabilities.minImageExtent.width;
  const u32 min_height = surface_capabilities.minImageExtent.height;

  const u32 max_width  = surface_capabilities.maxImageExtent.width;
  const u32 max_height = surface_capabilities.maxImageExtent.height;

  w = NVM_CLAMP((u32)w, min_width, max_width);
  h = NVM_CLAMP((u32)h, min_height, max_height);

  rd->m_render_extent = (nv_extent2d){ w, h };

  VkSwapchainKHR old_swapchain = rd->m_swapchain;

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (rd->m_flags & NOVA_RENDERER_VSYNC_ENABLE)
  {
    switch (rd->m_buffer_mode)
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
  swapchain_create_info.m_extent.width               = rd->m_render_extent.m_width;
  swapchain_create_info.m_extent.height              = rd->m_render_extent.m_height;
  swapchain_create_info.m_present_mode               = present_mode;
  swapchain_create_info.m_format                     = nvvk_context.swap_chain_image_format;
  swapchain_create_info.m_color_space                = nvvk_context.swap_chain_color_space;
  swapchain_create_info.m_image_count                = nvvk_context.swap_chain_image_count;
  swapchain_create_info.m_old_swapchain              = old_swapchain;
  nv_gpu_create_swapchain(&swapchain_create_info, &rd->m_swapchain);
  vkDestroySwapchainKHR(nvvk_context.device, old_swapchain, NOVA_VK_ALLOCATOR);

  nv_list_resize(&rd->m_render_data, nvvk_context.swap_chain_image_count);
  create_optional_images(rd);
  create_framebuffers_and_swapchain_image_views(rd);

  for (int i = 0; i < (int)nvvk_context.swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, i);
    nvvk_result_check(vkCreateSemaphore(nvvk_context.device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->m_render_finish_semaphore));
    nvvk_result_check(vkCreateSemaphore(nvvk_context.device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->m_image_available_semaphore));
    nvvk_result_check(vkCreateFence(nvvk_context.device, &fenceCreateInfo, NOVA_VK_ALLOCATOR, &data->m_in_flight_fence));
  }

  _nv_reset_frame_buffer_resized();
}

bool
nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color)
{
  nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, rd->m_frame);

  vkWaitForFences(nvvk_context.device, 1, &data->m_in_flight_fence, VK_TRUE, UINT64_MAX);

  const VkResult imageAcquireResult =
      vkAcquireNextImageKHR(nvvk_context.device, rd->m_swapchain, UINT64_MAX, data->m_image_available_semaphore, VK_NULL_HANDLE, &rd->m_image_index);

  const VkCommandBuffer drawBuffer = *(VkCommandBuffer*)nv_list_get(&rd->m_draw_cmd_buffers, rd->m_frame);

  if (imageAcquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAcquireResult == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized())
  {
    _nvvk_renderer_resize(rd);
    return false;
  }
  else if (imageAcquireResult != VK_SUCCESS && imageAcquireResult != VK_SUBOPTIMAL_KHR)
  {
    nv_push_error("Failed to acquire image from swapchain");
    return false;
  }

  vkResetFences(nvvk_context.device, 1, &data->m_in_flight_fence);

  // I do, in fact, care about my beloveds

  nv_renderer_frame_render_info* image_render_info = (nv_renderer_frame_render_info*)(nv_list_get(&rd->m_render_data, rd->m_image_index));

  VkFramebuffer framebuffer = image_render_info->m_color_framebuffer.m_handle;

  // Why was this static?
  VkRenderPassBeginInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass      = rd->m_render_pass,
    .framebuffer     = framebuffer,
    .renderArea      = (VkRect2D){ .extent = (VkExtent2D){ rd->m_render_extent.m_width, rd->m_render_extent.m_height }, .offset = nv_zero_init(VkOffset2D) },
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
    .width    = (float)rd->m_render_extent.m_width,
    .height   = (float)rd->m_render_extent.m_height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(drawBuffer, 0, 1, &viewport);

  VkRect2D scissor = {
    .offset = (VkOffset2D){ 0, 0 },
    .extent = (VkExtent2D){ (unsigned)rd->m_render_extent.m_width, (unsigned)rd->m_render_extent.m_height },
  };
  vkCmdSetScissor(drawBuffer, 0, 1, &scissor);

  return true;
}

#define V2F_TO_V3F(v) ((vec3f){ (v).x, (v).y, 0.0f })

void
nv_renderer_end(nv_renderer_t* rd)
{
  const VkCommandBuffer drawBuffer = *(VkCommandBuffer*)nv_list_get(&rd->m_draw_cmd_buffers, rd->m_frame);

  for (int i = 0; i < (int)nv_list_size(&rd->m_ctext->m_labels); i++)
  {
    ctext_label_t* label = nv_list_get(&rd->m_ctext->m_labels, i);

    nv_sprite_renderer* spr_rd = nv_object_get_sprite_renderer(label->m_obj);

    ctext_text_render_info_t r_info = ctext_init_text_render_info();
    r_info.m_position               = V2F_TO_V3F(nv_object_get_position(label->m_obj));
    r_info.m_color                  = spr_rd->m_color;
    r_info.m_horizontal             = label->m_h_align;
    r_info.m_vertical               = label->m_v_align;
    ctext_render(label->m_fnt, &r_info, "%s", nv_string_data(&label->m_text));
  }
  ctext_flush_renders(rd);

  // nvui internally checks whether it has been initialized or not
  nvui_render(rd);
  _nv_renderer_flush_renders(rd);

  vkCmdEndRenderPass(drawBuffer);
  vkEndCommandBuffer(drawBuffer);

  VkSubmitInfo submitInfo = nv_zero_init(VkSubmitInfo);
  submitInfo.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  const nv_renderer_frame_render_info* data               = (nv_renderer_frame_render_info*)nv_list_get(&rd->m_render_data, rd->m_frame);
  const VkSemaphore                    waitSemaphores[]   = { data->m_image_available_semaphore };
  const VkSemaphore                    signalSemaphores[] = { data->m_render_finish_semaphore };

  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.pWaitDstStageMask      = waitStages;

  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores    = waitSemaphores;

  const VkCommandBuffer buffers[] = { drawBuffer };
  submitInfo.commandBufferCount   = nv_arrlen(buffers);
  submitInfo.pCommandBuffers      = buffers;

  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores    = signalSemaphores;

  vkQueueSubmit(nvvk_context.present_queue, 1, &submitInfo, data->m_in_flight_fence);

  VkPresentInfoKHR presentInfo   = nv_zero_init(VkPresentInfoKHR);
  presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores    = signalSemaphores; // This is signalSemaphores so that this starts as
                                                     // soon as the signaled semaphores are signaled.
  presentInfo.pImageIndices  = &rd->m_image_index;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains    = &rd->m_swapchain;

  VkResult result = VK_SUCCESS;
  result          = vkQueuePresentKHR(nvvk_context.present_queue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized())
  {
    _nvvk_renderer_resize(rd);
  }

  rd->m_frame = (rd->m_frame + 1) % nv_renderer_get_max_frames_in_flight(rd);
}

// nvgfx ^^

// engine
u32           MAX_SAMPLES;
unsigned char SUPPORTS_MULTISAMPLING;
flt_t         MAX_ANISOTROPY;

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

void
_VK_DEBUG_LOG(const char* fmt, ...)
{
  const char* preceder = " ";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, __FILE__, __LINE__, STR(nvvk_debug_messenger), preceder, fmt, 1);
  va_end(args);
}

static SDL_UNUSED VKAPI_ATTR VkBool32 VKAPI_CALL
nvvk_debug_messenger(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData)
{
  (void)messageSeverity;
  (void)messageType;
  (void)pUserData;

  _VK_DEBUG_LOG("%s\n", pCallbackData->pMessage);

  return VK_FALSE;
}

nv_list_t
setify(u32 i1, u32 i2, u32 i3, u32 i4)
{
  nv_list_t ret;
  nv_list_init(sizeof(u32), 4, nv_allocator_get_default(), &ret);
  u32 nums[4] = { i1, i2, i3, i4 };
  for (int j = 0; j < (int)nv_arrlen(nums); j++)
  {
    const u32 e          = nums[j];
    bool      already_in = false;
    for (int i = 0; i < (int)nv_list_size(&ret); i++)
    {
      if (e == *(u32*)nv_list_get(&ret, i))
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
nvvk_validate_layers(nv_allocator_t* ac)
{
  nv_assert_and_ret(ac != NULL, false);

  if (nv_arrlen(ValidationLayers) == 0)
  {
    return true;
  }

  bool validation_layers_available = true;

  uint32_t vk_layer_count = 0;
  vkEnumerateInstanceLayerProperties(&vk_layer_count, NULL);

  nv_list_t vk_layer_properties;
  nv_list_init(sizeof(VkLayerProperties), vk_layer_count, nv_allocator_get_default(), &vk_layer_properties);
  vkEnumerateInstanceLayerProperties(&vk_layer_count, (VkLayerProperties*)nv_list_data(&vk_layer_properties));

  for (int j = 0; j < (int)nv_arrlen(ValidationLayers); j++)
  {
    const char* layer       = ValidationLayers[j];
    bool        layer_found = false;
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      const VkLayerProperties* vk_layer = &((VkLayerProperties*)nv_list_data(&vk_layer_properties))[i];
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
    nv_push_error("Failed to initialize validation layers. Requested layers:");
    for (int i = 0; i < (int)nv_arrlen(ValidationLayers); i++)
    {
      nv_push_error("\t%s", ValidationLayers[i]);
    }

    nv_push_error("Available Layers:");
    for (uint32_t i = 0; i < vk_layer_count; i++)
    {
      nv_push_error("\t%s", ((VkLayerProperties*)nv_list_data(&vk_layer_properties))[i].layerName);
    }

    /* We add the missing layers next */
    nv_push_error("But nvvk_context.instance asked for (i.e. are not available):");

    nv_list_t missing_layers;
    nv_list_init(sizeof(const char*), 16, ac, &missing_layers);

    for (int i = 0; i < (int)nv_arrlen(ValidationLayers); i++)
    {
      const char* layer          = ValidationLayers[i];
      bool        layerAvailable = false;
      for (uint32_t j = 0; j < vk_layer_count; j++)
      {
        const VkLayerProperties* vk_layer = &((VkLayerProperties*)nv_list_data(&vk_layer_properties))[j];
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
    for (int i = 0; i < (int)nv_list_size(&missing_layers); i++)
    {
      const char* layer = *(const char**)nv_list_get(&missing_layers, i);
      if (layer)
      {
        nv_push_error("\t%s", layer);
      }
      else
      {
        nv_push_error("\t(Layer access failed/NULL)");
      }
    }

    nv_push_error("Validation layers have NOT been enabled! Do you have the vulkan-validation-layers package installed?");

    nv_list_destroy(&missing_layers);
  }

  nv_list_destroy(&vk_layer_properties);

  /* true, as program will exit if we failed validation */
  return validation_layers_available;
}

static inline void
nvvk_setup_debug_messenger(void)
{
  VkDebugUtilsMessengerCreateInfoEXT create_info = nv_zero_init(VkDebugUtilsMessengerCreateInfoEXT);
  create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = nvvk_debug_messenger;

  PFN_vkCreateDebugUtilsMessengerEXT _CreateDebugUtilsMessenger =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(nvvk_context.instance, "vkCreateDebugUtilsMessengerEXT");

  if (!_CreateDebugUtilsMessenger)
  {
    nv_push_error("vkCreateDebugUtilsMessengerEXT exported function pointer not found."
                  " Either the " VK_EXT_DEBUG_UTILS_EXTENSION_NAME " extension was not loaded or your driver does not support it.");
    nv_push_error("The debug messenger failed to initialize.");
    return;
  }

  VkResult r;
  if ((r = _CreateDebugUtilsMessenger(nvvk_context.instance, &create_info, NOVA_VK_ALLOCATOR, &nvvk_context.debug_messenger)) != VK_SUCCESS)
  {
    nv_push_error("Vulkan debug messenger could not start. err %i", r);
    return;
  }

  /* Submit a message just to notify the user */
  vkSubmitDebugUtilsMessageEXT(
      nvvk_context.instance,
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
      &(VkDebugUtilsMessengerCallbackDataEXT){
          .sType    = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
          .pMessage = "The vulkan debug messenger has been set up",
      });
}

/* returned_valid_extensions contains a list of valid extensions */
static inline void
nvvk_get_valid_extensions(nv_list_t* returned_valid_extensions)
{
  unsigned char      buffer[1024];
  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buffer, sizeof(buffer));

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(nvvk_context.window, &SDLExtensionCount, NULL) == SDL_TRUE);
  const char** sdl_extensions = ac.m_alloc(&ac, 1, sizeof(const char*) * SDLExtensionCount);
  nv_assert(SDL_Vulkan_GetInstanceExtensions(nvvk_context.window, &SDLExtensionCount, sdl_extensions) == SDL_TRUE);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
  // VkExtensionProperties is too big to fit on the stack
  nv_list_t vk_extensions;
  nv_list_init(sizeof(VkExtensionProperties), extensionCount, nv_allocator_get_default(), &vk_extensions);
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, (VkExtensionProperties*)nv_list_data(&vk_extensions));

  for (int i = 0; i < (int)NUM_REQUIRED_INSTANCE_EXTENSIONS; i++)
  {
    const char* ext = REQUIRED_INSTANCE_EXTENSIONS[i];
    nv_list_push_back(returned_valid_extensions, &ext);
  }

  for (int i = 0; i < (int)SDLExtensionCount; i++)
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

  ac.m_free(&ac, sdl_extensions);
  nv_list_destroy(&vk_extensions);
}

VkInstance
nvvk_create_instance(const char* title)
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

  unsigned char      buffer[1024];
  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buffer, sizeof(buffer));

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(nvvk_context.window, &SDLExtensionCount, NULL) == SDL_TRUE);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);

  nv_list_t enabled_extensions;
  nv_list_init(sizeof(const char*), (NUM_REQUIRED_INSTANCE_EXTENSIONS + SDLExtensionCount + extensionCount + NUM_WANTED_INSTANCE_EXTENSIONS), &ac, &enabled_extensions);

  nvvk_get_valid_extensions(&enabled_extensions);

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

  validation_layers_available = nvvk_validate_layers(&ac);
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

  nvvk_result_check(vkCreateInstance(&instance_create_info, NOVA_VK_ALLOCATOR, &nvvk_context.instance));

  /* Load the volk functions as soon as available, we need them to set up the debug messengers and stuff. */
  volkLoadInstance(nvvk_context.instance);

#ifdef DEBUG
  nvvk_setup_debug_messenger();

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

  return nvvk_context.instance;
}

void
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
      "Driver Vendor: %s Driver Version: %u.%u.%u Device ID: %#X\n",
      device_driver_vendor,
      VK_VERSION_MAJOR(properties.driverVersion),
      VK_VERSION_MINOR(properties.driverVersion),
      VK_VERSION_PATCH(properties.driverVersion),
      properties.deviceID);
}

VkPhysicalDevice
_nvvk_choose_physical_device(VkInstance instance, VkSurfaceKHR surface)
{
  uint32_t phys_device_count = 0;

  VkResult r = VK_SUCCESS;

  if ((r = vkEnumeratePhysicalDevices(instance, &phys_device_count, NULL)) != VK_SUCCESS)
  {
    nv_log_and_abort("Error fetching physical devices. VkResult=%i\n", r);
  }

  if (phys_device_count == 0)
  {
    nv_log_and_abort("Huuuhhh??? No physical devices found? Are you running this \n"
                     "on a banana???");
  }

  nv_list_t physical_devices;
  nv_list_init(sizeof(VkPhysicalDevice), phys_device_count, nv_allocator_get_default(), &physical_devices);
  vkEnumeratePhysicalDevices(instance, &phys_device_count, (VkPhysicalDevice*)nv_list_data(&physical_devices));

  for (u32 i = 0; i < phys_device_count; i++)
  {
    const VkPhysicalDevice device = ((VkPhysicalDevice*)nv_list_data(&physical_devices))[i];

    uint32_t format_count = 0;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL));

    uint32_t present_mode_count = 0;
    nvvk_result_check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL));

    bool extensionsAvailable = true;

    uint32_t extension_count = 0;
    nvvk_result_check(vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL));
    nv_list_t available_extensions;
    nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_get_default(), &available_extensions);
    nvvk_result_check(vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&available_extensions)));

    for (int i = 0; i < (int)NUM_WANTED_DEVICE_EXTENSIONS; i++)
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
        nv_push_error("Failed to validate extension with name: %s", extension);
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

  VkPhysicalDevice fallback = ((VkPhysicalDevice*)nv_list_data(&physical_devices))[0];

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(fallback, &properties);

  nv_push_error("No nvvk_context.device found. Falling back to nvvk_context.device \"%s\".", properties.deviceName);

  nvvk_print_device_info(fallback);

  nv_list_destroy(&physical_devices);

  return fallback;
}

// WARNING: does not init available_extensions itself!!!
static inline void
nvvk_get_valid_device_extensions(nv_list_t* available_extensions)
{
  u32 extension_count = 0;
  vkEnumerateDeviceExtensionProperties(nvvk_context.phys_device, NULL, &extension_count, NULL);
  nv_list_t extensions;
  nv_list_init(sizeof(VkExtensionProperties), extension_count, nv_allocator_get_default(), &extensions);
  vkEnumerateDeviceExtensionProperties(nvvk_context.phys_device, NULL, &extension_count, (VkExtensionProperties*)nv_list_data(&extensions));

  for (int i = 0; i < (int)NUM_WANTED_DEVICE_EXTENSIONS; i++)
  {
    const char* wanted = WANTED_DEVICE_EXTENSIONS[i];
    for (u32 i = 0; i < extension_count; i++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[i];
      if (nv_strcmp(wanted, ext.extensionName) == 0)
      {
        const char* ext_name_copy = nv_strdup(ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
      }
    }
  }

  for (int i = 0; i < (int)NUM_REQUIRED_DEVICE_EXTENSIONS; i++)
  {
    const char* required  = REQUIRED_DEVICE_EXTENSIONS[i];
    bool        validated = false;
    for (u32 i = 0; i < extension_count; i++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_list_data(&extensions))[i];
      if (nv_strcmp(required, ext.extensionName) == 0)
      {
        char* ext_name_copy = nv_strdup(ext.extensionName);
        nv_list_push_back(available_extensions, (void*)&ext_name_copy);
        validated = true;
      }
    }

    if (!validated)
    {
      nv_push_error("Failed to validate required extension with name %s", required);
    }
  }

  nv_list_destroy(&extensions);
}

static inline void
nvvk_validate_queues(nv_list_t* queue_create_infos)
{
  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(nvvk_context.phys_device, &queue_count, NULL);
  nv_list_t queue_families;
  nv_list_init(sizeof(VkQueueFamilyProperties), queue_count, nv_allocator_get_default(), &queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(nvvk_context.phys_device, &queue_count, (VkQueueFamilyProperties*)nv_list_data(&queue_families));

  // Clang loves complaining about these.
  u32 graphics_family = 0, present_family = 0, compute_family = 0, transfer_family = 0;
  (void)graphics_family, (void)present_family, (void)compute_family, (void)transfer_family;

  bool found_graphics_family = false, found_present_family = false, found_compute_family = false, found_transfer_family = false;

  u32 i = 0;
  for (int j = 0; j < (int)nv_list_size(&queue_families); j++)
  {
    const VkQueueFamilyProperties queue_family    = ((VkQueueFamilyProperties*)nv_list_data(&queue_families))[j];
    VkBool32                      present_support = false;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceSupportKHR(nvvk_context.phys_device, i, nvvk_context.surface, &present_support));

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

  for (int i = 0; i < (int)nv_list_size(&unique_queue_families); i++)
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

VkDevice
nvvk_create_device(void)
{
  nv_list_t enabled_extensions;
  nv_list_init(sizeof(const char*), NUM_WANTED_DEVICE_EXTENSIONS + NUM_WANTED_DEVICE_EXTENSIONS, nv_allocator_get_default(), &enabled_extensions);
  nvvk_get_valid_device_extensions(&enabled_extensions);

  nv_list_t queue_create_infos;
  nv_list_init(sizeof(VkDeviceQueueCreateInfo), 0, nv_allocator_get_default(), &queue_create_infos);
  nvvk_validate_queues(&queue_create_infos);

  const float queue_priority = 1.0F;
  for (int i = 0; i < (int)nv_list_size(&queue_create_infos); i++)
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

  nvvk_result_check(vkCreateDevice(nvvk_context.phys_device, &deviceCreateInfo, NOVA_VK_ALLOCATOR, &nvvk_context.device));

#ifndef NDEBUG
  nv_log_info(" Enabled device extensions: [ ");
  for (size_t i = 0; i < nv_list_size(&enabled_extensions); i++)
  {
    nv_printf("\"%s\", ", *(const char**)nv_list_get(&enabled_extensions, i));
  }
  nv_printf(" ]\n");
#endif

  for (int i = 0; i < (int)nv_list_size(&enabled_extensions); i++)
  {
    const char* ext_name_allocated = *(const char**)nv_list_get(&enabled_extensions, i);
    nv_free((void*)ext_name_allocated);
  }
  nv_list_destroy(&enabled_extensions);
  nv_list_destroy(&queue_create_infos);

  return nvvk_context.device;
}

void
nvvk_context_initialize(nvvk_context_t* ctx)
{
  ctx->instance = nvvk_create_instance(SDL_GetWindowTitle(ctx->window));
  nv_assert(ctx->instance != NULL);

  if (SDL_Vulkan_CreateSurface(ctx->window, ctx->instance, &ctx->surface) != SDL_TRUE)
  {
    nv_log_and_abort("Surface creation failed.\nSDL reports: %s\n", SDL_GetError());
  }

  ctx->phys_device = _nvvk_choose_physical_device(ctx->instance, ctx->surface);

  ctx->device = nvvk_create_device();
  nv_assert(ctx->device != NULL);

  volkLoadDevice(ctx->device);

  // if ()

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(ctx->phys_device, &props);

  MAX_ANISOTROPY         = props.limits.maxSamplerAnisotropy;
  SUPPORTS_MULTISAMPLING = true;

  const VkSampleCountFlags vk_samples = props.limits.framebufferColorSampleCounts;
  if (vk_samples & VK_SAMPLE_COUNT_64_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_64_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_32_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_32_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_16_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_16_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_8_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_8_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_4_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_4_BIT;
  }
  else if (vk_samples & VK_SAMPLE_COUNT_2_BIT)
  {
    MAX_SAMPLES = VK_SAMPLE_COUNT_2_BIT;
  }
  else
  {
    MAX_SAMPLES            = VK_SAMPLE_COUNT_1_BIT;
    SUPPORTS_MULTISAMPLING = false;
  }
}

// engine

// ctext vv

/* I have no idea what any of this is */

void
_ctext_load_font_upload_glyph_atlas(nv_renderer_t* rd, const nv_texture_atlas_t* atlas, cfont_t* dst)
{
  nv_gpu_texture_create_info image_info = {
    .m_format      = NOVA_FORMAT_R8,
    .m_samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .m_type        = VK_IMAGE_TYPE_2D,
    .m_usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .m_extent      = (nv_extent3D){ .m_width = atlas->m_width, .m_height = atlas->m_height, .m_depth = 1 },
    .m_arraylayers = 1,
    .m_miplevels   = 1,
  };
  nv_gpu_create_texture(&image_info, &dst->m_texture);

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(nvvk_context.device, nv_gpu_texture_get(&dst->m_texture), &imageMemoryRequirements);

  nv_gpu_allocate_memory(imageMemoryRequirements.size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &dst->m_texture_mem);
  nv_gpu_bind_texture_to_memory(&dst->m_texture_mem, 0, &dst->m_texture);

  const size_t atlas_w = atlas->m_width;
  const size_t atlas_h = atlas->m_height;

  nv_image_t atlas_img = (nv_image_t){ .m_width = atlas_w, .m_height = atlas_h, .m_format = NOVA_FORMAT_R8, .m_data = (unsigned char*)atlas->m_data };
  nv_gpu_write_to_texture(&dst->m_texture, &atlas_img);

  const nv_gpu_sampler_create_info sampler_info = {
    .m_filter       = VK_FILTER_LINEAR,
    .m_mipmap_mode  = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .m_address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
  };
  nv_gpu_create_sampler(rd, &sampler_info, &dst->m_sampler);
}

void
_ctext_load_font_update_descriptors(nv_ctext_module* ctext, cfont_t* dst)
{
  const VkDescriptorImageInfo ctext_bitmap_image_info = {
    .sampler     = nv_gpu_sampler_get(&dst->m_sampler),
    .imageView   = nv_gpu_texture_get_view(&dst->m_texture),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet writeSet = { .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet          = ctext->m_desc_set->m_set,
                                    .dstBinding      = 0,
                                    .descriptorCount = 1,
                                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo      = &ctext_bitmap_image_info };
  for (int i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    writeSet.dstArrayElement = i;
    nv_descriptor_set_submit_write(ctext->m_desc_set, &writeSet);
  }
}

void
ctext_load_font(nv_renderer_t* rdr, const char* font_path, int scale, cfont_t* dst)
{
  if (!rdr || !dst)
  {
    nv_push_error("rdr or dst is NULL!");
    return;
  }

  if (scale <= 0)
  {
    nv_push_error("attempting to load a font with 0 fontscale.");
    return;
  }

  *dst = nv_zero_init(cfont_t);

  fontc_file_t f_file;
  if (fontc_load_font(font_path, scale, &f_file) != 0)
  {
    nv_push_error("There was an error loading the font file. Skipping");
    return;
  }

  // Store a pointer to the font for future reference
  *(cfont_t**)nv_list_push_empty(&rdr->m_ctext->m_fonts) = dst;

  dst->m_rd = rdr;

  nv_hashmap_init(256, sizeof(u32), sizeof(ctext_glyph_t), nv_hash_murmur3, nv_allocator_get_default(), &dst->m_glyph_map);
  nv_list_init(sizeof(ctext_drawcall_t), 4, nv_allocator_get_default(), &dst->m_drawcalls);

  nv_texture_atlas_t atlas;

  dst->m_line_height = f_file.m_header.m_line_height;
  dst->m_space_width = f_file.m_header.m_space_width;
  atlas.m_width      = f_file.m_header.m_bmpwidth;
  atlas.m_height     = f_file.m_header.m_bmpheight;
  atlas.m_data       = f_file.m_bitmap;

  for (int i = 0; i < f_file.m_header.m_numglyphs; i++)
  {
    ctext_glyph_t glyph = {
      .m_x0      = f_file.m_glyphs[i].m_x0,
      .m_x1      = f_file.m_glyphs[i].m_x1,
      .m_y0      = f_file.m_glyphs[i].m_y0,
      .m_y1      = f_file.m_glyphs[i].m_y1,
      .m_l       = f_file.m_glyphs[i].m_l,
      .m_r       = f_file.m_glyphs[i].m_r,
      .m_b       = f_file.m_glyphs[i].m_b,
      .m_t       = f_file.m_glyphs[i].m_t,
      .m_advance = f_file.m_glyphs[i].m_advance,
    };
    u32 codepoint = f_file.m_glyphs[i].m_codepoint;
    nv_hashmap_insert(&dst->m_glyph_map, &codepoint, &glyph, NULL);
  }

  _ctext_load_font_upload_glyph_atlas(rdr, &atlas, dst);
  _ctext_load_font_update_descriptors(rdr->m_ctext, dst);

  fontc_clean_font_file(&f_file);

  const size_t initial_font_gpu_buffer_size = 1024;

  dst->m_allocated_size = initial_font_gpu_buffer_size;
  nv_gpu_allocate_memory(initial_font_gpu_buffer_size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &dst->m_buffer_mem);
  nv_gpu_create_buffer(initial_font_gpu_buffer_size, NOVA_GPU_ALIGNMENT_UNNECESSARY, NOVA_GPU_BUFFER_USAGE_VERTEX_BUFFER | NOVA_GPU_BUFFER_USAGE_INDEX_BUFFER, &dst->m_buffer);

  if (ctext_validate_font(dst) != 0)
  {
    // nv_push_error("Broken font. Something has gone horribly wrong");
    return;
  }
}

int
ctext_validate_font(const cfont_t* fnt)
{
  // If the renderer of the font has died, die along with the renderer.
  if (!fnt || !fnt->m_rd)
  {
    return -1;
  }
  // TODO: Add support for a constant canary across the whole project?
  if (fnt->m_glyph_map.m_canary != CONT_CANARY || fnt->m_drawcalls.m_canary != CONT_CANARY)
  {
    return -1;
  }
  return 0;
}

void
ctext_destroy_font(cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
    return;
  }

  nv_gpu_destroy_texture(&fnt->m_texture);
  nv_gpu_free_memory(&fnt->m_texture_mem);

  if (fnt->m_buffer.m_buffer != NULL)
  {
    nv_gpu_destroy_buffer(&fnt->m_buffer);
    nv_gpu_free_memory(&fnt->m_buffer_mem);
  }
  nv_list_destroy(&fnt->m_drawcalls);
  nv_hashmap_destroy(&fnt->m_glyph_map);
}

bool
_ctext_font_resize_buffer(cfont_t* fnt, size_t new_buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
    return false;
  }

  size_t new_allocation_size = 0;

  if (fnt->m_allocated_size < new_buffer_size)
  {
    new_allocation_size = NV_MAX(fnt->m_allocated_size * 2, new_buffer_size);
  }
  else if (new_buffer_size < (fnt->m_allocated_size / 3))
  {
    new_allocation_size = NV_MAX(fnt->m_allocated_size / 3, new_buffer_size);
  }
  else
  {
    return false;
  }

  vkDeviceWaitIdle(nvvk_context.device);

  if (fnt->m_buffer.m_buffer)
  {
    nv_gpu_destroy_buffer(&fnt->m_buffer);
    nv_gpu_free_memory(&fnt->m_buffer_mem);
  }

  nv_gpu_allocate_memory(new_allocation_size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE | NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &fnt->m_buffer_mem);

  nv_gpu_create_buffer(new_allocation_size, NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &fnt->m_buffer);
  nv_gpu_bind_buffer_to_memory(&fnt->m_buffer_mem, 0, &fnt->m_buffer);

  fnt->m_allocated_size = new_allocation_size;
  fnt->m_to_render      = 0;

  return true;
}

void
_ctext_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
    return;
  }

  const VkCommandBuffer cmd       = nv_renderer_get_draw_buffer(rd);
  const VkDeviceSize    offsets[] = { 0 };

  struct push_constants pc = nv_zero_init(struct push_constants);

  const VkPipeline       pipeline        = g_Pipelines.m_ctext.m_pipeline;
  const VkPipelineLayout pipeline_layout = g_Pipelines.m_ctext.m_pipeline_layout;

  const VkDescriptorSet sets[]           = { camera.m_sets->m_set, rd->m_ctext->m_desc_set->m_set };
  const uint32_t        camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);

  // Viewport && scissor are set by renderer so no need to set them here
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
  vkCmdBindVertexBuffers(cmd, 0, 1, &fnt->m_buffer.m_buffer, offsets);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindIndexBuffer(cmd, fnt->m_buffer.m_buffer, fnt->m_index_buffer_offset, VK_INDEX_TYPE_UINT32);

  size_t offset = 0;
  for (int i = 0; i < (int)nv_list_size(&fnt->m_drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->m_drawcalls, i);

    pc.m_model = drawcall->m_model;
    pc.m_scale = drawcall->m_scale;
    pc.m_color = drawcall->m_color;
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

    vkCmdDrawIndexed(cmd, drawcall->m_index_count, 1, 0, (int32_t)offset, 0);
    offset += drawcall->m_vertex_count;
  }
}

static nv_list_t
split_string_by_lines(const char* str)
{
  nv_list_t    result;
  char*        substr  = NULL;
  const size_t str_len = nv_strlen(str);
  size_t       i_start = 0;

  nv_list_init(sizeof(char*), 16, nv_allocator_get_default(), &result);

  // FIXED: consecutive newlines not being considered
  // they are now added as a single NULL terminator
  for (size_t i = 0; i < str_len; i++)
  {
    if ((str[i] == '\n' || str[i] == '\r') && (i - i_start) > 0)
    {
      substr = nv_substr(str, i_start, i - i_start);
      nv_list_push_back(&result, (void*)&substr);
      i_start = i + 1;
    }
  }

  substr = nv_substr(str, i_start, str_len - i_start);
  nv_list_push_back(&result, (void*)&substr);

  return result;
}

// static nv_list_t
// split_string_by_lines(char *str)
// {
//   nv_list_t result;
//   nv_list_init(sizeof(char*), 16, nv_allocator_get_default(), &result);

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
static void
ctext_get_text_size(const cfont_t* fnt, const char* str, flt_t* w, flt_t* h)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
    *w = FLT_MAX;
    *h = FLT_MAX;
    return;
  }

  flt_t width = 0.0f, height = fnt->m_line_height, prev_width = 0.0f;

  bool is_new_line = true;

  u32 codepoint = 0;

  while (*str)
  {
    codepoint = (u32)*str;
    switch (*str)
    {
      case ' ':
        width += fnt->m_space_width;
        is_new_line = 0;
        break;
      case '\t':
        width += fnt->m_space_width * 4.0f;
        is_new_line = 0;
        break;
      case '\n':
      case '\r':
        if (!is_new_line)
        {
          height += fnt->m_line_height;
          prev_width = NV_MAX(width, prev_width);
        }
        width       = 0.0f;
        is_new_line = 1;
        break;
      default:
      {
        const ctext_glyph_t* glyph = (ctext_glyph_t*)nv_hashmap_find(&fnt->m_glyph_map, &codepoint, NULL);
        if (!glyph)
        {
          break;
        }
        width += glyph->m_advance;
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
    // nv_push_error("Broken font");
    return -1;
  }

  u32 codepoint = 0;

  size_t iter = 0;

  const size_t string_length = nv_strlen(str);
  for (size_t i = 0; i < string_length; i++)
  {
    if (str[i] == ' ')
    {
      x += fnt->m_space_width * scale;
      continue;
    }
    if (str[i] == '\t')
    {
      x += fnt->m_space_width * 4.0f * scale;
      continue;
    }

    codepoint                  = (u32)str[i];
    const ctext_glyph_t* glyph = (const ctext_glyph_t*)nv_hashmap_find(&fnt->m_glyph_map, &codepoint, NULL);
    if (!glyph)
    {
      nv_log_info("no glyph when rendering char %i\n", str[i]);
      break;
    }
    const flt_t glyph_x0 = (glyph->m_x0 * scale) + x;
    const flt_t glyph_x1 = (glyph->m_x1 * scale) + x;
    const flt_t glyph_y0 = (glyph->m_y0 * scale) + y;
    const flt_t glyph_y1 = (glyph->m_y1 * scale) + y;

    const size_t          index_offset = (fnt->m_chars_drawn + iter) * 4;
    ctext_glyph_vertex_t* v_out        = drawcall->m_vertices + ((glyph_iter + iter) * 4);

    // clang-format off
    v_out[0] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y0, zpos}, (vec2f){glyph->m_l, glyph->m_b} };
    v_out[1] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y0, zpos}, (vec2f){glyph->m_r, glyph->m_b} };
    v_out[2] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y1, zpos}, (vec2f){glyph->m_r, glyph->m_t} };
    v_out[3] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y1, zpos}, (vec2f){glyph->m_l, glyph->m_t} };
    // clang-format on

    u32* i_out = drawcall->m_indices + ((glyph_iter + iter) * 6);
    i_out[0]   = index_offset;
    i_out[1]   = index_offset + 1;
    i_out[2]   = index_offset + 2;
    i_out[3]   = index_offset + 2;
    i_out[4]   = index_offset + 3;
    i_out[5]   = index_offset;

    x += glyph->m_advance * scale;
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

int
_ctext_gen_vertices(cfont_t* fnt, ctext_drawcall_t* drawcall, const ctext_text_render_info_t* pInfo, const char* str)
{
  if (!str || *str == 0) // nv_strlen == 0
  {
    return 1;
  }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
    return -1;
  }

  nv_list_t lines;

  flt_t text_w = 0.0f;
  flt_t text_h = 0.0f;
  flt_t scale  = 0.0f;
  flt_t ypos   = 0.0f;
  flt_t xpos   = 0.0f;

  const size_t old_chars_drawn    = fnt->m_chars_drawn;
  size_t       actual_chars_drawn = 0;

  lines = split_string_by_lines(str);

  scale = pInfo->m_scale;
  ctext_get_text_size(fnt, str, &text_w, NULL);
  text_h = -fnt->m_line_height * ((flt_t)nv_list_size(&lines) - 1.0f);

  if (pInfo->m_scale_for_fit)
  {
    flt_t scale_x = (pInfo->m_bbox.x) / text_w;
    flt_t scale_y = (pInfo->m_bbox.y) / text_h;
    // multiply with normal scale to get new scale
    scale *= fminf(scale_x, scale_y);
  }
  drawcall->m_scale = scale;

  text_w *= scale;
  text_h *= scale;

  ypos = pInfo->m_position.y;
  switch (pInfo->m_vertical)
  {
    case CTEXT_VERT_ALIGN_CENTER: ypos += (text_h + fnt->m_line_height * scale) / 2.0f; break;
    case CTEXT_VERT_ALIGN_BOTTOM: ypos += text_h; break;
    case CTEXT_VERT_ALIGN_TOP: ypos += fnt->m_line_height * scale; break;
    default: nv_push_error("Invalid vertical alignment. Specified (int)%u. (Implement?)", pInfo->m_vertical); break;
  }
  for (size_t i = 0; i < nv_list_size(&lines); i++, ypos += fnt->m_line_height * scale)
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

    xpos = pInfo->m_position.x;
    switch (pInfo->m_horizontal)
    {
      case CTEXT_HORI_ALIGN_CENTER: xpos -= text_w / 2.0f; break;
      case CTEXT_HORI_ALIGN_RIGHT: xpos -= text_w; break;
      case CTEXT_HORI_ALIGN_LEFT: break;
      default:
        __builtin_unreachable();
        nv_push_error("Invalid horizontal alignment. Specified (int)%u. (Implement?)", pInfo->m_horizontal);
        break;
    }
    actual_chars_drawn = fnt->m_chars_drawn - old_chars_drawn;
    fnt->m_chars_drawn += _ctext_render_line(
        fnt,
        line,
        drawcall,
        scale,
        pInfo->m_position.z,
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
void
_ctext_render_and_submit_drawcall(cfont_t* fnt, const ctext_text_render_info_t* pInfo, char* buffer, size_t buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    nv_push_error("Broken font");
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
  drawcall.m_vertices       = (ctext_glyph_vertex_t*)allocation;
  drawcall.m_index_offset   = (vertex_count * sizeof(ctext_glyph_vertex_t));
  drawcall.m_indices        = (u32*)((uchar*)allocation + drawcall.m_index_offset);

  drawcall.m_color = pInfo->m_color;
  drawcall.m_scale = pInfo->m_scale;
  drawcall.m_model = pInfo->m_model;

  drawcall.m_vertex_count = effective_length * 4;
  drawcall.m_index_count  = effective_length * 6;

  // can we not have a bounds check before making the vertices?
  // probably not..
  if (_ctext_gen_vertices(fnt, &drawcall, pInfo, buffer) != 0)
  {
    nv_free(allocation);
    return;
  }
  nv_list_push_back(&fnt->m_drawcalls, &drawcall);
}

void
ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...)
{
  if (!fnt || !pInfo || !fmt)
  {
    return;
  }

  // if (!nv_async_is_task_complete(&fnt->m_load_task)) { return; }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_push_error("Broken font");
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

  fnt->m_rendered_this_frame = 1;
}

void
_ctext_upload_vertices_and_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  u32 total_vertex_byte_size = 0;
  u32 total_index_count      = 0;

  for (int i = 0; i < (int)nv_list_size(&fnt->m_drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->m_drawcalls, i);
    total_vertex_byte_size += drawcall->m_vertex_count * sizeof(ctext_glyph_vertex_t);
    total_index_count += drawcall->m_index_count;
  }

  if (total_vertex_byte_size == 0 || total_index_count == 0)
  {
    return;
  }

  const u32 total_index_byte_size = total_index_count * sizeof(u32);
  const u32 total_buffer_size     = total_index_byte_size + total_vertex_byte_size;

  const bool fnt_buffer_resized = _ctext_font_resize_buffer(fnt, total_buffer_size);

  if (fnt->m_to_render && !fnt_buffer_resized)
  {
    _ctext_render_drawcalls(rd, fnt);
  }

  uint8_t* mapped = NULL;
  nv_gpu_map_memory(&fnt->m_buffer_mem, total_buffer_size, 0, (void**)&mapped);

  if (mapped == NULL)
  {
    nv_push_error("mapping font buffer memory failed");
    return;
  }

  // this may be dumb but I am too

  u32 vertex_copy_iterator = 0;
  u32 index_copy_iterator  = 0;
  for (int i = 0; i < (int)nv_list_size(&fnt->m_drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->m_drawcalls, i);
    nv_memcpy(mapped + vertex_copy_iterator, drawcall->m_vertices, drawcall->m_vertex_count * sizeof(ctext_glyph_vertex_t));
    nv_memcpy(mapped + total_vertex_byte_size + index_copy_iterator, drawcall->m_indices, drawcall->m_index_count * sizeof(u32));
    vertex_copy_iterator += drawcall->m_vertex_count * sizeof(ctext_glyph_vertex_t);
    index_copy_iterator += drawcall->m_index_count * sizeof(u32);
  }
  nv_gpu_unmap_memory(&fnt->m_buffer_mem);

  fnt->m_index_buffer_offset = total_vertex_byte_size;
  fnt->m_index_count         = total_index_count;
  fnt->m_to_render           = true;
}

void
_ctext_flush_font(nv_renderer_t* rd, cfont_t* fnt)
{
  if (!fnt->m_rendered_this_frame)
  {
    return;
  }
  else
  {
    fnt->m_rendered_this_frame = 0;
  }

  _ctext_upload_vertices_and_render_drawcalls(rd, fnt);
  fnt->m_chars_drawn = 0;

  for (int i = 0; i < (int)nv_list_size(&fnt->m_drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_list_get(&fnt->m_drawcalls, i);
    if (drawcall && drawcall->m_vertices)
    {
      nv_free(drawcall->m_vertices);
    }
  }
  nv_list_clear(&fnt->m_drawcalls);
}

void
ctext_flush_renders(nv_renderer_t* rd)
{
  for (int i = 0; i < (int)nv_list_size(&rd->m_ctext->m_fonts); i++)
  {
    cfont_t* fnt = *(cfont_t**)nv_list_get(&rd->m_ctext->m_fonts, i);
    _ctext_flush_font(rd, fnt);
  }
}

ctext_label_t*
ctext_create_label(nv_scene_t* scene, cfont_t* fnt)
{
  ctext_label_t label = {
    .m_h_align = CTEXT_HORI_ALIGN_LEFT,
    .m_v_align = CTEXT_VERT_ALIGN_TOP,
    .m_index   = (int)nv_list_size(&fnt->m_rd->m_ctext->m_labels),
    .m_text    = nv_string_init(0, nv_allocator_get_default()),
    .m_fnt     = fnt,
    .m_obj     = nv_object_create(scene, "Text Label", 0, 0, 0, nv_zero_init(vec2), (vec2){ 1.0f, 1.0f }, NOVA_OBJECT_NO_COLLISION),
  };
  nv_list_push_back(&fnt->m_rd->m_ctext->m_labels, &label);
  return &(((ctext_label_t*)fnt->m_rd->m_ctext->m_labels.m_data)[nv_list_size(&fnt->m_rd->m_ctext->m_labels) - 1]);
}

void
ctext_destroy_label(ctext_label_t* label)
{
  nv_string_destroy(&label->m_text);
  nv_list_remove(&label->m_fnt->m_rd->m_ctext->m_labels, label->m_index);
}

nv_object*
ctext_label_get_object(const ctext_label_t* label)
{
  return label->m_obj;
}
void
ctext_label_set_text(ctext_label_t* label, const char* text)
{
  nv_string_set(&label->m_text, text);
}
void
ctext_label_set_horizontal_align(ctext_label_t* label, ctext_hori_align h_align)
{
  label->m_h_align = h_align;
}
void
ctext_label_set_vertical_align(ctext_label_t* label, ctext_vert_align v_align)
{
  label->m_v_align = v_align;
}

void
ctext_label_set_text_scale(ctext_label_t* label, flt_t scale)
{
  label->m_scale = scale;
}

void
ctext_init(struct nv_renderer_t* rd)
{
  rd->m_ctext            = nv_calloc(sizeof(nv_ctext_module));
  nv_ctext_module* ctext = rd->m_ctext;

  nv_list_init(sizeof(cfont_t*), 4, nv_allocator_get_default(), &ctext->m_fonts);
  nv_list_init(sizeof(ctext_label_t), 4, nv_allocator_get_default(), &ctext->m_labels);

  nv_assert(nv_list_is_initialized(&ctext->m_fonts) == 0);
  nv_assert(nv_list_is_initialized(&ctext->m_labels) == 0);

  const VkDescriptorSetLayoutBinding bindings[] = {
    // binding; descriptorType; descriptorCount; stageFlags;
    // pImmutableSamplers;
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CTEXT_MAX_FONT_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
  };

  nv_allocate_descriptor_set(&g_pool, bindings, nv_arrlen(bindings), &ctext->m_desc_set);

  const VkDescriptorImageInfo empty_img_info = { .sampler     = nv_sprite_get_sampler(nv_sprite_empty),
                                                 .imageView   = nv_sprite_get_vk_image_view(nv_sprite_empty),
                                                 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

  VkWriteDescriptorSet write_set = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = ctext->m_desc_set->m_set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &empty_img_info,
  };
  for (int i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    write_set.dstArrayElement = i;
    nv_descriptor_set_submit_write(ctext->m_desc_set, &write_set);
  }
}

void
ctext_shutdown(struct nv_renderer_t* rd)
{
  if (!rd || !rd->m_ctext)
  {
    return;
  }
  nv_list_destroy(&rd->m_ctext->m_fonts);
  nv_list_destroy(&rd->m_ctext->m_labels);
  nv_free(rd->m_ctext);
}

flt_t
ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox)
{
  if (!fnt || !str || ctext_validate_font(fnt) != 0)
  {
    return INFINITY;
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
nv_descriptor_set_submit_write(nv_descriptor_set_t* set, const VkWriteDescriptorSet* write)
{
  set->m_writes = nv_realloc(set->m_writes, (set->m_nwrites + 1) * sizeof(VkWriteDescriptorSet));
  nv_assert(set->m_writes != NULL);
  if (!nv_memcpy(&set->m_writes[set->m_nwrites], write, sizeof(VkWriteDescriptorSet)))
  {
    return -1;
  }
  set->m_nwrites++;
  vkUpdateDescriptorSets(nvvk_context.device, 1, write, 0, 0);
  return 0;
}

void
nv_descriptor_set_destroy(nv_descriptor_set_t* set)
{
  vkDestroyDescriptorSetLayout(nvvk_context.device, set->m_layout, NOVA_VK_ALLOCATOR);
  nv_free(set->m_writes);
  nv_free(set);
}

void
nv_descriptor_pool_destroy(nv_descriptor_pool_t* pool)
{
  for (int i = 0; i < pool->m_nsets; i++)
  {
    nv_descriptor_set_destroy(pool->m_sets[i]);
  }
  nv_free(pool->m_sets);
  vkDestroyDescriptorPool(nvvk_context.device, pool->m_pool, NOVA_VK_ALLOCATOR);
}

int
_nv_descriptor_pool_allocate(nv_descriptor_pool_t* pool)
{
  nv_assert(pool != NULL);

  VkDescriptorPoolSize allocations[11]     = { 0 };
  int                  descriptors_written = 0;

  for (int i = 0; i < 11; i++)
  {
    if (pool->m_descriptors[i].m_capacity == 0)
    {
      continue;
    }
    allocations[descriptors_written] = (VkDescriptorPoolSize){ pool->m_descriptors[i].m_type, pool->m_descriptors[i].m_capacity };
    descriptors_written++;
  }

  if (descriptors_written == 0)
  {
    return 0;
  }

  bool need_more_max_sets = (pool->m_nsets + 1) > pool->m_max_child_sets;
  if (need_more_max_sets)
  {
    pool->m_max_child_sets = NV_MAX(pool->m_max_child_sets * 2, 1);
  }

  VkDescriptorPoolCreateInfo poolInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = pool->m_max_child_sets, .poolSizeCount = descriptors_written, .pPoolSizes = allocations
  };

  VkDescriptorPool new_pool;
  nvvk_result_check(vkCreateDescriptorPool(nvvk_context.device, &poolInfo, NOVA_VK_ALLOCATOR, &new_pool));
  if (!new_pool)
  {
    return -1;
  }

  VkDescriptorSet* new_sets = nv_malloc(sizeof(VkDescriptorSet) * NV_MAX(pool->m_nsets, 1));
  nv_assert(new_sets != NULL);

  if (pool->m_nsets > 0)
  {
    VkDescriptorSetLayout* layouts = nv_malloc(sizeof(VkDescriptorSetLayout) * pool->m_nsets);
    for (int i = 0; i < pool->m_nsets; i++)
    {
      layouts[i] = pool->m_sets[i]->m_layout;
      nv_assert(layouts[i] != NULL);
    }

    VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
    setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool              = new_pool;
    setAllocInfo.descriptorSetCount          = pool->m_nsets;
    setAllocInfo.pSetLayouts                 = layouts;
    nvvk_result_check(vkAllocateDescriptorSets(nvvk_context.device, &setAllocInfo, new_sets));
    if (!new_sets)
    {
      return -1;
    }

    nv_free(layouts);
  }

  int ncopies = 0;
  for (int i = 0; i < pool->m_nsets; i++)
  {
    ncopies += pool->m_sets[i]->m_nwrites;
  }
  VkCopyDescriptorSet* copies = nv_malloc(sizeof(VkCopyDescriptorSet) * NV_MAX(ncopies, 1));

  nv_assert(pool->m_sets != NULL);

  ncopies = 0;
  for (int i = 0; i < pool->m_nsets; i++)
  {
    nv_descriptor_set_t* old_set = pool->m_sets[i];

    for (int writei = 0; writei < old_set->m_nwrites; writei++)
    {
      nv_assert(new_sets[i] != NULL);

      VkWriteDescriptorSet* write = &old_set->m_writes[writei];
      copies[ncopies]             = (VkCopyDescriptorSet){
                    .sType           = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,
                    .srcSet          = old_set->m_set,
                    .srcBinding      = write->dstBinding,
                    .srcArrayElement = write->dstArrayElement,
                    .dstSet          = new_sets[i],
                    .dstBinding      = write->dstBinding,
                    .dstArrayElement = write->dstArrayElement,
                    .descriptorCount = write->descriptorCount,
      };
      ncopies++;
    }
    old_set->m_set = new_sets[i];
  }
  vkUpdateDescriptorSets(nvvk_context.device, 0, NULL, ncopies, copies);

  if (pool->m_pool)
  {
    vkDestroyDescriptorPool(nvvk_context.device, pool->m_pool, NOVA_VK_ALLOCATOR);
  }
  pool->m_pool = new_pool;

  nv_free(new_sets);
  nv_free(copies);
  return 0;
}

int
nv_descriptor_pool_init(nv_descriptor_pool_t* dst)
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
  nv_memcpy(dst->m_descriptors, pool_sizes, sizeof(pool_sizes));
  dst->m_sets           = nv_malloc(sizeof(nv_descriptor_set_t*));
  dst->m_max_child_sets = 1;
  dst->m_nsets          = 0;
  if (_nv_descriptor_pool_allocate(dst) != 0)
  {
    return -1;
  }
  return 0;
}

int
nv_allocate_descriptor_set(nv_descriptor_pool_t* pool, const VkDescriptorSetLayoutBinding* bindings, int nbindings, nv_descriptor_set_t** dst)
{
  bool need_realloc = 0;
  for (int i = 0; i < 11; i++)
  {
    for (int j = 0; j < nbindings; j++)
    {
      nv_descriptor_pool_size* descriptor = &pool->m_descriptors[i];
      if (descriptor->m_type == bindings[j].descriptorType)
      {
        descriptor->m_capacity = NV_MAX(descriptor->m_capacity * 2, (int)bindings[j].descriptorCount + descriptor->m_capacity);
        need_realloc           = 1;
      }
    }
  }

  if (need_realloc || ((pool->m_nsets + 1) > pool->m_max_child_sets))
  {
    if ((pool->m_nsets + 1) > pool->m_max_child_sets)
    {
      pool->m_max_child_sets = NV_MAX(pool->m_max_child_sets * 2, 1);
      pool->m_sets           = nv_realloc(pool->m_sets, pool->m_max_child_sets * sizeof(nv_descriptor_set_t));
      nv_assert(pool->m_sets != NULL);
    }

    _nv_descriptor_pool_allocate(pool);
  }

  nv_descriptor_set_t* set = nv_calloc(sizeof(nv_descriptor_set_t));
  nv_assert(set != NULL);

  pool->m_sets[pool->m_nsets] = set;
  pool->m_nsets++;

  (*dst) = set;

  set->m_pool   = pool;
  set->m_writes = nv_malloc(sizeof(VkWriteDescriptorSet));
  nv_assert(set->m_writes != NULL);

  VkDescriptorSetLayoutCreateInfo layoutinfo = nv_zero_init(VkDescriptorSetLayoutCreateInfo);
  layoutinfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutinfo.pBindings                       = bindings;
  layoutinfo.bindingCount                    = nbindings;
  nvvk_result_check(vkCreateDescriptorSetLayout(nvvk_context.device, &layoutinfo, NOVA_VK_ALLOCATOR, &set->m_layout));
  if (set->m_layout == NULL)
  {
    return -1;
  }

  VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
  setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAllocInfo.descriptorPool              = pool->m_pool;
  setAllocInfo.descriptorSetCount          = 1;
  setAllocInfo.pSetLayouts                 = &set->m_layout;
  nvvk_result_check(vkAllocateDescriptorSets(nvvk_context.device, &setAllocInfo, &set->m_set));
  if (set->m_set == NULL)
  {
    return -1;
  }

  return 0;
}
// nv_descriptors ^^

void
__BakeUnlitPipeline(nv_renderer_t* rd)
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
  nvvk_result_check(vkCreateDescriptorSetLayout(nvvk_context.device, &layoutinfo, NOVA_VK_ALLOCATOR, &g_Pipelines.m_unlit.m_descriptor_layout));

  nvsm_shader_t *vertex, *fragment;
  nv_assert(nvsm_load_shader("Unlit/vert", &vertex) == 0);
  nv_assert(nvsm_load_shader("Unlit/frag", &fragment) == 0);

  nv_assert(vertex != NULL && fragment != NULL);

  const nvsm_shader_t*        shaders[] = { vertex, fragment };
  const VkDescriptorSetLayout layouts[] = { camera.m_sets->m_layout, g_Pipelines.m_unlit.m_descriptor_layout };

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);

  const VkVertexInputAttributeDescription attributeDescriptions[] = {
    // location; binding; format; offset;
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },          // pos
    { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(vec3f) }, // texcoord
  };

  const VkVertexInputBindingDescription bindingDescriptions[] = { // binding; stride; inputRate
                                                                  { 0, sizeof(vec3f) + sizeof(vec2f), VK_VERTEX_INPUT_RATE_VERTEX }
  };

  const VkPushConstantRange pushConstants[] = { // stageFlags, offset, size
                                                { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mat4f) + sizeof(vec4f) + sizeof(vec2f) }
  };

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.m_format                    = nvvk_context.swap_chain_image_format;
  pc.m_subpass                   = 0;
  pc.m_render_pass               = nv_renderer_get_render_pass(rd);

  pc.m_n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.m_p_attribute_descriptions = attributeDescriptions;

  pc.m_n_push_constants = nv_arrlen(pushConstants);
  pc.m_p_push_constants = pushConstants;

  pc.m_n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.m_p_binding_descriptions = bindingDescriptions;

  pc.m_n_shaders = nv_arrlen(shaders);
  pc.m_p_shaders = shaders;

  pc.m_n_descriptor_layouts = nv_arrlen(layouts);
  pc.m_p_descriptor_layouts = layouts;

  pc.m_extent.width  = RenderExtent.m_width;
  pc.m_extent.height = RenderExtent.m_height;
  pc.m_samples       = nvvk_context.samples;
  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.m_unlit.m_pipeline_layout);
  pc.m_pipeline_layout = g_Pipelines.m_unlit.m_pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.m_unlit.m_pipeline, 0);
}

void
__BakeCtextPipeline(nv_renderer_t* rd)
{
  const VkVertexInputAttributeDescription attributeDescriptions[] = {
    // location; binding; format; offset;
    { 0, 0, nv_format_to_vk_format(NOVA_FORMAT_RGB32), 0 },            // pos
    { 1, 0, nv_format_to_vk_format(NOVA_FORMAT_RG32), sizeof(vec3f) }, // uv
  };

  const VkVertexInputBindingDescription bindingDescriptions[] = { // binding; stride; inputRate
                                                                  { 0, sizeof(vec3f) + sizeof(vec2f), VK_VERTEX_INPUT_RATE_VERTEX }
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert(nvsm_load_shader("ctext/vert", &vertex) != -1);
  nv_assert(nvsm_load_shader("ctext/frag", &fragment) != -1);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.m_sets->m_layout, rd->m_ctext->m_desc_set->m_layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.m_format                    = nvvk_context.swap_chain_image_format;
  pc.m_subpass                   = 0;
  pc.m_render_pass               = nv_renderer_get_render_pass(rd);

  pc.m_n_attribute_descriptions = nv_arrlen(attributeDescriptions);
  pc.m_p_attribute_descriptions = attributeDescriptions;

  pc.m_n_push_constants = nv_arrlen(pushConstants);
  pc.m_p_push_constants = pushConstants;

  pc.m_n_binding_descriptions = nv_arrlen(bindingDescriptions);
  pc.m_p_binding_descriptions = bindingDescriptions;

  pc.m_n_shaders = nv_arrlen(shaders);
  pc.m_p_shaders = (const struct nvsm_shader_t* const*)shaders;

  pc.m_n_descriptor_layouts = nv_arrlen(layouts);
  pc.m_p_descriptor_layouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.m_extent.width              = RenderExtent.m_width;
  pc.m_extent.height             = RenderExtent.m_height;
  pc.m_blend_state               = &blend;
  pc.m_samples                   = nvvk_context.samples;

  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.m_ctext.m_pipeline_layout);
  pc.m_pipeline_layout = g_Pipelines.m_ctext.m_pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.m_ctext.m_pipeline, 0);
}

void
__BakeDebugLinePipeline(nv_renderer_t* rd)
{
  struct line_push_constants
  {
    mat4f m_model;
    vec4f m_color;
    vec2f m_line_begin;
    vec2f m_line_end;
  };

  const VkPushConstantRange pushConstants[] = {
    // stageFlags, offset, size
    { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants) },
  };

  nvsm_shader_t *vertex, *fragment;
  nv_assert(nvsm_load_shader("Debug/Line/vert", &vertex) != -1);
  nv_assert(nvsm_load_shader("Debug/Line/frag", &fragment) != -1);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.m_sets->m_layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.m_format                    = nvvk_context.swap_chain_image_format;

  pc.m_topology    = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pc.m_render_pass = nv_renderer_get_render_pass(rd);

  pc.m_n_attribute_descriptions = 0;
  pc.m_p_attribute_descriptions = NULL;

  pc.m_n_push_constants = nv_arrlen(pushConstants);
  pc.m_p_push_constants = pushConstants;

  pc.m_n_binding_descriptions = 0;
  pc.m_p_binding_descriptions = NULL;

  pc.m_n_shaders = nv_arrlen(shaders);
  pc.m_p_shaders = (const struct nvsm_shader_t* const*)shaders;

  pc.m_n_descriptor_layouts = nv_arrlen(layouts);
  pc.m_p_descriptor_layouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.m_extent.width              = RenderExtent.m_width;
  pc.m_extent.height             = RenderExtent.m_height;
  pc.m_blend_state               = &blend;
  pc.m_samples                   = nvvk_context.samples;

  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.m_line.m_pipeline_layout);
  pc.m_pipeline_layout = g_Pipelines.m_line.m_pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.m_line.m_pipeline, 0);
}

void
nv_vk_bake_global_pipelines(nv_renderer_t* rd)
{
  __BakeUnlitPipeline(rd);
  __BakeDebugLinePipeline(rd);
  __BakeCtextPipeline(rd);
}

void
nv_vk_destroy_pipeline(nv_vk_pipeline_t* pipeline)
{
  if (!pipeline)
  {
    return;
  }

  vkDestroyPipeline(nvvk_context.device, pipeline->m_pipeline, NOVA_VK_ALLOCATOR);
  vkDestroyPipelineLayout(nvvk_context.device, pipeline->m_pipeline_layout, NOVA_VK_ALLOCATOR);
  vkDestroyDescriptorSetLayout(nvvk_context.device, pipeline->m_descriptor_layout, NOVA_VK_ALLOCATOR);
}

void
nv_vk_destroy_global_pipelines(void)
{
  nv_vk_pipeline_t pipelines[] = {
    g_Pipelines.m_unlit,
    g_Pipelines.m_ctext,
    g_Pipelines.m_line,
  };
  for (int i = 0; i < (int)nv_arrlen(pipelines); i++)
  {
    nv_vk_destroy_pipeline(&pipelines[i]);
  }
}

void
nv_gpu_create_graphics_pipeline(const nv_gpu_pipeline_create_info* pCreateInfo, VkPipeline* dstPipeline, u32 flags)
{
  NVVK_REQUIRED_PTR(nvvk_context.device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstPipeline);
  NVVK_REQUIRED_PTR(pCreateInfo->m_render_pass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_n_shaders, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_extent.height, 0);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING))
  {
    // Vulkan requires nvvk_context.samples to not be 1.
    NVVK_NOT_EQUAL_TO(pCreateInfo->m_samples, VK_SAMPLE_COUNT_1_BIT);
  }

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount   = pCreateInfo->m_n_binding_descriptions,
    .pVertexBindingDescriptions      = pCreateInfo->m_p_binding_descriptions,
    .vertexAttributeDescriptionCount = pCreateInfo->m_n_attribute_descriptions,
    .pVertexAttributeDescriptions    = pCreateInfo->m_p_attribute_descriptions,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .topology               = pCreateInfo->m_topology,
    .primitiveRestartEnable = VK_FALSE,
  };

  VkViewport viewportState = {
    .x        = 0,
    .y        = 0,
    .width    = (flt_t)(pCreateInfo->m_extent.width),
    .height   = (flt_t)(pCreateInfo->m_extent.height),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };

  VkRect2D scissor = {
    .offset = (VkOffset2D){ 0, 0 },
    .extent = pCreateInfo->m_extent,
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
    .rasterizationSamples  = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING) ? pCreateInfo->m_samples : VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable   = VK_FALSE,
    .minSampleShading      = 1.0f,
    .pSampleMask           = VK_NULL_HANDLE,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable      = VK_FALSE,
  };

  VkPipelineColorBlendAttachmentState colorblendAttachmentState = nv_zero_init(VkPipelineColorBlendAttachmentState);

  if (pCreateInfo->m_blend_state != NULL)
  {
    const nv_gpu_pipeline_blend_state* blendState = pCreateInfo->m_blend_state;

    colorblendAttachmentState = (VkPipelineColorBlendAttachmentState){
      .blendEnable         = VK_TRUE,
      .srcColorBlendFactor = blendState->m_src_color_blend_factor,
      .dstColorBlendFactor = blendState->m_dst_color_blend_factor,
      .colorBlendOp        = blendState->m_color_blend_op,
      .srcAlphaBlendFactor = blendState->m_src_alpha_blend_factor,
      .dstAlphaBlendFactor = blendState->m_dst_alpha_blend_factor,
      .alphaBlendOp        = blendState->m_alpha_blend_op,
      .colorWriteMask      = blendState->m_color_write_mask,
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

  VkPipelineShaderStageCreateInfo* shader_infos = (VkPipelineShaderStageCreateInfo*)nv_calloc(pCreateInfo->m_n_shaders * sizeof(VkPipelineShaderStageCreateInfo));
  for (int i = 0; i < pCreateInfo->m_n_shaders; i++)
  {
    shader_infos[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_infos[i].stage  = (VkShaderStageFlagBits)pCreateInfo->m_p_shaders[i]->m_stage;
    shader_infos[i].module = (VkShaderModule)pCreateInfo->m_p_shaders[i]->m_shader_module;
    shader_infos[i].pName  = "main";
  }

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {
    .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount          = pCreateInfo->m_n_shaders,
    .pStages             = shader_infos,
    .pVertexInputState   = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState      = &viewportStateCreateInfo,
    .pRasterizationState = &rasterizerPipelineStateCreateInfo,
    .pMultisampleState   = &multisamplerPipelineStageCreateInfo,
    .pColorBlendState    = &colorblendState,
    .layout              = pCreateInfo->m_pipeline_layout,
    .renderPass          = pCreateInfo->m_render_pass,
    .subpass             = pCreateInfo->m_subpass,
    .basePipelineHandle  = pCreateInfo->m_old_pipeline,
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

  graphicsPipelineCreateInfo.basePipelineHandle = base_pipeline;

  // if(cacheIsNull) cacheCreator.join();
  // nvvk_result_check(vkCreateGraphicsPipelines(nvvk_context.device, pCreateInfo->m_cache, 1, &graphicsPipelineCreateInfo, NOVA_VK_ALLOCATOR, dstPipeline));
  nvvk_result_check(vkCreateGraphicsPipelines(nvvk_context.device, NULL, 1, &graphicsPipelineCreateInfo, NOVA_VK_ALLOCATOR, dstPipeline));

  base_pipeline = *dstPipeline;

  nv_free(shader_infos);
}

void
nv_gpu_create_render_pass(nv_gpu_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags)
{
  NVVK_REQUIRED_PTR(nvvk_context.device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstRenderPass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_format, NOVA_FORMAT_UNDEFINED);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    NVVK_NOT_EQUAL_TO(pCreateInfo->m_depth_buffer_format, NOVA_FORMAT_UNDEFINED);
  }

  VkAttachmentDescription colorAttachmentDescription = {
    .flags          = 0,
    .format         = nv_format_to_vk_format(pCreateInfo->m_format),
    .samples        = HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING) ? pCreateInfo->m_samples : VK_SAMPLE_COUNT_1_BIT,
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
  nv_list_init(sizeof(VkAttachmentDescription), 5, nv_allocator_get_default(), &attachments);
  nv_list_push_back(&attachments, &colorAttachmentDescription);

  VkAttachmentDescription depthAttachment    = nv_zero_init(VkAttachmentDescription);
  VkAttachmentReference   depthAttachmentRef = nv_zero_init(VkAttachmentReference);
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    depthAttachment = (VkAttachmentDescription){
      .flags          = 0,
      .format         = nv_format_to_vk_format(pCreateInfo->m_depth_buffer_format),
      .samples        = pCreateInfo->m_samples,
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
      .format         = nv_format_to_vk_format(pCreateInfo->m_format),
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
  nvvk_result_check(vkCreateRenderPass(nvvk_context.device, &renderPassInfo, NOVA_VK_ALLOCATOR, dstRenderPass));

  nv_list_destroy(&attachments);
}

void
nv_gpu_create_pipeline_layout(nv_gpu_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout)
{
  NVVK_REQUIRED_PTR(nvvk_context.device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstLayout);

  // int totalLayouts = 0;
  // for (int i = 0; i < pCreateInfo->m_n_shaders; i++) {
  // 	totalLayouts += pCreateInfo->m_p_shaders[i]->m_nsetlayouts;
  // }

  // nv_list_t *sets = nv_list_init(sizeof(VkDescriptorSetLayout, nv_allocator_get_default()),
  // totalLayouts);

  // for (int i = 0; i < pCreateInfo->m_n_shaders; i++) {
  // 	const nvsm_shader_t *shader = pCreateInfo->m_p_shaders[i];
  // 	for (int j = 0; j < shader->m_nsetlayouts; j++) {
  // 		nv_list_push_back(sets, &shader->m_setlayouts[j]);
  // 	}
  // }

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = pCreateInfo->m_n_descriptor_layouts,
    .pSetLayouts            = pCreateInfo->m_p_descriptor_layouts,
    .pushConstantRangeCount = pCreateInfo->m_n_push_constants,
    .pPushConstantRanges    = pCreateInfo->m_p_push_constants,
  };
  nvvk_result_check(vkCreatePipelineLayout(nvvk_context.device, &pipelineLayoutCreateInfo, NOVA_VK_ALLOCATOR, dstLayout));
}

const char*
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
nv_gpu_create_swapchain(nv_gpu_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain)
{
  NVVK_REQUIRED_PTR(nvvk_context.device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_extent.height, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->m_image_count, 0);

  /* Used to check for errors or unavailable settings */
  /* These are the variables passed to the create function*/
  VkPresentModeKHR   present_mode   = pCreateInfo->m_present_mode;
  VkSurfaceFormatKHR surface_format = (VkSurfaceFormatKHR){ nv_format_to_vk_format(pCreateInfo->m_format), pCreateInfo->m_color_space };

  unsigned char      buffer[512];
  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buffer, sizeof(buffer));

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  u32 present_mode_count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(nvvk_context.phys_device, nvvk_context.surface, &present_mode_count, NULL);
  VkPresentModeKHR* present_modes = ac.m_alloc(&ac, 1, present_mode_count * sizeof(VkPresentModeKHR));
  nv_assert_and_ret(present_modes != NULL, );
  vkGetPhysicalDeviceSurfacePresentModesKHR(nvvk_context.phys_device, nvvk_context.surface, &present_mode_count, present_modes);

  bool found_present_mode = false;
  for (u32 i = 0; i < present_mode_count; i++)
  {
    if (present_modes[i] == pCreateInfo->m_present_mode)
    {
      found_present_mode = true;
      break;
    }
  }

  const VkPresentModeKHR fallback_present_mode = VK_PRESENT_MODE_FIFO_KHR;

  if (!found_present_mode)
  {
    nv_push_error(
        "Present mode %s unavailable. Using %s.", _nv_gpu_present_mode_to_string(pCreateInfo->m_present_mode), _nv_gpu_present_mode_to_string(fallback_present_mode));
    present_mode = fallback_present_mode;
  }

  u32 surface_format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(nvvk_context.phys_device, nvvk_context.surface, &surface_format_count, NULL);
  VkSurfaceFormatKHR* surface_formats = ac.m_alloc(&ac, 1, sizeof(VkSurfaceFormatKHR) * surface_format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(nvvk_context.phys_device, nvvk_context.surface, &surface_format_count, surface_formats);

  const VkSurfaceFormatKHR* fallback = &surface_formats[0];

  bool found_surface_format = false;
  for (u32 i = 0; i < surface_format_count; i++)
  {
    const VkSurfaceFormatKHR* vk_surface_format = &surface_formats[i];
    const VkFormat            vk_format         = nv_format_to_vk_format(pCreateInfo->m_format);

    if (vk_surface_format->format == vk_format && vk_surface_format->colorSpace == pCreateInfo->m_color_space)
    {
      found_surface_format = true;
      break;
    }
  }

  if (!found_surface_format)
  {
    nv_push_error(
        "Surface format (VkSurfaceFormatKHR)(format=%u,colorspace=%u) is not an available pair."
        "Using (VkSurfaceFormatKHR)(format=%u,colorspace=%u)",
        pCreateInfo->m_format,
        pCreateInfo->m_color_space,
        fallback->format,
        fallback->colorSpace);

    surface_format.format     = fallback->format;
    surface_format.colorSpace = fallback->colorSpace;
  }

  VkSwapchainCreateInfoKHR swapChainCreateInfo = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext                 = NULL,
    .flags                 = 0,
    .surface               = nvvk_context.surface,
    .minImageCount         = pCreateInfo->m_image_count,
    .imageFormat           = surface_format.format,
    .imageColorSpace       = surface_format.colorSpace,
    .imageExtent           = pCreateInfo->m_extent,
    .imageArrayLayers      = 1,
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode           = present_mode,
    .clipped               = VK_TRUE,
    .oldSwapchain          = pCreateInfo->m_old_swapchain,
  };
  nvvk_result_check(vkCreateSwapchainKHR(nvvk_context.device, &swapChainCreateInfo, NOVA_VK_ALLOCATOR, dstSwapchain));
}

nv_gpu_pipeline_blend_state
nv_gpu_init_pipeline_blend_state(nv_gpu_pipeline_blend_preset preset)
{
  nv_gpu_pipeline_blend_state ret = nv_zero_init(nv_gpu_pipeline_blend_state);
  ret.m_color_write_mask          = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  switch (preset)
  {
    case NVVK_BLEND_PRESET_NONE:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ALPHA:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_SRC_ALPHA;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ADDITIVE:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_MULTIPLICATIVE:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_DST_COLOR;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_DST_ALPHA;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_PREMULTIPLIED_ALPHA:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_SUBTRACTIVE:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_color_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ONE;
      ret.m_alpha_blend_op         = VK_BLEND_OP_REVERSE_SUBTRACT;
      break;
    default:
      ret.m_src_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_color_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_color_blend_op         = VK_BLEND_OP_ADD;
      ret.m_src_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_dst_alpha_blend_factor = VK_BLEND_FACTOR_ZERO;
      ret.m_alpha_blend_op         = VK_BLEND_OP_ADD;
      break;
  }
  return ret;
}

void
nv_vk_create_buffer(size_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer* dstBuffer, VkDeviceMemory* retMem, bool externallyAllocated)
{
  if (size == 0)
  {
    nv_push_error("Zero size buffer requested.");
    return;
  }

  VkBuffer       newBuffer;
  VkDeviceMemory newMemory;

  VkBufferCreateInfo bufferCreateInfo = nv_zero_init(VkBufferCreateInfo);
  bufferCreateInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size               = size;
  bufferCreateInfo.usage              = usageFlags;
  bufferCreateInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(vkCreateBuffer(nvvk_context.device, &bufferCreateInfo, NOVA_VK_ALLOCATOR, &newBuffer));

  VkMemoryRequirements bufferMemoryRequirements;
  vkGetBufferMemoryRequirements(nvvk_context.device, newBuffer, &bufferMemoryRequirements);

  if (!externallyAllocated)
  {
    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = bufferMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(bufferMemoryRequirements.memoryTypeBits, propertyFlags);
    nvvk_result_check(vkAllocateMemory(nvvk_context.device, &allocInfo, NOVA_VK_ALLOCATOR, &newMemory));

    nvvk_result_check(vkBindBufferMemory(nvvk_context.device, newBuffer, newMemory, 0));
    *retMem = newMemory;
  }

  *dstBuffer = newBuffer;
}

void
nv_vk_stage_buffer_transfer(VkBuffer dst, void* data, size_t size)
{
  nv_gpu_buffer_t staging_buffer;
  nv_gpu_create_buffer(size, 1, NOVA_GPU_BUFFER_USAGE_TRANSFER_SOURCE, &staging_buffer);

  nv_gpu_memory_t staging_buffer_memory;
  nv_gpu_allocate_memory(size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE, &staging_buffer_memory);

  nv_gpu_bind_buffer_to_memory(&staging_buffer_memory, 0, &staging_buffer);

  nv_gpu_write_to_buffer(&staging_buffer, size, data, 0);

  const VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = size };

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();
  vkCmdCopyBuffer(cmd, staging_buffer.m_buffer, dst, 1, &copy);
  nvvk_result_check(nv_vk_end_command_buffer(cmd, nvvk_context.transfer_queue, 1));

  nv_gpu_destroy_buffer(&staging_buffer);
  nv_gpu_free_memory(&staging_buffer_memory);
}

u32
nv_vk_get_mem_type(const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties)
{
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(nvvk_context.phys_device, &properties);

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

VkCommandBuffer
nv_vk_begin_command_buffer(void)
{
  if (!cmd_pool)
  {
    VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
    cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.queueFamilyIndex        = nvvk_context.graphics_family_index;
    cmdPoolCreateInfo.flags                   = 0;
    nvvk_result_check(vkCreateCommandPool(nvvk_context.device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &cmd_pool));
  }

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = 1;
  cmdAllocInfo.commandPool                 = cmd_pool;
  nvvk_result_check(vkAllocateCommandBuffers(nvvk_context.device, &cmdAllocInfo, &buffer));

  return nv_vk_begin_command_buffer_from(buffer);
}

VkResult
nv_vk_end_command_buffer(VkCommandBuffer cmd, VkQueue queue, bool waitForExecution)
{
  VkResult res = vkEndCommandBuffer(cmd);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  VkSubmitInfo submitInfo       = nv_zero_init(VkSubmitInfo);
  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers    = &cmd;

  VkFence           fence     = VK_NULL_HANDLE;
  VkFenceCreateInfo fenceInfo = nv_zero_init(VkFenceCreateInfo);
  fenceInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags             = 0;

  if (waitForExecution)
  {
    res = vkCreateFence(nvvk_context.device, &fenceInfo, NOVA_VK_ALLOCATOR, &fence);
    if (res != VK_SUCCESS)
    {
      return res;
    }
  }

  static SDL_mutex* vk_queue_mutex = NULL;
  if (!vk_queue_mutex)
  {
    vk_queue_mutex = SDL_CreateMutex();
  }

  SDL_LockMutex(vk_queue_mutex);

  res = vkQueueSubmit(queue, 1, &submitInfo, fence);
  if (res != VK_SUCCESS)
  {
    return res;
  }

  SDL_UnlockMutex(vk_queue_mutex);

  if (waitForExecution)
  {
    if (fence != VK_NULL_HANDLE)
    {
      res = vkWaitForFences(nvvk_context.device, 1, &fence, VK_TRUE, UINT64_MAX);
      if (res != VK_SUCCESS)
      {
        return res;
      }
      vkDestroyFence(nvvk_context.device, fence, NOVA_VK_ALLOCATOR);
    }
    vkDeviceWaitIdle(nvvk_context.device);
    vkFreeCommandBuffers(nvvk_context.device, cmd_pool, 1, &cmd);
    buffer = NULL;
  }

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
    nv_push_error("fopen error: %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fseek(f, 0, SEEK_END) != 0)
  {
    nv_push_error("fseek error: %s", strerror(errno));
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
    nv_push_error("error in rewind?? %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fread(dst, file_size, 1, f) != 1)
  {
    nv_push_error("fread error %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

CLEANUP_AND_RETURN:
  *dstSize = file_size;
  if (f)
    NOVA_CALL_FILE_FN(fclose(f));
}

void
nv_vk_stage_image_transfer(VkImage dst, const void* data, size_t width, size_t height, size_t image_size)
{
  VkBuffer       stagingBuffer       = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(nvvk_context.device, dst, &mem_req);

  const VkBufferCreateInfo stagingBufferInfo = {
    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size        = mem_req.size,
    .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  nvvk_result_check(vkCreateBuffer(nvvk_context.device, &stagingBufferInfo, NOVA_VK_ALLOCATOR, &stagingBuffer));

  VkMemoryRequirements stagingBufferRequirements;
  vkGetBufferMemoryRequirements(nvvk_context.device, stagingBuffer, &stagingBufferRequirements);

  const VkMemoryAllocateInfo stagingBufferAllocInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize  = stagingBufferRequirements.size,
    .memoryTypeIndex = nv_vk_get_mem_type(stagingBufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  nvvk_result_check(vkAllocateMemory(nvvk_context.device, &stagingBufferAllocInfo, NOVA_VK_ALLOCATOR, &stagingBufferMemory));
  nvvk_result_check(vkBindBufferMemory(nvvk_context.device, stagingBuffer, stagingBufferMemory, 0));

  void* stagingBufferMapped;
  nvvk_result_check(vkMapMemory(nvvk_context.device, stagingBufferMemory, 0, stagingBufferRequirements.size, 0, &stagingBufferMapped));
  nv_memcpy(stagingBufferMapped, data, image_size);
  vkUnmapMemory(nvvk_context.device, stagingBufferMemory);

  const VkCommandBuffer cmd = nv_vk_begin_command_buffer();

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

  nv_vk_end_command_buffer(cmd, nvvk_context.transfer_queue, true);

  vkDestroyBuffer(nvvk_context.device, stagingBuffer, NOVA_VK_ALLOCATOR);
  vkFreeMemory(nvvk_context.device, stagingBufferMemory, NOVA_VK_ALLOCATOR);
}

void
nv_vk_create_texture_from_memory(u8* buffer, u32 width, u32 height, nv_format format, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_vk_create_texture_empty(width, height, format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, NULL, dst, dstMem);
  nv_vk_stage_image_transfer(*dst, buffer, width, height, width * height * nv_format_get_bytes_per_pixel(format));
}

// If the format given is oki then it'll just return it
// otherwise it gives you the next best optoin
nv_format
nv_vk_get_supported_format_for_draw(nv_format fmt)
{
  VkFormatProperties formatProperties;
  vkGetPhysicalDeviceFormatProperties(nvvk_context.phys_device, nv_format_to_vk_format(fmt), &formatProperties);

  if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
  {
    // format not supported
    const char* fmt_str;
    nv_format_to_string(fmt, &fmt_str);
    nv_log_warning("Format %i (%s) unsupported. Using NOVA_FORMAT_RGBA8\n", fmt, fmt_str);
    return NOVA_FORMAT_RGBA8;
  }

  return fmt;
}

void
nv_vk_create_texture_empty(
    u32 width, u32 height, nv_format format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, size_t* image_size, VkImage* dst, VkDeviceMemory* dstMem)
{
  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
  {
    format = nv_vk_get_supported_format_for_draw(format);
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
  nvvk_result_check(vkCreateImage(nvvk_context.device, &imageCreateInfo, NOVA_VK_ALLOCATOR, dst));

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(nvvk_context.device, *dst, &imageMemoryRequirements);

  if (image_size)
  {
    *image_size = imageMemoryRequirements.size;
  }

  // allow for preallocated memory.
  if (dstMem != NULL)
  {
    const u32 localDeviceMemoryIndex = nv_vk_get_mem_type(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = imageMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = localDeviceMemoryIndex;

    nvvk_result_check(vkAllocateMemory(nvvk_context.device, &allocInfo, NOVA_VK_ALLOCATOR, dstMem));
    nvvk_result_check(vkBindImageMemory(nvvk_context.device, *dst, *dstMem, 0));
  }
}

u8*
nv_vk_create_texture_from_disk(const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_image_t tex = nv_image_load(path);

  nv_assert(tex.m_data != NULL);

  *width    = tex.m_width;
  *height   = tex.m_height;
  *channels = tex.m_format;

  nv_vk_create_texture_from_memory(tex.m_data, tex.m_width, tex.m_height, *channels, dst, dstMem);
  return tex.m_data;
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
nv_vk_get_supported_format(VkPhysicalDevice phys_device, VkSurfaceKHR surface, nv_format* dst_format, VkColorSpaceKHR* dst_color_space)
{
  NVVK_REQUIRED_PTR(nvvk_context.device);
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);
  NVVK_REQUIRED_PTR(dst_format);
  NVVK_REQUIRED_PTR(dst_color_space);

  u32 formatCount = 0;
  nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, VK_NULL_HANDLE));
  nv_list_t surface_formats;
  nv_list_init(sizeof(VkSurfaceFormatKHR), formatCount, nv_allocator_get_default(), &surface_formats);
  nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, (VkSurfaceFormatKHR*)nv_list_data(&surface_formats)));

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
nv_vk_get_surface_image_count(VkPhysicalDevice phys_device, VkSurfaceKHR surface)
{
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);

  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  nvvk_result_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface, &surfaceCapabilities));

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
nv_gpu_allocate_memory(size_t size, nv_gpu_memory_usage usage, nv_gpu_memory_t* dst)
{
  nv_assert_and_ret(size > 0, );
  nv_assert_and_ret(usage != 0, );
  nv_assert_and_ret(dst != NULL, );

  (*dst)                                 = nv_zero_init(nv_gpu_memory_t);
  dst->m_size                            = size;
  dst->m_usage                           = usage;
  const VkMemoryPropertyFlags properties = (VkMemoryPropertyFlags)usage;

  VkMemoryAllocateInfo alloc_info = nv_zero_init(VkMemoryAllocateInfo);
  alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize       = size;

  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(nvvk_context.phys_device, &mem_properties);

  for (u32 i = 0; i < mem_properties.memoryTypeCount; i++)
  {
    if ((properties & mem_properties.memoryTypes[i].propertyFlags) == properties)
    {
      alloc_info.memoryTypeIndex = i;
      break;
    }
  }

  nvvk_result_check(vkAllocateMemory(nvvk_context.device, &alloc_info, NOVA_VK_ALLOCATOR, &dst->m_memory));
}

void
nv_gpu_create_buffer(size_t size, size_t alignment, VkBufferUsageFlags usage, nv_gpu_buffer_t* dst)
{
  const size_t aligned_sz = ALIGN_UP(size, alignment);

  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = aligned_sz;
  buffer_info.usage              = usage;
  nvvk_result_check(vkCreateBuffer(nvvk_context.device, &buffer_info, NOVA_VK_ALLOCATOR, &dst->m_buffer));

  VkMemoryRequirements memory_requirements;
  vkGetBufferMemoryRequirements(nvvk_context.device, dst->m_buffer, &memory_requirements);

  dst->m_size      = memory_requirements.size;
  dst->m_alignment = memory_requirements.alignment;
  dst->m_usage     = usage;
}

void
nv_gpu_write_to_local_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset)
{
  nv_gpu_buffer_t staging_buffer;
  nv_gpu_create_buffer(size, NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_buffer);

  nv_gpu_memory_t staging_memory;
  nv_gpu_allocate_memory(size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE, &staging_memory);

  nv_gpu_bind_buffer_to_memory(&staging_memory, 0, &staging_buffer);

  void* mapped = NULL;
  nv_gpu_map_memory(&staging_memory, size, 0, &mapped);
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(&staging_memory);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  VkBufferCopy copy = {
    .srcOffset = 0,
    .dstOffset = offset,
    .size      = size,
  };
  vkCmdCopyBuffer(cmd, staging_buffer.m_buffer, buffer->m_buffer, 1, &copy);

  if (nv_vk_end_command_buffer(cmd, nvvk_context.graphics_queue, 1) != VK_SUCCESS)
  {
    nv_push_error("Failed to write data to GPU buffer");
  }
}

void
nv_gpu_write_to_uniform_buffer(nv_gpu_buffer_t* buffer, size_t size, void* data, size_t offset)
{
  void* mapped = NULL;
  nv_gpu_map_memory(buffer->m_memory, size, offset, &mapped);
  if (mapped == NULL)
  {
    nv_push_error("error in mapping");
    return;
  }
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(buffer->m_memory);
}

void
nv_gpu_map_buffer(nv_gpu_buffer_t* buffer)
{
  nv_assert(!buffer->m_is_mapped);
  nv_gpu_map_memory(buffer->m_memory, buffer->m_size, buffer->m_offset, &buffer->m_mapping);
  buffer->m_is_mapped = 1;
}

void
nv_gpu_unmap_buffer(nv_gpu_buffer_t* buffer)
{
  nv_assert(buffer->m_is_mapped);
  nv_gpu_unmap_memory(buffer->m_memory);
  buffer->m_is_mapped = 0;
}

void
nv_gpu_write_to_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset)
{
  if (buffer->m_is_mapped)
  {
    nv_assert(buffer->m_mapping != NULL);
    nv_memcpy((unsigned char*)buffer->m_mapping + offset, data, size);
    return;
  }
  if (!(buffer->m_memory->m_usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE))
  {
    nv_gpu_write_to_local_buffer(buffer, size, data, offset);
    return;
  }
  void* mapped = NULL;
  nv_gpu_map_memory(buffer->m_memory, size, offset, &mapped);
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(buffer->m_memory);
}

void
nv_gpu_copy_buffer(nv_gpu_buffer_t* dst, const nv_gpu_buffer_t* src)
{
  nv_assert_and_ret(dst != NULL, );
  nv_assert_and_ret(src != NULL, );

  if (src->m_size > dst->m_size)
  {
    nv_push_error("Tried copying a buffer of size %zu to a buffer of size %u.", src->m_size, dst->m_size);
    return;
  }

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = src->m_size };
  vkCmdCopyBuffer(cmd, dst->m_buffer, src->m_buffer, 1, &copy);

  if (nv_vk_end_command_buffer(cmd, nvvk_context.transfer_queue, 1) != VK_SUCCESS)
  {
    nv_push_error("Error in copying buffers.");
    return;
  }
}

void
nv_gpu_resize_buffer(nv_gpu_buffer_t* buffer, nv_gpu_memory_t* memory, size_t new_size)
{
  nv_assert_and_ret(buffer != NULL, );
  nv_assert_and_ret(memory != NULL, );
  nv_assert_and_ret(new_size > 0, );

  nv_assert(0);

  // if (buffer->m_size == new_size)
  // {
  //   return;
  // }

  // if (!(buffer->m_memory))
  // {
  //   nv_push_error("Buffer not bound to memory. Bind the buffer to gpu memory using nv_gpu_bind_buffer_to_memory.");
  //   return;
  // }

  // const bool was_mapped = buffer->m_is_mapped;

  // if (buffer->m_is_mapped)
  // {
  //   nv_gpu_unmap_buffer(buffer);
  // }

  // nv_gpu_memory_t new_memory;
  // nv_gpu_allocate_memory(new_size, nv_gpu_memory_usage usage, nv_gpu_memory_t *dst)

  // nv_gpu_buffer_t new_buffer;
  // nv_gpu_create_buffer(new_size, buffer->m_alignment, buffer->m_usage, &new_buffer);
  // nv_gpu_bind_buffer_to_memory(buffer->m_memory, buffer->m_offset, &new_buffer);
  // nv_gpu_copy_buffer(&new_buffer, buffer);

  // nv_gpu_destroy_buffer(buffer);

  // nv_memcpy(buffer, &new_buffer, sizeof(nv_gpu_buffer_t));

  // if (was_mapped && !buffer->m_is_mapped)
  // {
  //   nv_gpu_map_buffer(buffer);
  // }
}

void
nv_gpu_map_memory(nv_gpu_memory_t* memory, size_t size, size_t offset, void** out)
{
  nv_assert(memory != NULL);
  nv_assert(out != NULL);
  nv_assert(size != 0);

  if (!(memory->m_usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE))
  {
    nv_push_error("Memory usage does not have NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE "
                  "Use nv_gpu_write_to_buffer() instead.");
    *out = NULL;
    return;
  }
  if (memory->m_map_size != 0)
  {
    *out = memory->m_mapped;
    return;
  }
  if (vkMapMemory(nvvk_context.device, memory->m_memory, offset, size, 0, &memory->m_mapped) != VK_SUCCESS)
  {
    nv_push_error("Memory could not be mapped for write");
    *out = NULL;
    return;
  }
  memory->m_map_size   = size;
  memory->m_map_offset = offset;
  *out                 = memory->m_mapped;
}

void
nv_gpu_unmap_memory(nv_gpu_memory_t* memory)
{
  // if (!(memory->m_usage & NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE)) {
  //     VkMappedMemoryRange range = {
  //         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
  //         .memory = memory->m_memory,
  //         .offset = memory->m_map_offset,
  //         .size = memory->m_map_size,
  //     };
  //     vkFlushMappedMemoryRanges(nvvk_context.device, 1, &range);
  // }
  memory->m_map_offset = 0;
  memory->m_map_size   = 0;
  vkUnmapMemory(nvvk_context.device, memory->m_memory);
}

void
nv_gpu_free_memory(nv_gpu_memory_t* mem)
{
  if (mem && mem->m_memory)
  {
    vkFreeMemory(nvvk_context.device, mem->m_memory, NULL);
  }
}

// I think these given an error when, say offset is too big so maybe we can
// check their return values?
void
nv_gpu_bind_buffer_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_buffer_t* buffer)
{
  buffer->m_memory = mem;
  buffer->m_offset = offset;

  nvvk_result_check(vkBindBufferMemory(nvvk_context.device, buffer->m_buffer, mem->m_memory, offset));
}

void
nv_gpu_texture_attach_view(nv_gpu_texture* tex, VkImageView view)
{
  tex->m_view = view;
}

void
nv_gpu_bind_texture_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_texture* tex)
{
  tex->m_memory = mem;
  tex->m_offset = offset;
  nvvk_result_check(vkBindImageMemory(nvvk_context.device, tex->m_image, mem->m_memory, offset));

  VkImageAspectFlags aspect = 0;
  if (nv_format_has_depth_channel(tex->m_format))
  {
    aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  }
  else if (nv_format_has_color_channel(tex->m_format))
  {
    aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  if (nv_format_has_stencil_channel(tex->m_format))
  {
    aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
  }

  VkImageSubresourceRange subresourceRange = {
    .aspectMask = aspect, .baseMipLevel = 0, .levelCount = VK_REMAINING_MIP_LEVELS, .baseArrayLayer = 0, .layerCount = VK_REMAINING_ARRAY_LAYERS
  };

  nv_format dst_format = tex->m_format;
  dst_format           = nv_vk_get_supported_format_for_draw(dst_format);

  VkImageViewCreateInfo view_info = {
    .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image            = tex->m_image,
    .viewType         = (VkImageViewType)tex->m_type,
    .format           = nv_format_to_vk_format(dst_format),
    .subresourceRange = subresourceRange,
    .components.r     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.g     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.b     = VK_COMPONENT_SWIZZLE_IDENTITY,
    .components.a     = VK_COMPONENT_SWIZZLE_IDENTITY,
  };
  nvvk_result_check(vkCreateImageView(nvvk_context.device, &view_info, NOVA_VK_ALLOCATOR, &tex->m_view));
}

void
nv_gpu_destroy_buffer(nv_gpu_buffer_t* buffer)
{
  if (!buffer->m_buffer)
  {
    nv_log_info("Attempt to destroy a buffer %u which has a NULL VkBuffer\n", buffer);
    return;
  }
  vkDeviceWaitIdle(nvvk_context.device);
  vkDestroyBuffer(nvvk_context.device, buffer->m_buffer, NOVA_VK_ALLOCATOR);
  nv_memset(buffer, 0, sizeof(nv_gpu_buffer_t));
}

void
nv_gpu_destroy_texture(nv_gpu_texture* tex)
{
  vkDeviceWaitIdle(nvvk_context.device);
  if (!tex->m_view)
  {
    nv_log_info("Attempt to destroy an image view which is NULL\n");
    return;
  }
  if (!tex->m_image)
  {
    nv_log_info("Attempt to destroy an image which is NULL\n");
    return;
  }
  vkDestroyImage(nvvk_context.device, tex->m_image, NOVA_VK_ALLOCATOR);
  vkDestroyImageView(nvvk_context.device, tex->m_view, NOVA_VK_ALLOCATOR);
}

size_t
nv_gpu_get_buffer_size(const nv_gpu_buffer_t* buffer)
{
  return buffer->m_size;
}

void
nv_gpu_buffer_readback(const nv_gpu_buffer_t* buffer, void* dst)
{
  if (!(buffer->m_memory->m_usage & NOVA_GPU_BUFFER_USAGE_TRANSFER_SOURCE))
  {
    nv_push_error("Cannot readback from buffer that is not transfer source");
    return;
  }

  nv_gpu_buffer_t staging;
  nv_gpu_memory_t staging_mem;
  nv_gpu_create_buffer(buffer->m_size, NOVA_GPU_ALIGNMENT_UNNECESSARY, NOVA_GPU_BUFFER_USAGE_TRANSFER_DESTINATION, &staging);
  nv_gpu_allocate_memory(buffer->m_size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &staging_mem);
  nv_gpu_bind_buffer_to_memory(&staging_mem, 0, &staging);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = buffer->m_size };
  vkCmdCopyBuffer(cmd, buffer->m_buffer, staging.m_buffer, 1, &copy);

  nv_vk_end_command_buffer(cmd, nvvk_context.transfer_queue, 1);

  void* mapped = NULL;
  nv_gpu_map_memory(&staging_mem, buffer->m_size, 0, &mapped);
  nv_assert(mapped != NULL);
  nv_memcpy(dst, mapped, buffer->m_size);
  nv_gpu_unmap_memory(&staging_mem);

  nv_gpu_destroy_buffer(&staging);
  nv_gpu_free_memory(&staging_mem);
}

void
nv_gpu_get_texture_size(const nv_gpu_texture* tex, size_t* w, size_t* h)
{
  if (w)
  {
    *w = tex->m_extent.width;
  }
  if (h)
  {
    *h = tex->m_extent.height;
  }
}

void
nv_gpu_create_sampler(nv_renderer_t* rd, const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler* sampler)
{
  nv_assert_and_ret(rd != NULL, );
  nv_assert_and_ret(pInfo != NULL, );
  nv_assert_and_ret(sampler != NULL, );

  for (int i = 0; i < (int)nv_list_size(&rd->m_samplers); i++)
  {
    nv_gpu_sampler* cache = *((nv_gpu_sampler**)nv_list_get(&rd->m_samplers, i));
    if (cache != NULL && cache->m_filter == pInfo->m_filter && cache->m_mipmap_mode == pInfo->m_mipmap_mode && cache->m_address_mode == pInfo->m_address_mode
        && cache->m_max_anisotropy == pInfo->m_max_anisotropy && cache->m_mip_lod_bias == pInfo->m_mip_lod_bias && cache->m_min_lod == pInfo->m_min_lod
        && cache->m_max_lod == pInfo->m_max_lod && cache->m_vksampler != VK_NULL_HANDLE)
    {
      *sampler = *cache;
      return;
    }
  }

  *sampler = (nv_gpu_sampler){
    .m_filter         = pInfo->m_filter,
    .m_mipmap_mode    = pInfo->m_mipmap_mode,
    .m_address_mode   = pInfo->m_address_mode,
    .m_max_anisotropy = pInfo->m_max_anisotropy,
    .m_mip_lod_bias   = pInfo->m_mip_lod_bias,
    .m_min_lod        = pInfo->m_min_lod,
    .m_max_lod        = pInfo->m_max_lod,
  };

  VkSamplerCreateInfo samplerInfo = nv_zero_init(VkSamplerCreateInfo);
  samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter           = pInfo->m_filter;
  samplerInfo.minFilter           = pInfo->m_filter;
  samplerInfo.mipmapMode          = pInfo->m_mipmap_mode;
  samplerInfo.addressModeU        = pInfo->m_address_mode;
  samplerInfo.addressModeV        = pInfo->m_address_mode;
  samplerInfo.addressModeW        = pInfo->m_address_mode;
  samplerInfo.anisotropyEnable    = pInfo->m_max_anisotropy > 1.0f;
  samplerInfo.maxLod              = pInfo->m_max_lod;
  samplerInfo.minLod              = pInfo->m_min_lod;
  nvvk_result_check(vkCreateSampler(nvvk_context.device, &samplerInfo, NOVA_VK_ALLOCATOR, &sampler->m_vksampler));

  nv_list_push_back(&rd->m_samplers, &sampler);
}

void
nv_gpu_write_to_texture(nv_gpu_texture* tex, const nv_image_t* src)
{
  if (!(tex->m_usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
  {
    nv_push_error("Cannot write to an image which does not have usage "
                  "VK_IMAGE_USAGE_TRANSFER_DST_BIT");
  }

  const size_t tex_size = tex->m_extent.width * tex->m_extent.height * nv_format_get_bytes_per_pixel(tex->m_format);
  // if (tex->m_memory->m_usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE) {
  //     void *mapped;
  //     nv_gpu_map_memory(tex->m_memory, tex_size, tex->m_offset, &mapped);
  //     memcpy(mapped, src->m_data, tex_size);
  //     nv_gpu_unmap_memory(tex->m_memory);
  //     return;
  // }

  nv_vk_stage_image_transfer(tex->m_image, src->m_data, src->m_width, src->m_height, tex_size);
}

VkImage
nv_gpu_texture_get(const nv_gpu_texture* tex)
{
  return tex ? tex->m_image : NULL;
}

VkImageView
nv_gpu_texture_get_view(const nv_gpu_texture* tex)
{
  return tex ? tex->m_view : NULL;
}

VkSampler
nv_gpu_sampler_get(const nv_gpu_sampler* sampler)
{
  return sampler ? sampler->m_vksampler : NULL;
}

void
nv_gpu_create_texture(const nv_gpu_texture_create_info* pInfo, nv_gpu_texture* dst)
{
  nv_assert_and_ret(pInfo != NULL, );
  nv_assert_and_ret(pInfo->m_arraylayers != 0, );
  nv_assert_and_ret(pInfo->m_format != 0, );
  nv_assert_and_ret(pInfo->m_miplevels != 0, );
  nv_assert_and_ret(pInfo->m_samples != 0, );
  nv_assert_and_ret(pInfo->m_type != 0, );
  nv_assert_and_ret(dst != NULL, );

  nv_bzero(dst, sizeof(nv_gpu_texture));

  nv_format fmt = pInfo->m_format;
  if (pInfo->m_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
  {
    fmt = nv_vk_get_supported_format_for_draw(fmt);
  }

  dst->m_type   = pInfo->m_type;
  dst->m_format = pInfo->m_format;
  dst->m_extent = (VkExtent3D){ pInfo->m_extent.m_width, pInfo->m_extent.m_height, pInfo->m_extent.m_depth };

  VkImageUsageFlags usage = 0;

  switch (pInfo->m_usage)
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
    default: nv_push_error("Unknown texture usage: %u", pInfo->m_usage); break;
  }
  dst->m_usage = usage;

  if (pInfo->m_usage & NOVA_GPU_TEXTURE_USAGE_PRESENTATION)
  {
    return;
  }

  VkImageCreateInfo imageCreateInfo = nv_zero_init(VkImageCreateInfo);
  imageCreateInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType         = pInfo->m_type;
  imageCreateInfo.extent            = (VkExtent3D){ pInfo->m_extent.m_width, pInfo->m_extent.m_height, pInfo->m_extent.m_depth };
  imageCreateInfo.mipLevels         = pInfo->m_miplevels;
  imageCreateInfo.arrayLayers       = pInfo->m_arraylayers;
  imageCreateInfo.format            = nv_format_to_vk_format(fmt);
  imageCreateInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.usage             = usage;
  imageCreateInfo.samples           = (VkSampleCountFlagBits)pInfo->m_samples;
  imageCreateInfo.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(vkCreateImage(nvvk_context.device, &imageCreateInfo, NOVA_VK_ALLOCATOR, &dst->m_image));
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
nv_vk_format_to_nv_format(uint32_t format)
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
struct nv_sprite
{
  size_t               m_w, m_h;
  size_t               m_rcount;
  nv_format            m_fmt;
  nv_gpu_texture       m_tex;
  nv_gpu_memory_t      m_mem;
  nv_gpu_sampler       m_sampler;
  nv_descriptor_set_t* m_set;
};

nv_sprite* nv_sprite_empty = NULL;

nv_sprite*
nv_sprite_load_from_memory(nv_renderer_t* rd, const unsigned char* data, size_t w, size_t h, nv_format fmt)
{
  nv_sprite* spr = nv_calloc(sizeof(struct nv_sprite));

  spr->m_rcount = 1;

  fmt        = nv_vk_get_supported_format_for_draw(fmt);
  spr->m_fmt = fmt;

  nv_gpu_texture_create_info tex_info = {
    .m_format      = fmt,
    .m_samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .m_type        = VK_IMAGE_TYPE_2D,
    .m_usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .m_extent      = (nv_extent3D){ .m_width = w, .m_height = h, .m_depth = 1 },
    .m_arraylayers = 1,
    .m_miplevels   = 1,
  };
  nv_gpu_create_texture(&tex_info, &spr->m_tex);

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(nvvk_context.device, nv_gpu_texture_get(&spr->m_tex), &mem_req);
  nv_gpu_allocate_memory(mem_req.size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &spr->m_mem);
  nv_gpu_bind_texture_to_memory(&spr->m_mem, 0, &spr->m_tex);

  const nv_image_t img = (const nv_image_t){ .m_width = w, .m_height = h, .m_format = fmt, .m_data = (unsigned char*)data };
  nv_gpu_write_to_texture(&spr->m_tex, &img);

  nv_gpu_sampler_create_info sampler_info = {
    .m_filter         = VK_FILTER_NEAREST,
    .m_mipmap_mode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .m_address_mode   = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .m_max_anisotropy = 1.0f,
    .m_mip_lod_bias   = 0.0f,
    .m_min_lod        = 0.0f,
    .m_max_lod        = VK_LOD_CLAMP_NONE,
  };
  nv_gpu_create_sampler(rd, &sampler_info, &spr->m_sampler);

  VkDescriptorSetLayoutBinding binding = (VkDescriptorSetLayoutBinding){
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  nv_allocate_descriptor_set(&g_pool, &binding, 1, &spr->m_set);

  VkDescriptorImageInfo desc_img = {
    .sampler     = nv_gpu_sampler_get(&spr->m_sampler),
    .imageView   = nv_sprite_get_vk_image_view(spr),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = spr->m_set->m_set,
    .dstBinding      = 0,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &desc_img,
  };
  nv_descriptor_set_submit_write(spr->m_set, &write);

  return spr;
}

nv_sprite*
nv_sprite_load_from_disk(struct nv_renderer_t* rd, const char* path)
{
  nv_image_t tex = nv_image_load(path);
  nv_sprite* spr = nv_sprite_load_from_memory(rd, tex.m_data, tex.m_width, tex.m_height, tex.m_format);
  spr->m_rcount  = 1;
  nv_free(tex.m_data);
  return spr;
}

void
nv_sprite_destroy(nv_sprite* spr)
{
  nv_gpu_destroy_texture(&spr->m_tex);
  nv_gpu_free_memory(&spr->m_mem);
}

void
nv_sprite_lock(nv_sprite* spr)
{
  spr->m_rcount++;
}

void
nv_sprite_release(nv_sprite* spr)
{
  spr->m_rcount--;
  if (spr->m_rcount <= 0)
  {
    nv_sprite_destroy(spr);
  }
}

void
nv_sprite_get_dimensions(const nv_sprite* spr, size_t* w, size_t* h)
{
  if (w)
  {
    *w = spr->m_w;
  }
  if (h)
  {
    *h = spr->m_h;
  }
}

VkImage
nv_sprite_get_vk_image(const nv_sprite* spr)
{
  return nv_gpu_texture_get(&spr->m_tex);
}

VkImageView
nv_sprite_get_vk_image_view(const nv_sprite* spr)
{
  return nv_gpu_texture_get_view(&spr->m_tex);
}

VkDescriptorSet
nv_sprite_get_descriptor_set(const nv_sprite* spr)
{
  return spr->m_set->m_set;
}

VkSampler
nv_sprite_get_sampler(const nv_sprite* spr)
{
  return nv_gpu_sampler_get(&spr->m_sampler);
}

nv_format
nv_sprite_get_format(const nv_sprite* spr)
{
  return spr->m_fmt;
}
// SPRITE

void
nv_camera_destroy(nv_camera_t* cam)
{
  // nv_descriptor_set_t_destroy(cam->m_sets);
  nv_gpu_destroy_buffer(&cam->m_ub);
  nv_gpu_free_memory(&cam->m_mem);
}

void
nv_camera_init(nv_camera_t* cam)
{
  const flt_t ortho_w = 10.0F, ortho_h = 10.0F;
  *cam = (nv_camera_t){
    .m_perspective = nv_zero_init(mat4),
    .m_ortho_size  = (vec2){ ortho_w, ortho_h },
    .m_ortho       = m4ortho(-ortho_w, ortho_w, -ortho_h, ortho_h, 0.1f, 100.0f),
    .m_position    = (vec3){ 0.0f, 0.0f, 10.0f },
    .m_actual_pos  = (vec3){ 0.0f, 0.0f, 10.0f },
    .m_front       = (vec3){ 0.0f, 0.0f, 1.0f },
    .m_up          = (vec3){ 0.0f, 1.0f, 0.0f },
    .m_right       = (vec3){ 1.0f, 0.0f, 0.0f },

    // These angles should not be in radians because they're converted at update() time.
    .m_yaw   = -90.0F,
    .m_pitch = 0.0F,
    .m_fov   = 90.0f,

    .m_near_clip = 0.1F,
    .m_far_clip  = 1000.0F,
  };

  VkPhysicalDeviceProperties phys_device_properties;
  vkGetPhysicalDeviceProperties(nvvk_context.phys_device, &phys_device_properties);

  VkDeviceSize ub_align = phys_device_properties.limits.minUniformBufferOffsetAlignment;

  // the size of a single uniform buffer.
  int ub_size = ALIGN_UP(sizeof(nv_camera_uniform_buffer), ub_align);

  nv_gpu_create_buffer(ub_size * CAMERA_FAKE_BUFFER_COUNT, ub_align, NOVA_GPU_BUFFER_USAGE_UNIFORM_BUFFER, &cam->m_ub);
  nv_gpu_allocate_memory(ub_size * CAMERA_FAKE_BUFFER_COUNT, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE, &cam->m_mem);
  nv_gpu_bind_buffer_to_memory(&cam->m_mem, 0, &cam->m_ub);

  VkDescriptorSetLayoutBinding bindings[] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL } };
  nv_allocate_descriptor_set(&g_pool, bindings, 1, &cam->m_sets);

  VkDescriptorBufferInfo bufferinfo = { .buffer = cam->m_ub.m_buffer, .offset = 0, .range = (VkDeviceSize)ub_size };
  VkWriteDescriptorSet   write      = {
           .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
           .dstSet          = cam->m_sets->m_set,
           .dstBinding      = 0,
           .descriptorCount = 1,
           .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
           .pBufferInfo     = &bufferinfo,
  };
  nv_descriptor_set_submit_write(cam->m_sets, &write);
  nv_gpu_map_memory(&cam->m_mem, VK_WHOLE_SIZE, 0, (void**)&cam->m_mem_mapped);
}

mat4
nv_camera_get_projection(nv_camera_t* cam)
{
  return cam->m_perspective;
}

mat4
nv_camera_get_view(nv_camera_t* cam)
{
  return cam->m_view;
}

vec3
nv_camera_get_up_vector(nv_camera_t* cam)
{
  return cam->m_up;
}

vec3
nv_camera_get_front_vector(nv_camera_t* cam)
{
  return cam->m_front;
}

void
nv_camera_rotate(nv_camera_t* cam, flt_t yaw_, flt_t pitch_)
{
  cam->m_yaw += yaw_;
  cam->m_pitch -= pitch_;

  cam->m_yaw = fmodf(cam->m_yaw, 360.0f);

  const flt_t bound = 89.9f;
  cam->m_pitch      = NVM_CLAMP(cam->m_pitch, -bound, bound);
}

void
nv_camera_move(nv_camera_t* cam, const vec3 amt)
{
  cam->m_actual_pos = v3add(cam->m_actual_pos, v3muls(cam->m_right, amt.x));
  cam->m_actual_pos = v3add(cam->m_actual_pos, v3muls(cam->m_up, amt.y));
  cam->m_actual_pos = v3add(cam->m_actual_pos, v3muls(cam->m_front, amt.z));
}

void
nv_camera_set_position(nv_camera_t* cam, const vec3 pos)
{
  cam->m_actual_pos = pos;
}

void
nv_camera_update(nv_camera_t* cam, struct nv_renderer_t* rd)
{
  const flt_t yaw_rads = NVM_DEG2RAD(cam->m_yaw), pitch_rads = NVM_DEG2RAD(cam->m_pitch);
  const flt_t cospitch = cosf(pitch_rads);
  vec3        new_front;
  new_front.x = cosf(yaw_rads) * cospitch;
  new_front.y = sinf(pitch_rads);
  new_front.z = sinf(yaw_rads) * cospitch;

  const vec3 world_up = (vec3){ 0.0f, 1.0f, 0.0f };

  cam->m_front = v3normalize(new_front);
  cam->m_right = v3normalize(v3cross(cam->m_front, world_up));
  cam->m_up    = v3normalize(v3cross(cam->m_right, cam->m_front));

  cam->m_view = m4lookat(cam->m_actual_pos, v3add(cam->m_actual_pos, cam->m_front), cam->m_up);

  cam->m_position = cam->m_actual_pos;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  const flt_t       aspect       = (flt_t)RenderExtent.m_width / (flt_t)RenderExtent.m_height;
  cam->m_perspective             = m4perspective(cam->m_fov, aspect, cam->m_near_clip, cam->m_far_clip);

  nv_camera_uniform_buffer ub = nv_zero_init(nv_camera_uniform_buffer);
  NV_MATRIX_COPY(ub.m_perspective, cam->m_perspective);
  NV_MATRIX_COPY(ub.m_ortho, cam->m_ortho);
  NV_MATRIX_COPY(ub.m_view, cam->m_view);
  cam->m_mem_mapped[nv_renderer_get_frame(rd)] = ub;
}

vec2
nv_camera_get_global_mouse_position(const nv_camera_t* cam)
{
  vec2 ortho_pos = v2mulv(nv_input_get_mouse_position(), cam->m_ortho_size);
  return v2add(ortho_pos, (vec2){ cam->m_position.x, cam->m_position.y });
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
nv_gpu_create_framebuffer(const nv_gpu_framebuffer_create_info_t* pCreateInfo, nv_gpu_framebuffer_t* dst)
{
  nv_assert_and_ret(pCreateInfo != NULL, -1);
  nv_assert_and_ret(pCreateInfo->m_attachments != NULL, -1);
  nv_assert_and_ret(pCreateInfo->m_num_attachments != 0, -1);
  nv_assert_and_ret(pCreateInfo->m_extent.m_width != 0, -1);
  nv_assert_and_ret(pCreateInfo->m_extent.m_height != 0, -1);
  nv_assert_and_ret(pCreateInfo->m_num_layers != 0, -1);
  nv_assert_and_ret(pCreateInfo->m_pass != VK_NULL_HANDLE, -1);
  nv_assert_and_ret(dst != NULL, -1);

  nv_bzero(dst, sizeof(nv_gpu_framebuffer_t));

  dst->m_attachments     = pCreateInfo->m_attachments;
  dst->m_num_attachments = pCreateInfo->m_num_attachments;
  dst->m_pass            = pCreateInfo->m_pass;
  dst->m_extent          = pCreateInfo->m_extent;
  dst->m_num_layers      = pCreateInfo->m_num_layers;

  nv_list_t attachments;
  nv_list_init(sizeof(VkImageView), 6, nv_allocator_get_default(), &attachments);

  for (size_t i = 0; i < pCreateInfo->m_num_attachments; i++)
  {
    VkImageView view = nv_gpu_texture_get_view(pCreateInfo->m_attachments[i]);
    nv_list_push_back(&attachments, &view);
  }

  VkFramebufferCreateInfo framebufferInfo = nv_zero_init(VkFramebufferCreateInfo);
  framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass              = pCreateInfo->m_pass;
  framebufferInfo.attachmentCount         = nv_list_size(&attachments);
  framebufferInfo.pAttachments            = (VkImageView*)nv_list_data(&attachments);
  framebufferInfo.width                   = pCreateInfo->m_extent.m_width;
  framebufferInfo.height                  = pCreateInfo->m_extent.m_height;
  framebufferInfo.layers                  = 1;
  nvvk_result_check(vkCreateFramebuffer(nvvk_context.device, &framebufferInfo, NOVA_VK_ALLOCATOR, &dst->m_handle));

  nv_list_destroy(&attachments);

  return 0;
}

void
nv_gpu_destroy_framebuffer(nv_gpu_framebuffer_t* dst)
{
  if (!dst)
  {
    return;
  }

  vkDestroyFramebuffer(nvvk_context.device, dst->m_handle, NOVA_VK_ALLOCATOR);
}
