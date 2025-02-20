#include "../include/GPU/vk.h"

#include "../common/containers/dynarray.h"
#include "../common/image.h"
#include "../include/GPU/buffer.h"
#include "../include/GPU/memory.h"
#include "../include/GPU/pipeline.h"
#include "../include/engine/camera.h"
#include "../include/engine/ctext.h"
#include "../include/engine/engine.h"
#include "../include/engine/fontc.h"
#include "../include/engine/shadermanager.h"
#include "../include/engine/shadermanagerdev.h"
#include "../include/engine/sprite_renderer.h"
#include "../include/engine/ui.h"
#include "../std/stdafx.h"
#include "../std/string.h"

#include <SDL2/SDL_vulkan.h>
#include <math.h>
#include <pthread.h>

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

nv_dynarray_t g_Samplers;
// nv_pipelines.h

// renderer.h
VkInstance               instance       = NULL;
VkDevice                 device         = NULL;
VkPhysicalDevice         phys_device    = NULL;
VkSurfaceKHR             surface        = NULL;
SDL_Window*              window         = NULL;
VkDebugUtilsMessengerEXT debugMessenger = NULL;

nv_format swap_chain_image_format;
u32       swap_chain_color_space;
u32       swap_chain_image_count            = 0;
u32       graphics_family_index             = 0;
u32       present_family_index              = 0;
u32       compute_family_index              = 0;
u32       transfer_queue_index              = 0;
u32       graphics_and_compute_family_index = 0;
VkQueue   graphics_queue                    = VK_NULL_HANDLE;
VkQueue   graphics_and_compute_queue        = VK_NULL_HANDLE;
VkQueue   present_queue                     = VK_NULL_HANDLE;
VkQueue   compute_queue                     = VK_NULL_HANDLE;
VkQueue   transfer_queue                    = VK_NULL_HANDLE;
u32       samples                           = VK_SAMPLE_COUNT_1_BIT;

nv_descriptor_pool_t g_pool;
nv_camera_t          camera;

typedef struct nv_ctext_module     nv_ctext_module;
typedef struct nv_quad_draw_call_t nv_quad_draw_call_t;
typedef struct nv_line_draw_call_t nv_line_draw_call_t;
typedef struct nv_draw_call_t      nv_draw_call_t;

struct nv_ctext_module
{
  nv_dynarray_t        fonts;
  nv_dynarray_t        labels;
  nv_descriptor_set_t* desc_set;
  unsigned             flags;
};

struct nv_quad_draw_call_t
{
  nv_sprite* spr;
  vec3f      siz, pos;
  vec2f      tex_multiplier;
  vec4f      col;
};

struct nv_line_draw_call_t
{
  vec2f begin, end;
  vec4f col;
};

typedef enum nv_draw_call_type
{
  NOVA_DRAWCALL_QUAD = 0,
  NOVA_DRAWCALL_LINE = 1,
  NOVA_DRAWCALL_INVALID = 0x7fffffff
} nv_draw_call_type;

struct nv_draw_call_t
{
  nv_draw_call_type type;
  int             layer;
  union nv_DrawCallData
  {
    nv_line_draw_call_t line;
    nv_quad_draw_call_t quad;
  } drawcall;
};

struct nv_renderer_t
{
  unsigned       flags;
  nv_buffer_mode buffer_mode;

  VkRenderPass render_pass;
  nv_extent2d  render_extent;

  VkSwapchainKHR swapchain;
  VkCommandPool  commandPool;

  u32 attachment_count;
  u32 frame;
  u32 image_index;

  int shadow_image_size; // the size of ONE depth texture. Multiply by
                         // SwapchainImageCount to get total size
  nv_gpu_memory_t* depth_image_memory;

  nv_gpu_texture*  color_image;
  nv_gpu_memory_t* color_image_memory;

  VkFormat depth_buffer_format;

  nv_dynarray_t render_data;
  nv_dynarray_t draw_cmd_buffers;

  nv_dynarray_t drawcalls;

  nv_ctext_module* ctext;

  // These are used to render all the sprites in the game (quad based sprites
  // that is)
  nv_gpu_buffer_t  quad_vb;
  nv_gpu_memory_t* quad_memory;

  vec4 clear_color;

  void* mapped;
};

extern nv_dynarray_t g_Samplers;

struct nv_gpu_sampler
{
  VkFilter             filter;
  VkSamplerMipmapMode  mipmap_mode;
  VkSamplerAddressMode address_mode;
  flt_t                max_anisotropy;
  flt_t                mip_lod_bias, min_lod, max_lod;
  VkSampler            vksampler;
};

struct nv_gpu_texture
{
  nv_gpu_memory_t* memory;
  size_t           size, offset;

  VkImageLayout      layout;
  VkImageAspectFlags aspect;
  VkImageType        type;
  VkImageUsageFlags  usage;

  VkImage         image;
  VkImageView     view;
  VkExtent3D      extent;
  int             miplevels, arraylayers;
  nv_format       format;
  nv_sample_count samples;
};

// renderer.h

// nvgfx vv

nv_extent2d
nv_get_window_size()
{
  int ww, wh;
  SDL_GetWindowSize(window, &ww, &wh);
  return (nv_extent2d){ ww, wh };
}

typedef struct quad_vertex          quad_vertex;
typedef struct nv_line_vertex_t     nv_line_vertex_t;
typedef struct ctext_glyph_vertex_t ctext_glyph_vertex_t;

struct quad_vertex
{
  vec3f position;
  vec2f tex_coords;
};

struct nv_line_vertex_t
{
  vec2 position;
};

static quad_vertex quad_vertices[4];

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
  nv_string_t      text;
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

typedef enum ctext_err_t
{
  CTEXT_ERR_SUCCESS,
  CTEXT_ERR_Invalid_glyph, // May also mean that there just isn't a glyph
  CTEXT_ERR_WRONG_CACHE,   // This cache is not the one you're looking for
  CTEXT_ERR_FILE_ERROR,
} ctext_err_t;
// ctext

int
nv_renderer_get_frame(const nv_renderer_t* rd)
{
  return rd->frame;
}

VkCommandBuffer
nv_renderer_get_draw_buffer(const nv_renderer_t* rd)
{
  return *(VkCommandBuffer*)nv_dynarray_get(&rd->draw_cmd_buffers, rd->frame);
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

int
nv_renderer_get_max_frames_in_flight(const nv_renderer_t* rd)
{
  return 1 + (int)rd->buffer_mode;
}

#define ABSF(x) ((x >= 0.0f) ? (x) : -(x))

bool
nv_Quad_Visible(const vec3* pos, const vec3* siz)
{
  const flt_t half_width  = siz->x * 0.5f;
  const flt_t half_height = siz->y * 0.5f;
  const flt_t deltax      = pos->x - camera.position.x;
  const flt_t deltay      = pos->y - camera.position.y;
  const flt_t dx          = ABSF(deltax); // delta x & y
  const flt_t dy          = ABSF(deltay); //

  return (dx <= (half_width + camera.ortho_size.x) && dy <= (half_height + camera.ortho_size.y));
}

void
nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer)
{
  nv_draw_call_t drawcall = { .type     = NOVA_DRAWCALL_QUAD,
                              .layer    = layer,
                              .drawcall = { .quad = { .spr = spr, .tex_multiplier = tex_coord_multiplier, .pos = position, .siz = size, .col = color } } };
  nv_dynarray_push_back(&rd->drawcalls, &drawcall);
}

void
nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer)
{
  nv_draw_call_t drawcall = { .type = NOVA_DRAWCALL_LINE, .layer = layer, .drawcall = { .line = { .begin = start, .end = end, .col = color } } };
  nv_dynarray_push_back(&rd->drawcalls, &drawcall);
}

// thjs will just sort the array from small layer to big layer :>
int
__drawcall_compar(const void* obj1, const void* obj2)
{
  const nv_draw_call_t* call1 = obj1;
  const nv_draw_call_t* call2 = obj2;
  return (call1->layer - call2->layer) + (call1->type - call2->type);
}

void
_nv_renderer_flush_renders(nv_renderer_t* rd)
{
  const uint32_t        camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);
  const VkCommandBuffer cmd              = nv_renderer_get_draw_buffer(rd);
  const VkDescriptorSet camera_set       = camera.sets->set;

  bool bound_quad_state = 0;

  nv_dynarray_sort(&rd->drawcalls, __drawcall_compar);

  nv_draw_call_type state = NOVA_DRAWCALL_INVALID;

  for (int i = 0; i < (int)nv_dynarray_size(&rd->drawcalls); i++)
  {
    const nv_draw_call_t* drawcall = &((nv_draw_call_t*)nv_dynarray_data(&rd->drawcalls))[i];

    if (drawcall->type == NOVA_DRAWCALL_QUAD)
    {
      // if (!nv_Quad_Visible(&drawcall->drawcall.quad.pos,
      // &drawcall->drawcall.quad.siz)) {
      //     continue;
      // }

      if (state != NOVA_DRAWCALL_QUAD)
      {
        VkDeviceSize offsets = 0;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Unlit.pipeline);

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
      vkCmdPushConstants(cmd, g_Pipelines.Unlit.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

      VkDescriptorSet       sprite_set = nv_sprite_get_descriptor_set(drawcall->drawcall.quad.spr);
      const VkDescriptorSet sets[]     = { camera_set, sprite_set };

      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Unlit.pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
      vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
    }
    else if (drawcall->type == NOVA_DRAWCALL_LINE)
    {
      if (state != NOVA_DRAWCALL_LINE)
      {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Line.pipeline);

        state = NOVA_DRAWCALL_LINE;
      }

      const VkDescriptorSet sets[] = { camera_set };
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_Pipelines.Line.pipeline_layout, 0, 1, sets, 1, &camera_ub_offset);

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
      vkCmdPushConstants(cmd, g_Pipelines.Line.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct line_push_constants), &pc);

      vkCmdDraw(cmd, 2, 1, 0, 0);
    }
  }

  nv_dynarray_clear(&rd->drawcalls);
}

void
nv_renderer_prepare_quad_renderer(nv_renderer_t* rd)
{
  nv_memcpy(
      quad_vertices,
      (const quad_vertex[4]){ (const quad_vertex){ .position = (vec3f){ +0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 0.0f } },
                              (const quad_vertex){ .position = (vec3f){ -0.5f, +0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 0.0f } },
                              (const quad_vertex){ .position = (vec3f){ -0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 0.0f, 1.0f } },
                              (const quad_vertex){ .position = (vec3f){ +0.5f, -0.5f, 0.0f }, .tex_coords = (vec2f){ 1.0f, 1.0f } } },
      sizeof(quad_vertices));

  nv_gpu_create_buffer(
      sizeof(quad_vertices) + sizeof(quad_indices), NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &rd->quad_vb);
  nv_gpu_allocate_memory(sizeof(quad_vertices) + sizeof(quad_indices), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &rd->quad_memory);
  nv_gpu_bind_buffer_to_memory(rd->quad_memory, 0, &rd->quad_vb);

  void* data = nv_malloc(sizeof(quad_vertices) + sizeof(quad_indices));
  nv_memcpy(data, quad_vertices, sizeof(quad_vertices));
  nv_memcpy((char*)data + sizeof(quad_vertices), quad_indices, sizeof(quad_indices));
  nv_gpu_write_to_buffer(&rd->quad_vb, sizeof(quad_vertices) + sizeof(quad_indices), data, 0);

  nv_free(data);
}

void
nv_renderer_destroy(nv_renderer_t* rd)
{
  if (!rd) { return; }

  vkDeviceWaitIdle(device);

  // we were only making frames_in_flight fences and it was working for some
  // reason!! that was the reason we were getting errors!
  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    nv_gpu_destroy_texture(data->depth_image);
    vkDestroyImageView(
        device,
        nv_gpu_texture_get_view(data->sc_image),
        NOVA_VK_ALLOCATOR); // as the view was silently smushed into the
                            // structure, we just kinda smush it out as well.
    vkDestroyFramebuffer(device, data->color_framebuffer, NOVA_VK_ALLOCATOR);

    vkDestroySemaphore(device, data->image_available_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroySemaphore(device, data->render_finish_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroyFence(device, data->in_flight_fence, NOVA_VK_ALLOCATOR);

    nv_free(data->sc_image);
  }
  for (int i = 0; i < (int)nv_dynarray_size(&g_Samplers); i++)
  {
    nv_gpu_sampler* samp = nv_dynarray_get(&g_Samplers, i);
    vkDestroySampler(device, samp->vksampler, NOVA_VK_ALLOCATOR);
  }

  nv_sprite_destroy(nv_sprite_empty);

  nv_camera_destroy(&camera);

  nv_vk_destroy_global_pipelines();
  nv_descriptor_pool_destroy(&g_pool);

  ctext_shutdown(rd);

  nv_dynarray_destroy(&g_Samplers);
  nv_dynarray_destroy(&rd->drawcalls);
  nv_dynarray_destroy(&rd->render_data);

  nv_gpu_free_memory(rd->depth_image_memory);

  nv_gpu_destroy_buffer(&rd->quad_vb);
  nv_gpu_free_memory(rd->quad_memory);

  vkFreeCommandBuffers(device, cmd_pool, 1, &buffer);
  vkDestroyCommandPool(device, cmd_pool, NOVA_VK_ALLOCATOR);

  vkFreeCommandBuffers(device, rd->commandPool, nv_dynarray_size(&rd->draw_cmd_buffers), (VkCommandBuffer*)nv_dynarray_data(&rd->draw_cmd_buffers));
  vkDestroyCommandPool(device, rd->commandPool, NOVA_VK_ALLOCATOR);
  nv_dynarray_destroy(&rd->draw_cmd_buffers);

  nvsm_shutdown();

  vkDestroySwapchainKHR(device, rd->swapchain, NOVA_VK_ALLOCATOR);

  if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    nv_gpu_destroy_texture(rd->color_image);
    nv_gpu_free_memory(rd->color_image_memory);
  }
  vkDestroyRenderPass(device, rd->render_pass, NOVA_VK_ALLOCATOR);
  nv_free(rd);

  vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, NOVA_VK_ALLOCATOR);
  vkDestroySurfaceKHR(instance, surface, NULL);
  SDL_DestroyWindow(window);
  vkDestroyDevice(device, NOVA_VK_ALLOCATOR);
  vkDestroyInstance(instance, NOVA_VK_ALLOCATOR);
}

void
create_optional_images(nv_renderer_t* rd)
{
  nvvk_result_check(vkGetSwapchainImagesKHR(device, rd->swapchain, &swap_chain_image_count, NULL));
  VkImage* swapchainImages = (VkImage*)nv_malloc(swap_chain_image_count * sizeof(VkImage));
  nvvk_result_check(vkGetSwapchainImagesKHR(device, rd->swapchain, &swap_chain_image_count, swapchainImages));

  nv_dynarray_resize(&rd->render_data, swap_chain_image_count);

  if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
  {
    int color_image_size = 0;
    nv_vk_create_texture_empty(
        rd->render_extent.width,
        rd->render_extent.height,
        swap_chain_image_format,
        samples,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        &color_image_size,
        &rd->color_image->image,
        NULL);
    nv_gpu_allocate_memory(color_image_size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &rd->color_image_memory);
    nv_gpu_bind_texture_to_memory(rd->color_image_memory, 0, rd->color_image);
  }

  // attachment vector will be like <color resolve, depth attachment, swapchain
  // image>
  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    (data->sc_image)                    = nv_calloc(sizeof(nv_gpu_texture));
    data->sc_image->image               = swapchainImages[i];

    const nv_gpu_texture_create_info image_info = {
      .format      = NOVA_FORMAT_D32,
      .samples     = samples,
      .type        = VK_IMAGE_TYPE_2D,
      .usage       = NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE,
      .extent      = (nv_extent3D){ .width = rd->render_extent.width, .height = rd->render_extent.height, .depth = 1 },
      .arraylayers = 1,
      .miplevels   = 1,
    };
    nv_gpu_create_texture(&image_info, &data->depth_image);

    if (i == 0)
    {
      VkMemoryRequirements memReqs = nv_zero_init(VkMemoryRequirements);
      vkGetImageMemoryRequirements(device, nv_gpu_texture_get(data->depth_image), &memReqs);

      rd->shadow_image_size = memReqs.size;
      nv_gpu_allocate_memory(rd->shadow_image_size * swap_chain_image_count, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &rd->depth_image_memory);
    }

    nv_gpu_bind_texture_to_memory(rd->depth_image_memory, i * rd->shadow_image_size, data->depth_image);
  }

  nv_free(swapchainImages);
}

void
create_framebuffers_and_swapchain_image_views(nv_renderer_t* rd)
{
  nv_dynarray_t attachments;
  nv_dynarray_init(sizeof(VkImageView), 3, &nv_allocator_default, &attachments);

  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);

    // we don't know anything about the swapchain_image, as it's a swapchain
    // image so we have to manually create the image view;
    VkImageViewCreateInfo imageViewCreateInfo           = nv_zero_init(VkImageViewCreateInfo);
    imageViewCreateInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image                           = nv_gpu_texture_get(data->sc_image);
    imageViewCreateInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format                          = nv_format_to_vk_format(swap_chain_image_format);
    imageViewCreateInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel   = 0;
    imageViewCreateInfo.subresourceRange.levelCount     = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount     = 1;
    VkImageView vioew;
    nvvk_result_check(vkCreateImageView(device, &imageViewCreateInfo, NOVA_VK_ALLOCATOR, &vioew));

    nv_gpu_texture_attach_view(data->sc_image, vioew);

    const VkImageView swapchain_image_view = nv_gpu_texture_get_view(data->sc_image);
    const VkImageView depth_image_view     = nv_gpu_texture_get_view(data->depth_image);

    nv_dynarray_clear(&attachments);
    if (rd->flags & NOVA_RENDERER_MULTISAMPLING_ENABLE)
    {
      nv_dynarray_push_set(&attachments, (VkImageView[]){ nv_gpu_texture_get_view(rd->color_image), depth_image_view, swapchain_image_view }, 3);
    }
    else { nv_dynarray_push_set(&attachments, (VkImageView[]){ swapchain_image_view, depth_image_view }, 2); }

    VkFramebufferCreateInfo framebufferInfo = nv_zero_init(VkFramebufferCreateInfo);
    framebufferInfo.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass              = rd->render_pass;
    framebufferInfo.attachmentCount         = nv_dynarray_size(&attachments);
    framebufferInfo.pAttachments            = (VkImageView*)nv_dynarray_data(&attachments);
    framebufferInfo.width                   = rd->render_extent.width;
    framebufferInfo.height                  = rd->render_extent.height;
    framebufferInfo.layers                  = 1;
    nvvk_result_check(vkCreateFramebuffer(device, &framebufferInfo, NOVA_VK_ALLOCATOR, &data->color_framebuffer));
  }

  nv_dynarray_destroy(&attachments);
}

void
nv_renderer_initialize_graphics_singleton()
{
  if (!nv_vk_get_supported_format(phys_device, surface, &swap_chain_image_format, &swap_chain_color_space)) { nv_log_and_abort("No supported format for display."); }
  swap_chain_image_count = nv_vk_get_surface_image_count(phys_device, surface);

  u32 queueCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queueCount, NULL);
  nv_dynarray_t queueFamilies;
  nv_dynarray_init(sizeof(VkQueueFamilyProperties), queueCount, &nv_allocator_default, &queueFamilies);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queueCount, (VkQueueFamilyProperties*)nv_dynarray_data(&queueFamilies));

  u32  graphicsFamily = 0, graphicsAndComputeFamily = 0, presentFamily = 0, computeFamily = 0, transferFamily = 0;
  bool foundGraphicsFamily = false, foundGraphicsAndComputeFamily = false, foundPresentFamily = false, foundComputeFamily = false, foundTransferFamily = false;

  u32 i = 0;
  for (u32 j = 0; j < queueCount; j++)
  {
    const VkQueueFamilyProperties queueFamily = ((VkQueueFamilyProperties*)nv_dynarray_data(&queueFamilies))[j];
    VkBool32                      presentSupport;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceSupportKHR(phys_device, i, surface, &presentSupport));

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
    if (foundGraphicsFamily && foundGraphicsAndComputeFamily && foundPresentFamily && foundComputeFamily && foundTransferFamily) break;

    i++;
  }

  graphics_family_index             = graphicsFamily;
  compute_family_index              = computeFamily;
  transfer_queue_index              = transferFamily;
  present_family_index              = presentFamily;
  graphics_and_compute_family_index = graphicsAndComputeFamily;

  vkGetDeviceQueue(device, graphics_family_index, 0, &graphics_queue);
  vkGetDeviceQueue(device, compute_family_index, 0, &compute_queue);
  vkGetDeviceQueue(device, transfer_queue_index, 0, &transfer_queue);
  vkGetDeviceQueue(device, present_family_index, 0, &present_queue);
  vkGetDeviceQueue(device, graphics_and_compute_family_index, 0, &graphics_and_compute_queue);

  nv_dynarray_destroy(&queueFamilies);
}

void
nv_renderer_initialize_rendering_components(nv_renderer_t* rd, const nv_renderer_config* conf)
{
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
  else { present_mode = VK_PRESENT_MODE_MAILBOX_KHR; }

  nv_gpu_swapchain_create_info scio = nv_zero_init(nv_gpu_swapchain_create_info);
  scio.extent.width                 = rd->render_extent.width;
  scio.extent.height                = rd->render_extent.height;
  scio.present_mode                 = present_mode;
  scio.format                       = swap_chain_image_format;
  scio.color_space                  = swap_chain_color_space;
  scio.image_count                  = swap_chain_image_count;
  nv_gpu_create_swapchain(&scio, &rd->swapchain);

  VkSampleCountFlagBits conf_samples;
  if (conf->samples == NOVA_SAMPLE_COUNT_MAX_SUPPORTED) { conf_samples = MAX_SAMPLES; }
  else { conf_samples = (VkSampleCountFlagBits)conf->samples; }
  const VkSampleCountFlagBits _samples = conf->multisampling_enable ? conf_samples : VK_SAMPLE_COUNT_1_BIT;
  samples                              = _samples;

  if (conf->multisampling_enable)
  {
    nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_MULTISAMPLING;
    rd->attachment_count++;
  }
  nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK;
  nv_gpu_vk_flag_register |= NVVK_PIPELINE_FLAGS_FORCE_CULLING;
  rd->attachment_count++;

  VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
  cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmdPoolCreateInfo.queueFamilyIndex        = graphics_family_index;
  cmdPoolCreateInfo.flags                   = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  nvvk_result_check(vkCreateCommandPool(device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &rd->commandPool));

  const int frames_in_flight = 1 + (int)conf->buffer_mode;

  nv_renderer_frame_render_info data = nv_zero_init(nv_renderer_frame_render_info);
  for (int i = 0; i < frames_in_flight; i++)
  {
    nv_dynarray_push_back(&rd->draw_cmd_buffers, &data);
    nv_dynarray_push_back(&rd->render_data, &data);
  }

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = frames_in_flight;
  cmdAllocInfo.commandPool                 = rd->commandPool;
  nvvk_result_check(vkAllocateCommandBuffers(device, &cmdAllocInfo, (VkCommandBuffer*)nv_dynarray_data(&rd->draw_cmd_buffers)));

  rd->depth_buffer_format = nv_format_to_vk_format(NOVA_FORMAT_D32); // replace (probably)

  nv_gpu_render_pass_create_info rpi = nv_zero_init(nv_gpu_render_pass_create_info);
  rpi.format                         = swap_chain_image_format;
  rpi.depthBufferFormat              = nv_vk_format_to_nv_format(rd->depth_buffer_format);
  rpi.subpass                        = 0;
  rpi.samples                        = samples;
  nv_gpu_create_render_pass(&rpi, &rd->render_pass, nv_gpu_vk_flag_register);

  create_optional_images(rd);
  create_framebuffers_and_swapchain_image_views(rd);
}

nv_renderer_t*
nv_renderer_init(const nv_renderer_config* conf)
{
  if (conf->multisampling_enable == 1)
  {
    nv_log_error("config samples must not be 1 if multisampling is enabled.");
    nv_assert(conf->samples != NOVA_SAMPLE_COUNT_1_SAMPLES);
  }
  struct nv_renderer_t* rd = (nv_renderer_t*)nv_calloc(sizeof(struct nv_renderer_t));

  // how many frames the renderer will render at once
  int frames_in_flight = (1 + (int)conf->buffer_mode);
  if (frames_in_flight <= 0) { frames_in_flight = 1; }

  nv_dynarray_init(sizeof(nv_gpu_sampler), 4, &nv_allocator_default, &g_Samplers);
  nv_dynarray_init(sizeof(nv_draw_call_t), 4, &nv_allocator_default, &rd->drawcalls);
  nv_dynarray_init(sizeof(VkCommandBuffer), frames_in_flight, &nv_allocator_default, &rd->draw_cmd_buffers);
  nv_dynarray_init(sizeof(nv_renderer_frame_render_info), frames_in_flight, &nv_allocator_default, &rd->render_data);

  nv_assert(nv_dynarray_is_initialized(&g_Samplers) == 0);
  nv_assert(nv_dynarray_is_initialized(&rd->drawcalls) == 0);
  nv_assert(nv_dynarray_is_initialized(&rd->draw_cmd_buffers) == 0);
  nv_assert(nv_dynarray_is_initialized(&rd->render_data) == 0);

  if (conf->multisampling_enable) { rd->flags |= NOVA_RENDERER_MULTISAMPLING_ENABLE; }
  if (conf->window_resizable) { rd->flags |= NOVA_RENDERER_WINDOW_RESIZABLE; }
  if (conf->vsync_enabled) { rd->flags |= NOVA_RENDERER_VSYNC_ENABLE; }
  rd->buffer_mode = conf->buffer_mode;

  rd->render_extent.width  = conf->initial_window_size.width;
  rd->render_extent.height = conf->initial_window_size.height;

  nv_renderer_initialize_graphics_singleton();
  nv_renderer_initialize_rendering_components(rd, conf);

  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

    const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    nvvk_result_check(vkCreateSemaphore(device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->render_finish_semaphore));
    nvvk_result_check(vkCreateSemaphore(device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->image_available_semaphore));
    nvvk_result_check(vkCreateFence(device, &fenceCreateInfo, NOVA_VK_ALLOCATOR, &data->in_flight_fence));
  }

  nv_descriptor_pool_init(&g_pool);

  const unsigned char empty_data[3] = { 255, 255, 255 }; // fill rgb with 255 so it's white
  nv_sprite_empty                   = nv_sprite_load_from_memory(empty_data, 1, 1, NOVA_FORMAT_RGB8);
  nv_assert(nv_sprite_empty != NULL);

  nv_log_info("loaded backup sprite");

  nv_camera_init(&camera);

  ctext_init(rd);
  nv_vk_bake_global_pipelines(rd);
  nv_renderer_prepare_quad_renderer(rd);

  return rd;
}

void
_nvvk_renderer_resize(nv_renderer_t* rd)
{
  vkDeviceWaitIdle(device);

  const VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };

  const VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };

  // adhoc method of resetting them
  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    vkDestroySemaphore(device, data->image_available_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroySemaphore(device, data->render_finish_semaphore, NOVA_VK_ALLOCATOR);
    vkDestroyFence(device, data->in_flight_fence, NOVA_VK_ALLOCATOR);

    nv_gpu_destroy_texture(data->depth_image);

    data->image_available_semaphore = NULL;
    data->render_finish_semaphore   = NULL;
    data->in_flight_fence           = NULL;
  }
  nv_gpu_free_memory(rd->depth_image_memory);

  if (rd->color_image)
  {
    nv_gpu_destroy_texture(rd->color_image);
    nv_gpu_free_memory(rd->color_image_memory);
  }

  for (u32 i = 0; i < swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    vkDestroyImageView(
        device,
        nv_gpu_texture_get_view(data->sc_image),
        NOVA_VK_ALLOCATOR); // as the view was silently smushed into the
                            // structure, we just kinda smush it out as well.
    vkDestroyFramebuffer(device, data->color_framebuffer, NOVA_VK_ALLOCATOR);
    nv_free(data->sc_image);
  }
  nv_dynarray_clear(&rd->render_data);

  i32 w, h;
  SDL_Vulkan_GetDrawableSize(window, &w, &h);

  VkSurfaceCapabilitiesKHR surface_capabilities;
  nvvk_result_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device, surface, &surface_capabilities));

  const u32 min_width  = surface_capabilities.minImageExtent.width;
  const u32 min_height = surface_capabilities.minImageExtent.height;

  const u32 max_width  = surface_capabilities.maxImageExtent.width;
  const u32 max_height = surface_capabilities.maxImageExtent.height;

  w = NVM_CLAMP((u32)w, min_width, max_width);
  h = NVM_CLAMP((u32)h, min_height, max_height);

  rd->render_extent = (nv_extent2d){ w, h };

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
  else { present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR; }

  nv_gpu_swapchain_create_info scio = nv_zero_init(nv_gpu_swapchain_create_info);
  scio.extent.width                 = rd->render_extent.width;
  scio.extent.height                = rd->render_extent.height;
  scio.present_mode                 = present_mode;
  scio.format                       = swap_chain_image_format;
  scio.color_space                  = swap_chain_color_space;
  scio.image_count                  = swap_chain_image_count;
  scio.old_swapchain                = old_swapchain;
  nv_gpu_create_swapchain(&scio, &rd->swapchain);
  vkDestroySwapchainKHR(device, old_swapchain, NOVA_VK_ALLOCATOR);

  nv_dynarray_resize(&rd->render_data, swap_chain_image_count);
  create_optional_images(rd);
  create_framebuffers_and_swapchain_image_views(rd);

  for (int i = 0; i < (int)swap_chain_image_count; i++)
  {
    nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, i);
    nvvk_result_check(vkCreateSemaphore(device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->render_finish_semaphore));
    nvvk_result_check(vkCreateSemaphore(device, &semaphoreCreateInfo, NOVA_VK_ALLOCATOR, &data->image_available_semaphore));
    nvvk_result_check(vkCreateFence(device, &fenceCreateInfo, NOVA_VK_ALLOCATOR, &data->in_flight_fence));
  }

  _nv_reset_frame_buffer_resized();
}

bool
nv_renderer_begin(nv_renderer_t* rd)
{
  nv_renderer_frame_render_info* data = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, rd->frame);

  vkWaitForFences(device, 1, &data->in_flight_fence, VK_TRUE, UINT64_MAX);

  const VkResult imageAcquireResult = vkAcquireNextImageKHR(device, rd->swapchain, UINT64_MAX, data->image_available_semaphore, VK_NULL_HANDLE, &rd->image_index);

  const VkCommandBuffer drawBuffer = *(VkCommandBuffer*)nv_dynarray_get(&rd->draw_cmd_buffers, rd->frame);

  if (imageAcquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAcquireResult == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized())
  {
    _nvvk_renderer_resize(rd);
    return false;
  }
  else if (imageAcquireResult != VK_SUCCESS && imageAcquireResult != VK_SUBOPTIMAL_KHR)
  {
    nv_log_error("Failed to acquire image from swapchain");
    return false;
  }

  vkResetFences(device, 1, &data->in_flight_fence);

  // I do, in fact, care about my beloveds

  VkFramebuffer fb = (*(nv_renderer_frame_render_info*)(nv_dynarray_get(&rd->render_data, rd->image_index))).color_framebuffer;

  // Why was this static?
  VkRenderPassBeginInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass      = rd->render_pass,
    .framebuffer     = fb,
    .renderArea      = (VkRect2D){ .extent = (VkExtent2D){ rd->render_extent.width, rd->render_extent.height }, .offset = {} },
    .clearValueCount = 2,
    .pClearValues =
        (VkClearValue[2]){
            { .color = (VkClearColorValue){ { rd->clear_color.x, rd->clear_color.y, rd->clear_color.z, rd->clear_color.w } } },
            { .depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 } },
        },
  };

  const VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, 0, NULL };

  vkBeginCommandBuffer(drawBuffer, &beginInfo);
  vkCmdBeginRenderPass(drawBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport = {
    .x        = 0.0f,
    .y        = 0.0f,
    .width    = rd->render_extent.width,
    .height   = rd->render_extent.height,
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

void
nv_renderer_end(nv_renderer_t* rd)
{
  const VkCommandBuffer drawBuffer = *(VkCommandBuffer*)nv_dynarray_get(&rd->draw_cmd_buffers, rd->frame);

  for (int i = 0; i < (int)rd->ctext->labels.m_size; i++)
  {
    ctext_label_t* label = nv_dynarray_get(&rd->ctext->labels, i);

    nv_sprite_renderer* spr_rd = nv_object_get_sprite_renderer(label->obj);

    ctext_text_render_info_t r_info = ctext_init_text_render_info();
    r_info.position                 = V2F_TO_V3F(nv_object_get_position(label->obj));
    r_info.color                    = spr_rd->color;
    r_info.horizontal               = label->h_align;
    r_info.vertical                 = label->v_align;
    ctext_render(label->fnt, &r_info, "%s", nv_string_data(&label->text));
  }
  ctext_flush_renders(rd);

  // nvui internally checks whether it has been initialized or not
  nvui_render(rd);
  _nv_renderer_flush_renders(rd);

  vkCmdEndRenderPass(drawBuffer);
  vkEndCommandBuffer(drawBuffer);

  VkSubmitInfo submitInfo = nv_zero_init(VkSubmitInfo);
  submitInfo.sType        = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  const nv_renderer_frame_render_info* data               = (nv_renderer_frame_render_info*)nv_dynarray_get(&rd->render_data, rd->frame);
  const VkSemaphore                    waitSemaphores[]   = { data->image_available_semaphore };
  const VkSemaphore                    signalSemaphores[] = { data->render_finish_semaphore };

  VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
  submitInfo.pWaitDstStageMask      = waitStages;

  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores    = waitSemaphores;

  const VkCommandBuffer buffers[] = { drawBuffer };
  submitInfo.commandBufferCount   = nv_arrlen(buffers);
  submitInfo.pCommandBuffers      = buffers;

  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores    = signalSemaphores;

  vkQueueSubmit(present_queue, 1, &submitInfo, data->in_flight_fence);

  VkPresentInfoKHR presentInfo   = nv_zero_init(VkPresentInfoKHR);
  presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores    = signalSemaphores; // This is signalSemaphores so that this starts as
                                                     // soon as the signaled semaphores are signaled.
  presentInfo.pImageIndices  = &rd->image_index;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains    = &rd->swapchain;

  VkResult result = VK_SUCCESS;
  result          = vkQueuePresentKHR(present_queue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || nv_get_frame_buffer_resized()) { _nvvk_renderer_resize(rd); }

  rd->frame = (rd->frame + 1) % (1 + (int)rd->buffer_mode);
}

void
nv_renderer_set_clear_color(struct nv_renderer_t* rd, vec4 col)
{
  rd->clear_color = col;
}
// nvgfx ^^

// engine
u32           MAX_SAMPLES;
unsigned char SUPPORTS_MULTISAMPLING;
flt_t         MAX_ANISOTROPY;

static SDL_UNUSED const char* ValidationLayers[] = {
  "VK_LAYER_KHRONOS_validation",
};

static SDL_UNUSED const char* RequiredInstanceExtensions[] = {
  VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
  VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
};

static SDL_UNUSED const char* WantedInstanceExtensions[] = {
  // VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
};

static SDL_UNUSED const char* WantedDeviceExtensions[] = {
  // VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
};

static SDL_UNUSED const char* RequiredDeviceExtensions[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// we'll just request them as needed

static const VkPhysicalDeviceFeatures WantedFeatures = {
  .samplerAnisotropy = VK_TRUE,
};

void
_VK_DEBUG_LOG(const char* fmt, ...)
{
  const char* preceder  = "";
  const char* succeeder = "\n";
  va_list     args;
  va_start(args, fmt);
  _nv_log(args, " " STR(nvvk_debug_messenger), succeeder, preceder, fmt, 1);
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

  _VK_DEBUG_LOG("%s", pCallbackData->pMessage);

  return VK_FALSE;
}

nv_dynarray_t
setify(u32 i1, u32 i2, u32 i3, u32 i4)
{
  nv_dynarray_t ret;
  nv_dynarray_init(sizeof(u32), 4, &nv_allocator_default, &ret);
  u32 nums[4] = { i1, i2, i3, i4 };
  for (int j = 0; j < (int)nv_arrlen(nums); j++)
  {
    const u32 e          = nums[j];
    bool      already_in = false;
    for (int i = 0; i < (int)nv_dynarray_size(&ret); i++)
    {
      if (e == *(u32*)nv_dynarray_get(&ret, i)) already_in = true;
    }
    if (!already_in) { nv_dynarray_push_back(&ret, &e); }
  }
  return ret;
}

VkInstance
nvvk_create_instance(const char* title)
{
  if (volkInitialize() != VK_SUCCESS)
  {
    nv_log_and_abort("Volk could not initialize. You probably don't have the "
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

  unsigned char      buffer[512];
  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buffer, sizeof(buffer));

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  uint32_t SDLExtensionCount = 0;
  nv_assert(SDL_Vulkan_GetInstanceExtensions(window, &SDLExtensionCount, NULL) == SDL_TRUE);
  const char** SDLExtensions = ac.alloc(&ac, 1, sizeof(const char*) * SDLExtensionCount);
  nv_assert(SDL_Vulkan_GetInstanceExtensions(window, &SDLExtensionCount, SDLExtensions) == SDL_TRUE);

  u32 extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
  // VkExtensionProperties is too big to fit on the stack
  nv_dynarray_t extensions;
  nv_dynarray_init(sizeof(VkExtensionProperties), extensionCount, &nv_allocator_default, &extensions);
  vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, (VkExtensionProperties*)nv_dynarray_data(&extensions));

  int          enabled_exts_count = 0;
  const char** enabled_exts =
      ac.alloc(&ac, 1, sizeof(const char*) * (nv_arrlen(RequiredInstanceExtensions) + SDLExtensionCount + extensionCount + nv_arrlen(WantedInstanceExtensions)));

  for (int i = 0; i < (int)nv_arrlen(RequiredInstanceExtensions); i++)
  {
    const char* ext                  = RequiredInstanceExtensions[i];
    enabled_exts[enabled_exts_count] = ext;
    enabled_exts_count++;
  }

  for (int i = 0; i < (int)SDLExtensionCount; i++)
  {
    const char* ext                  = SDLExtensions[i];
    enabled_exts[enabled_exts_count] = ext;
    enabled_exts_count++;
  }

  for (u32 i = 0; i < extensionCount; i++)
  {
    const char* name = ((VkExtensionProperties*)nv_dynarray_data(&extensions))[i].extensionName;
    for (int j = 0; j < (int)nv_arrlen(WantedInstanceExtensions); j++)
    {
      const char* want = WantedInstanceExtensions[j];
      if (nv_strcmp(name, want) == 0)
      {
        enabled_exts[enabled_exts_count] = name;
        enabled_exts_count++;
        break;
      }
    }
  }

  VkInstanceCreateInfo instance_create_info = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledExtensionCount   = enabled_exts_count,
    .ppEnabledExtensionNames = enabled_exts,
  };

#ifdef DEBUG

  bool     validationLayersAvailable = false;
  uint32_t layerCount                = 0;

  vkEnumerateInstanceLayerProperties(&layerCount, NULL);
  nv_dynarray_t layerProperties;
  nv_dynarray_init(sizeof(VkLayerProperties), layerCount, &nv_allocator_default, &layerProperties);
  vkEnumerateInstanceLayerProperties(&layerCount, (VkLayerProperties*)nv_dynarray_data(&layerProperties));

  if (nv_arrlen(ValidationLayers) != 0)
  {
    for (int j = 0; j < (int)nv_arrlen(ValidationLayers); j++)
    {
      for (uint32_t i = 0; i < layerCount; i++)
      {
        if (nv_strcmp(ValidationLayers[j], ((VkLayerProperties*)nv_dynarray_data(&layerProperties))[i].layerName) == 0) { validationLayersAvailable = true; }
      }
    }

    if (!validationLayersAvailable)
    {
      nv_log_error("Failed to initialize validation layers\nRequested layers:");
      for (int i = 0; i < (int)nv_arrlen(ValidationLayers); i++)
      {
        nv_log_error("\t%s", ValidationLayers[i]);
      }

      nv_log_error("Available Layers::");
      for (uint32_t i = 0; i < layerCount; i++)
        nv_log_error("\t%s", ((VkLayerProperties*)nv_dynarray_data(&layerProperties))[i].layerName);

      nv_log_error("But instance asked for (i.e. are not available):");

      nv_dynarray_t missingLayers;
      nv_dynarray_init(sizeof(const char*), 16, &ac, &missingLayers);

      for (int i = 0; i < (int)nv_arrlen(ValidationLayers); i++)
      {
        const char* layer          = ValidationLayers[i];
        bool        layerAvailable = false;
        for (uint32_t i = 0; i < layerCount; i++)
        {
          if (nv_strcmp(layer, ((VkLayerProperties*)nv_dynarray_data(&layerProperties))[i].layerName) == 0)
          {
            layerAvailable = true;
            break;
          }
        }
        if (!layerAvailable) nv_dynarray_push_back(&missingLayers, &layer);
      }
      for (int i = 0; i < (int)nv_dynarray_size(&missingLayers); i++)
        nv_log_error("\t%s", (const char*)nv_dynarray_get(&missingLayers, i));

      nv_dynarray_destroy(&missingLayers);

      abort();
    }

    instance_create_info.enabledLayerCount   = nv_arrlen(ValidationLayers);
    instance_create_info.ppEnabledLayerNames = ValidationLayers;
  }

#else

  instance_create_info.enabledLayerCount   = 0;
  instance_create_info.ppEnabledLayerNames = NULL;

#endif
  VkInstance instance;
  nvvk_result_check(vkCreateInstance(&instance_create_info, NOVA_VK_ALLOCATOR, &instance));

#ifdef DEBUG
  if (validationLayersAvailable)
  {
    VkDebugUtilsMessengerCreateInfoEXT create_info = nv_zero_init(VkDebugUtilsMessengerCreateInfoEXT);
    create_info.sType                              = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    create_info.pfnUserCallback = nvvk_debug_messenger;

    PFN_vkCreateDebugUtilsMessengerEXT _CreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

    if (_CreateDebugUtilsMessenger)
    {
      VkResult r;
      if ((r = _CreateDebugUtilsMessenger(instance, &create_info, NOVA_VK_ALLOCATOR, &debugMessenger)) != VK_SUCCESS)
        nv_log_error("Vulkan debug messenger could not start. err %i", r);
      else
      {
        const VkDebugUtilsMessengerCallbackDataEXT data = {
          .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT,
          .pNext           = NULL,
          .flags           = 0,
          .pMessageIdName  = 0,
          .messageIdNumber = 0,
          .pMessage        = "Vulkan debug messenger has been set up",
        };
        nvvk_debug_messenger(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &data, NULL);
      }
    }
    else
      nv_log_error("vkCreateDebugUtilsMessengerEXT proc address not found");
  }

  nv_dynarray_destroy(&layerProperties);
#endif

  ac.free(&ac, SDLExtensions);
  ac.free(&ac, enabled_exts);
  nv_dynarray_destroy(&extensions);

  volkLoadInstance(instance);
  return instance;
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
  nv_log_info("(%s) %s", device_type_str, properties.deviceName);
  nv_log_info("Vulkan API Version: %u.%u.%u", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
  nv_log_info(
      "Driver Vendor: %s Version: %u.%u.%u Device ID: %x",
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

  if ((r = vkEnumeratePhysicalDevices(instance, &phys_device_count, NULL)) != VK_SUCCESS) { nv_log_and_abort("Error fetching physical devices. VkResult=%i", r); }

  if (phys_device_count == 0)
  {
    nv_log_and_abort("Huuuhhh??? No physical devices found? Are you running this "
                     "on a banana???");
  }

  nv_dynarray_t physical_devices;
  nv_dynarray_init(sizeof(VkPhysicalDevice), phys_device_count, &nv_allocator_default, &physical_devices);
  vkEnumeratePhysicalDevices(instance, &phys_device_count, (VkPhysicalDevice*)nv_dynarray_data(&physical_devices));

  for (u32 i = 0; i < phys_device_count; i++)
  {
    const VkPhysicalDevice device = ((VkPhysicalDevice*)nv_dynarray_data(&physical_devices))[i];

    uint32_t format_count = 0;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL));

    uint32_t present_mode_count = 0;
    nvvk_result_check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL));

    bool extensionsAvailable = true;

    uint32_t extension_count = 0;
    nvvk_result_check(vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, NULL));
    nv_dynarray_t available_extensions;
    nv_dynarray_init(sizeof(VkExtensionProperties), extension_count, &nv_allocator_default, &available_extensions);
    nvvk_result_check(vkEnumerateDeviceExtensionProperties(device, NULL, &extension_count, (VkExtensionProperties*)nv_dynarray_data(&available_extensions)));

    for (int i = 0; i < (int)nv_arrlen(RequiredDeviceExtensions); i++)
    {
      const char* extension = RequiredDeviceExtensions[i];
      bool        validated = false;
      for (u32 j = 0; j < extension_count; j++)
      {
        if (nv_strcmp(extension, ((VkExtensionProperties*)nv_dynarray_data(&available_extensions))[j].extensionName) == 0) validated = true;
      }
      if (!validated)
      {
        nv_log_error("Failed to validate extension with name: %s", extension);
        extensionsAvailable = false;
      }
    }

    nv_dynarray_destroy(&available_extensions);

    if (extensionsAvailable && format_count > 0 && present_mode_count > 0)
    {
      nvvk_print_device_info(device);
      nv_dynarray_destroy(&physical_devices);
      return device;
    }
  }

  VkPhysicalDevice fallback = ((VkPhysicalDevice*)nv_dynarray_data(&physical_devices))[0];

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(fallback, &properties);

  nv_log_error("No device found. Falling back to device \"%s\".", properties.deviceName);

  nvvk_print_device_info(fallback);

  nv_dynarray_destroy(&physical_devices);

  return fallback;
}

// WARNING: does not init available_extensions itself!!!
static inline void
nvvk_validate_extensions(nv_dynarray_t* available_extensions)
{
  u32 extension_count = 0;
  vkEnumerateDeviceExtensionProperties(phys_device, NULL, &extension_count, NULL);
  nv_dynarray_t extensions;
  nv_dynarray_init(sizeof(VkExtensionProperties), extension_count, &nv_allocator_default, &extensions);
  vkEnumerateDeviceExtensionProperties(phys_device, NULL, &extension_count, (VkExtensionProperties*)nv_dynarray_data(&extensions));

  for (int i = 0; i < (int)nv_arrlen(WantedDeviceExtensions); i++)
  {
    const char* wanted = WantedDeviceExtensions[i];
    for (u32 i = 0; i < extension_count; i++)
    {
      VkExtensionProperties ext = ((VkExtensionProperties*)nv_dynarray_data(&extensions))[i];
      if (nv_strcmp(wanted, ext.extensionName) == 0)
      {
        const char* ext_name_copy = nv_strdup(ext.extensionName);
        nv_dynarray_push_back(available_extensions, &ext_name_copy);
      }
    }
  }

  for (int i = 0; i < (int)nv_arrlen(RequiredDeviceExtensions); i++)
  {
    const char* required  = RequiredDeviceExtensions[i];
    bool        validated = false;
    for (u32 i = 0; i < extension_count; i++)
    {
      const char* extName = ((VkExtensionProperties*)nv_dynarray_data(&extensions))[i].extensionName;
      if (nv_strcmp(required, extName) == 0)
      {
        char* ext_name_copy = nv_strdup(extName);
        nv_dynarray_push_back(available_extensions, &ext_name_copy);
        validated = true;
      }
    }

    if (!validated) nv_log_error("Failed to validate required extension with name %s", required);
  }

  nv_dynarray_destroy(&extensions);
}

static inline void
nvvk_validate_queues(nv_dynarray_t* queue_create_infos)
{
  u32 queue_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, NULL);
  nv_dynarray_t queue_families;
  nv_dynarray_init(sizeof(VkQueueFamilyProperties), queue_count, &nv_allocator_default, &queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(phys_device, &queue_count, (VkQueueFamilyProperties*)nv_dynarray_data(&queue_families));

  // Clang loves complaining about these.
  u32 graphics_family = 0, present_family = 0, compute_family = 0, transfer_family = 0;
  (void)graphics_family, (void)present_family, (void)compute_family, (void)transfer_family;

  bool found_graphics_family = false, found_present_family = false, found_compute_family = false, found_transfer_family = false;

  u32 i = 0;
  for (int j = 0; j < (int)nv_dynarray_size(&queue_families); j++)
  {
    const VkQueueFamilyProperties queue_family    = ((VkQueueFamilyProperties*)nv_dynarray_data(&queue_families))[j];
    VkBool32                      present_support = false;
    nvvk_result_check(vkGetPhysicalDeviceSurfaceSupportKHR(phys_device, i, surface, &present_support));

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
    if (found_graphics_family && found_compute_family && found_present_family && found_transfer_family) break;

    i++;
  }

  nv_dynarray_t unique_queue_families = setify(graphics_family, present_family, compute_family, transfer_family);

  /**
   * Vulkan gives errores sometimes even though the spec states that if queueCount is 1,
   * Only 1 element of pQueuePriorities may be checked. Seems like an issue with the validation layers
   * Should I submit a bug report? Nah. They can deal with it.
   */
  const float queue_priorities[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

  for (int i = 0; i < (int)nv_dynarray_size(&unique_queue_families); i++)
  {
    VkDeviceQueueCreateInfo queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = ((u32*)nv_dynarray_data(&unique_queue_families))[i],
      .queueCount       = 1,
      .pQueuePriorities = queue_priorities,
    };
    nv_dynarray_push_back(queue_create_infos, &queue_info);
  }

  nv_dynarray_destroy(&queue_families);
  nv_dynarray_destroy(&unique_queue_families);
}

VkDevice
nvvk_create_device()
{
  unsigned char      buffer[1024];
  nv_allocator_stack stack;
  nv_allocator_stack_init(&stack, buffer, sizeof(buffer));

  nv_allocator_t ac;
  nv_allocator_bind_stack_allocator(&ac, &stack);

  nv_dynarray_t enabled_extensions;
  nv_dynarray_init(sizeof(const char*), nv_arrlen(WantedDeviceExtensions) + nv_arrlen(RequiredDeviceExtensions), &ac, &enabled_extensions);
  nvvk_validate_extensions(&enabled_extensions);

  nv_dynarray_t queue_create_infos;
  nv_dynarray_init(sizeof(VkDeviceQueueCreateInfo), 0, &nv_allocator_default, &queue_create_infos);
  nvvk_validate_queues(&queue_create_infos);

  VkDeviceCreateInfo deviceCreateInfo = {
    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount    = nv_dynarray_size(&queue_create_infos),
    .pQueueCreateInfos       = (const VkDeviceQueueCreateInfo*)nv_dynarray_data(&queue_create_infos),
    .enabledExtensionCount   = nv_dynarray_size(&enabled_extensions),
    .ppEnabledExtensionNames = (const char* const*)nv_dynarray_data(&enabled_extensions),
    .pEnabledFeatures        = &WantedFeatures,
  };

  nvvk_result_check(vkCreateDevice(phys_device, &deviceCreateInfo, NOVA_VK_ALLOCATOR, &device));

  for (int i = 0; i < (int)nv_dynarray_size(&enabled_extensions); i++)
  {
    const char* ext_name_allocated = *(const char**)nv_dynarray_get(&enabled_extensions, i);
    nv_free((void*)ext_name_allocated);
  }
  nv_dynarray_destroy(&enabled_extensions);
  nv_dynarray_destroy(&queue_create_infos);

  return device;
}

void
_nvvk_initialize_context(const char* title)
{
  instance = nvvk_create_instance(title);
  nv_assert(instance != NULL);

  if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) { nv_log_and_abort("Surface creation failed.\nSDL reports: %s", SDL_GetError()); }

  phys_device = _nvvk_choose_physical_device(instance, surface);

  device = nvvk_create_device();
  nv_assert(device != NULL);

  volkLoadDevice(device);

  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(phys_device, &props);

  MAX_ANISOTROPY         = props.limits.maxSamplerAnisotropy;
  SUPPORTS_MULTISAMPLING = true;

  const VkSampleCountFlags samples = props.limits.framebufferColorSampleCounts;
  if (samples & VK_SAMPLE_COUNT_64_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_64_BIT; }
  else if (samples & VK_SAMPLE_COUNT_32_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_32_BIT; }
  else if (samples & VK_SAMPLE_COUNT_16_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_16_BIT; }
  else if (samples & VK_SAMPLE_COUNT_8_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_8_BIT; }
  else if (samples & VK_SAMPLE_COUNT_4_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_4_BIT; }
  else if (samples & VK_SAMPLE_COUNT_2_BIT) { MAX_SAMPLES = VK_SAMPLE_COUNT_2_BIT; }
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
_ctext_load_font_upload_glyph_atlas(const nv_texture_atlas_t* atlas, cfont_t* dst)
{
  nv_gpu_texture_create_info image_info = {
    .format      = NOVA_FORMAT_R8,
    .samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .type        = VK_IMAGE_TYPE_2D,
    .usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .extent      = (nv_extent3D){ .width = atlas->w, .height = atlas->h, .depth = 1 },
    .arraylayers = 1,
    .miplevels   = 1,
  };
  nv_gpu_create_texture(&image_info, &dst->texture);

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(device, nv_gpu_texture_get(dst->texture), &imageMemoryRequirements);

  nv_gpu_allocate_memory(imageMemoryRequirements.size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL, &dst->texture_mem);
  nv_gpu_bind_texture_to_memory(dst->texture_mem, 0, dst->texture);

  const int atlas_w = atlas->w, atlas_h = atlas->h;

  nv_image_t atlas_img = (nv_image_t){ .w = atlas_w, .h = atlas_h, .fmt = NOVA_FORMAT_R8, .data = (unsigned char*)atlas->data };
  nv_gpu_write_to_texture(dst->texture, &atlas_img);

  const nv_gpu_sampler_create_info sampler_info = { .filter = VK_FILTER_LINEAR, .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT };
  nv_gpu_create_sampler(&sampler_info, &dst->sampler);
}

void
_ctext_load_font_update_descriptors(nv_ctext_module* ctext, cfont_t* dst)
{
  const VkDescriptorImageInfo ctext_bitmap_image_info = {
    .sampler     = nv_gpu_sampler_get(dst->sampler),
    .imageView   = nv_gpu_texture_get_view(dst->texture),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet writeSet = { .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstSet          = ctext->desc_set->set,
                                    .dstBinding      = 0,
                                    .descriptorCount = 1,
                                    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                    .pImageInfo      = &ctext_bitmap_image_info };
  for (int i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    writeSet.dstArrayElement = i;
    nv_descriptor_set_submit_write(ctext->desc_set, &writeSet);
  }
}

// typedef struct _ctext_font_load
// {
//   nv_renderer_t* rd;
//   const char*    font_path;
//   int            scale;
//   cfont_t*       dst;
// } _ctext_font_load;

// static inline void*
// _ctext_load_font_internal(void* _user_data) {
//   _ctext_font_load* load      = (_ctext_font_load*)_user_data;
//   nv_renderer_t*    rd        = load->rd;
//   const char*       font_path = load->font_path;
//   int               scale     = load->scale;
//   cfont_t*          dst       = load->dst;

//   nv_hashmap_init(16, sizeof(char), sizeof(ctext_glyph_t), NULL, NULL, &nv_allocator_default, &dst->glyph_map);
//   nv_dynarray_init(sizeof(ctext_drawcall_t), 4, &nv_allocator_default, &dst->drawcalls);

//   fontc_file_t f_file;
//   fontc_read_font(font_path, &f_file);

//   nv_texture_atlas_t atlas;

//   dst->line_height = f_file.header.line_height;
//   dst->space_width = f_file.header.space_width;
//   atlas.w          = f_file.header.bmpwidth;
//   atlas.h          = f_file.header.bmpheight;
//   atlas.data       = f_file.bitmap;

//   for (int i = 0; i < f_file.header.numglyphs; i++) {
//     ctext_glyph_t glyph = {
//       .x0      = f_file.glyphs[i].x0,
//       .x1      = f_file.glyphs[i].x1,
//       .y0      = f_file.glyphs[i].y0,
//       .y1      = f_file.glyphs[i].y1,
//       .l       = f_file.glyphs[i].l,
//       .r       = f_file.glyphs[i].r,
//       .b       = f_file.glyphs[i].b,
//       .t       = f_file.glyphs[i].t,
//       .advance = f_file.glyphs[i].advance,
//     };
//     char glyphi = f_file.glyphs[i].codepoint;
//     nv_hashmap_insert(&dst->glyph_map, &glyphi, &glyph);
//   }

//   _ctext_load_font_upload_glyph_atlas(&atlas, dst);
//   _ctext_load_font_update_descriptors(rd->ctext, dst);

//   nv_free(f_file.glyphs);
//   nv_free(f_file.bitmap);

//   return NULL;
// }

void
ctext_load_font(nv_renderer_t* rdr, const char* font_path, int scale, cfont_t* dst)
{
  if (!rdr || !dst)
  {
    nv_log_error("rdr or dst is NULL!");
    return;
  }

  if (scale <= 0)
  {
    nv_log_error("attempting to load a font with 0 fontscale.");
    return;
  }

  *dst = (cfont_t){};

  fontc_file_t f_file;
  if (fontc_load_font(font_path, &f_file) != 0)
  {
    nv_log_error("There was an error loading the font file. Skipping");
    return;
  }

  // Store a pointer to the font for future reference
  *(cfont_t**)nv_dynarray_push_empty(&rdr->ctext->fonts) = dst;

  dst->rd = rdr;

  nv_hashmap_init(16, sizeof(char), sizeof(ctext_glyph_t), NULL, NULL, &nv_allocator_default, &dst->glyph_map);
  nv_dynarray_init(sizeof(ctext_drawcall_t), 4, &nv_allocator_default, &dst->drawcalls);

  nv_texture_atlas_t atlas;

  dst->line_height = f_file.header.line_height;
  dst->space_width = f_file.header.space_width;
  atlas.w          = f_file.header.bmpwidth;
  atlas.h          = f_file.header.bmpheight;
  atlas.data       = f_file.bitmap;

  for (int i = 0; i < f_file.header.numglyphs; i++)
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
    char glyphi = f_file.glyphs[i].codepoint;
    nv_hashmap_insert(&dst->glyph_map, &glyphi, &glyph);
  }

  _ctext_load_font_upload_glyph_atlas(&atlas, dst);
  _ctext_load_font_update_descriptors(rdr->ctext, dst);

  fontc_clean_font_file(&f_file);

  if (ctext_validate_font(dst) != 0)
  {
    // nv_log_error("Broken font. Something has gone horribly wrong");
    return;
  }
}

int
ctext_validate_font(const cfont_t* fnt)
{
  // If the renderer of the font has died, die along with the renderer.
  if (!fnt || !fnt->rd) return -1;
  // TODO: Add support for a constant canary across the whole project?
  if (fnt->glyph_map.m_canary != CONT_CANARY || fnt->drawcalls.m_canary != CONT_CANARY) return -1;
  return 0;
}

void
ctext_destroy_font(cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return;
  }

  nv_gpu_destroy_texture(fnt->texture);
  nv_gpu_free_memory(fnt->texture_mem);

  if (fnt->buffer.buffer != NULL)
  {
    nv_gpu_destroy_buffer(&fnt->buffer);
    nv_gpu_free_memory(fnt->buffer_mem);
  }
  nv_dynarray_destroy(&fnt->drawcalls);
  nv_hashmap_destroy(&fnt->glyph_map);
}

bool
_ctext_font_resize_buffer(cfont_t* fnt, size_t new_buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return false;
  }

  size_t new_allocation_size;

  if (fnt->allocated_size < new_buffer_size) { new_allocation_size = NVM_MAX(fnt->allocated_size * 2, new_buffer_size); }
  else if (new_buffer_size < (fnt->allocated_size / 3)) { new_allocation_size = NVM_MAX(fnt->allocated_size / 3, new_buffer_size); }
  else
    return false;

  vkDeviceWaitIdle(device);

  if (fnt->buffer.buffer)
  {
    nv_gpu_destroy_buffer(&fnt->buffer);
    nv_gpu_free_memory(fnt->buffer_mem);
  }

  nv_gpu_allocate_memory(new_allocation_size, NOVA_GPU_MEMORY_USAGE_GPU_LOCAL | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE | NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &fnt->buffer_mem);

  nv_gpu_create_buffer(new_allocation_size, NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &fnt->buffer);
  nv_gpu_bind_buffer_to_memory(fnt->buffer_mem, 0, &fnt->buffer);

  fnt->allocated_size = new_allocation_size;
  fnt->to_render      = 0;

  return true;
}

void
_ctext_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return;
  }

  const VkCommandBuffer cmd       = nv_renderer_get_draw_buffer(rd);
  const VkDeviceSize    offsets[] = { 0 };

  struct push_constants pc = nv_zero_init(struct push_constants);

  const VkPipeline       pipeline        = g_Pipelines.Ctext.pipeline;
  const VkPipelineLayout pipeline_layout = g_Pipelines.Ctext.pipeline_layout;

  const VkDescriptorSet sets[]           = { camera.sets->set, rd->ctext->desc_set->set };
  const uint32_t        camera_ub_offset = nv_renderer_get_frame(rd) * sizeof(nv_camera_uniform_buffer);

  // Viewport && scissor are set by renderer so no need to set them here
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 2, sets, 1, &camera_ub_offset);
  vkCmdBindVertexBuffers(cmd, 0, 1, &fnt->buffer.buffer, offsets);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindIndexBuffer(cmd, fnt->buffer.buffer, fnt->index_buffer_offset, VK_INDEX_TYPE_UINT32);

  int offset = 0;
  for (int i = 0; i < (int)nv_dynarray_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_dynarray_get(&fnt->drawcalls, i);

    pc.model = drawcall->model;
    pc.scale = drawcall->scale;
    pc.color = drawcall->color;
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct push_constants), &pc);

    vkCmdDrawIndexed(cmd, drawcall->index_count, 1, 0, offset, 0);
    offset += drawcall->vertex_count;
  }
}

static nv_dynarray_t
split_string_by_lines(const char* str)
{
  nv_dynarray_t result;
  char*         substr;
  const size_t  str_len = nv_strlen(str);
  size_t        i_start = 0;

  nv_dynarray_init(sizeof(char*), 16, &nv_allocator_default, &result);

  for (size_t i = 0; i < str_len; i++)
  {
    if (str[i] == '\n')
    {
      if (i > i_start)
      {
        substr = nv_substr(str, i_start, i - i_start);
        if (substr)
        {
          // substr should now handle errors internally
          nv_dynarray_push_back(&result, &substr);
        }
      }
      i_start = i + 1;
    }
  }

  if (i_start < str_len)
  {
    substr = nv_substr(str, i_start, str_len - i_start);
    nv_dynarray_push_back(&result, &substr);
  }

  return result;
}

// Get the unscaled size of the string
// Warning: slow
static void
ctext_get_text_size(const cfont_t* fnt, const char* str, flt_t* w, flt_t* h)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    *w = FLT_MAX;
    *h = FLT_MAX;
    return;
  }

  flt_t width = 0.0f, height = fnt->line_height, prev_width = 0.0f;

  bool is_new_line = true;

  while (*str)
  {
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
          prev_width = NVM_MAX(width, prev_width);
        }
        width       = 0.0f;
        is_new_line = 1;
        break;
      default:
      {
        const ctext_glyph_t* glyph = (ctext_glyph_t*)nv_hashmap_find(&fnt->glyph_map, str);
        if (!glyph) { break; }
        width += glyph->advance;
        is_new_line = 0;
        break;
      }
    }
    str++;
  }

  if (!is_new_line) { prev_width = NVM_MAX(width, prev_width); }

  if (w) { *w = prev_width; }

  if (h) { *h = -height; }
}

// TODO: Implement instancing: Very hard
static int
_ctext_render_line(const cfont_t* fnt, const char* str, const ctext_drawcall_t* drawcall, flt_t scale, flt_t zpos, const int glyph_iter, flt_t x, const flt_t y)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return -1;
  }

  int iter = 0;
  while (*str)
  {
    switch (*str)
    {
      case ' ': x += fnt->space_width * scale; break;
      case '\t': x += fnt->space_width * 4.0f * scale; break;
      default:
      {
        const ctext_glyph_t* glyph = (const ctext_glyph_t*)nv_hashmap_find(&fnt->glyph_map, str);
        if (!glyph)
        {
          nv_log_info("no glyph when rendering char %i", *str);
          break;
        }
        const flt_t glyph_x0 = (glyph->x0 * scale) + x;
        const flt_t glyph_x1 = (glyph->x1 * scale) + x;
        const flt_t glyph_y0 = (glyph->y0 * scale) + y;
        const flt_t glyph_y1 = (glyph->y1 * scale) + y;

        const int             index_offset = (fnt->chars_drawn + iter) * 4;
        ctext_glyph_vertex_t* v_out        = drawcall->vertices + (glyph_iter + iter) * 4;
        // clang-format off
        v_out[0] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y0, zpos}, (vec2f){glyph->l, glyph->b} };
        v_out[1] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y0, zpos}, (vec2f){glyph->r, glyph->b} };
        v_out[2] = (ctext_glyph_vertex_t){ (vec3f){glyph_x1, glyph_y1, zpos}, (vec2f){glyph->r, glyph->t} };
        v_out[3] = (ctext_glyph_vertex_t){ (vec3f){glyph_x0, glyph_y1, zpos}, (vec2f){glyph->l, glyph->t} };
        // clang-format on

        u32* i_out = drawcall->indices + (glyph_iter + iter) * 6;
        i_out[0]   = index_offset;
        i_out[1]   = index_offset + 1;
        i_out[2]   = index_offset + 2;
        i_out[3]   = index_offset + 2;
        i_out[4]   = index_offset + 3;
        i_out[5]   = index_offset;

        x += glyph->advance * scale;
        iter++;
        break;
      }
    }
    str++;
  }

  return iter;
}

static inline int
_ctext_get_effective_length(const char* buf, int buflen)
{
  int len = 0;
  for (int i = 0; i < buflen; i++)
  {
    const char c = buf[i];
    // I've tried to use isprint here
    // It causes some weird artefacts for some damned reason.
    if (c != ' ' && c != '\t' && c != '\n') { len++; }
  }
  return len;
}

int
_ctext_gen_vertices(cfont_t* fnt, ctext_drawcall_t* drawcall, const ctext_text_render_info_t* pInfo, const char* str)
{
  if (*str == 0) // nv_strlen == 0
  {
    return 1;
  }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return -1;
  }

  nv_dynarray_t lines;
  flt_t         text_w, text_h;
  flt_t         scale;
  flt_t         ypos, xpos;
  int           actual_chars_drawn;
  const int     old_chars_drawn = fnt->chars_drawn;

  lines = split_string_by_lines(str);

  scale = pInfo->scale;
  ctext_get_text_size(fnt, str, &text_w, NULL);
  text_h = -fnt->line_height * ((int)lines.m_size - 1);

  if (pInfo->scale_for_fit)
  {
    flt_t scale_x = (pInfo->bbox.x) / text_w;
    flt_t scale_y = (pInfo->bbox.y) / text_h;
    // multiply with normal scale to get new scale
    scale *= fminf(scale_x, scale_y);
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
    default:
      __builtin_unreachable();
      nv_log_error("Invalid vertical alignment. Specified (int)%u. (Implement?)", pInfo->vertical);
      break;
  }
  for (size_t i = 0; i < lines.m_size; i++)
  {
    // render_line returns the number of chars DRAWN. not the number of
    // characters in the string.
    const char* line = ((char**)nv_dynarray_data(&lines))[i];

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
        nv_log_error("Invalid horizontal alignment. Specified (int)%u. (Implement?)", pInfo->horizontal);
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
        NVM_MAX(actual_chars_drawn, 0),
        xpos,
        (ypos + ((flt_t)i * fnt->line_height * scale)));
  }

  for (size_t i = 0; i < lines.m_size; i++)
  {
    char* line = ((char**)nv_dynarray_data(&lines))[i];
    nv_free(line);
  }
  nv_dynarray_destroy(&lines);

  return 0;
}

// TODO: Replace with a better system
// that renders the characters all at once.
void
_ctext_render_and_submit_drawcall(cfont_t* fnt, const ctext_text_render_info_t* pInfo, char* buffer, size_t buffer_size)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return;
  }

  size_t effective_length = _ctext_get_effective_length(buffer, buffer_size);
  if (effective_length == 0) { return; }

  const size_t vertex_count    = effective_length * 4;
  const size_t index_count     = effective_length * 6;
  const size_t allocation_size = (vertex_count * sizeof(ctext_glyph_vertex_t)) + (index_count * sizeof(u32));

  void* allocation = nv_malloc(allocation_size);

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
  nv_dynarray_push_back(&fnt->drawcalls, &drawcall);
}

void
ctext_render(cfont_t* fnt, const ctext_text_render_info_t* pInfo, const char* fmt, ...)
{
  if (!fnt || !pInfo || !fmt) return;

  // if (!nv_async_is_task_complete(&fnt->load_task)) { return; }
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font");
    return;
  }

  char*  buffer;
  size_t buffer_size;

  va_list arg;
  va_start(arg, fmt);

  buffer_size = nv_vsnprintf(NULL, SIZE_MAX, fmt, arg);
  buffer      = nv_malloc(buffer_size + 1);
  nv_assert(buffer != NULL);

  va_end(arg);
  va_start(arg, fmt);

  nv_vsnprintf(buffer, buffer_size + 1, fmt, arg);
  buffer[buffer_size] = 0;

  va_end(arg);

  _ctext_render_and_submit_drawcall(fnt, pInfo, buffer, buffer_size);

  nv_free(buffer);

  fnt->rendered_this_frame = 1;
}

void
_ctext_upload_vertices_and_render_drawcalls(nv_renderer_t* rd, cfont_t* fnt)
{
  u32 total_vertex_byte_size = 0;
  u32 total_index_count      = 0;

  for (int i = 0; i < (int)nv_dynarray_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_dynarray_get(&fnt->drawcalls, i);
    total_vertex_byte_size += drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    total_index_count += drawcall->index_count;
  }

  if (total_vertex_byte_size == 0 || total_index_count == 0) { return; }

  const u32 total_index_byte_size = total_index_count * sizeof(u32);
  const u32 total_buffer_size     = total_index_byte_size + total_vertex_byte_size;

  const bool fnt_buffer_resized = _ctext_font_resize_buffer(fnt, total_buffer_size);

  if (fnt->to_render && !fnt_buffer_resized) { _ctext_render_drawcalls(rd, fnt); }

  uint8_t* mapped = NULL;
  nv_gpu_map_memory(fnt->buffer_mem, total_buffer_size, 0, (void**)&mapped);

  if (mapped == NULL)
  {
    nv_log_error("mapping font buffer memory failed");
    return;
  }

  // this may be dumb but I am too

  u32 vertex_copy_iterator = 0;
  u32 index_copy_iterator  = 0;
  for (int i = 0; i < (int)nv_dynarray_size(&fnt->drawcalls); i++)
  {
    const ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_dynarray_get(&fnt->drawcalls, i);
    nv_memcpy(mapped + vertex_copy_iterator, drawcall->vertices, drawcall->vertex_count * sizeof(ctext_glyph_vertex_t));
    nv_memcpy(mapped + total_vertex_byte_size + index_copy_iterator, drawcall->indices, drawcall->index_count * sizeof(u32));
    vertex_copy_iterator += drawcall->vertex_count * sizeof(ctext_glyph_vertex_t);
    index_copy_iterator += drawcall->index_count * sizeof(u32);
  }
  nv_gpu_unmap_memory(fnt->buffer_mem);

  fnt->index_buffer_offset = total_vertex_byte_size;
  fnt->index_count         = total_index_count;
  fnt->to_render           = true;
}

void
_ctext_flush_font(nv_renderer_t* rd, cfont_t* fnt)
{
  if (!fnt->rendered_this_frame) { return; }
  else { fnt->rendered_this_frame = 0; }

  _ctext_upload_vertices_and_render_drawcalls(rd, fnt);
  fnt->chars_drawn = 0;

  for (int i = 0; i < (int)nv_dynarray_size(&fnt->drawcalls); i++)
  {
    ctext_drawcall_t* drawcall = (ctext_drawcall_t*)nv_dynarray_get(&fnt->drawcalls, i);
    if (drawcall && drawcall->vertices) nv_free(drawcall->vertices);
  }
  nv_dynarray_clear(&fnt->drawcalls);
}

void
ctext_flush_renders(nv_renderer_t* rd)
{
  for (int i = 0; i < (int)rd->ctext->fonts.m_size; i++)
  {
    cfont_t* fnt = *(cfont_t**)nv_dynarray_get(&rd->ctext->fonts, i);
    _ctext_flush_font(rd, fnt);
  }
}

ctext_label_t*
ctext_create_label(nv_scene_t* scene, cfont_t* fnt)
{
  ctext_label_t label = {
    .h_align = CTEXT_HORI_ALIGN_LEFT,
    .v_align = CTEXT_VERT_ALIGN_TOP,
    .index   = fnt->rd->ctext->labels.m_size,
    .text    = nv_string_init(0, &nv_allocator_default),
    .fnt     = fnt,
    .obj     = nv_object_create(scene, "Text Label", 0, 0, 0, (vec2){}, (vec2){ 1.0f, 1.0f }, NOVA_OBJECT_NO_COLLISION),
  };
  nv_dynarray_push_back(&fnt->rd->ctext->labels, &label);
  return &(((ctext_label_t*)fnt->rd->ctext->labels.m_data)[fnt->rd->ctext->labels.m_size - 1]);
}

void
ctext_destroy_label(ctext_label_t* label)
{
  nv_string_destroy(&label->text);
  nv_dynarray_remove(&label->fnt->rd->ctext->labels, label->index);
}

nv_object*
ctext_label_get_object(const ctext_label_t* label)
{
  return label->obj;
}
void
ctext_label_set_text(ctext_label_t* label, const char* text)
{
  nv_string_set(&label->text, text);
}
void
ctext_label_set_horizontal_align(ctext_label_t* label, ctext_hori_align h_align)
{
  label->h_align = h_align;
}
void
ctext_label_set_vertical_align(ctext_label_t* label, ctext_vert_align v_align)
{
  label->v_align = v_align;
}

void
ctext_label_set_text_scale(ctext_label_t* label, flt_t scale)
{
  label->scale = scale;
}

void
ctext_init(struct nv_renderer_t* rd)
{
  rd->ctext              = nv_calloc(sizeof(nv_ctext_module));
  nv_ctext_module* ctext = rd->ctext;

  nv_dynarray_init(sizeof(cfont_t*), 4, &nv_allocator_default, &ctext->fonts);
  nv_dynarray_init(sizeof(ctext_label_t), 4, &nv_allocator_default, &ctext->labels);

  nv_assert(nv_dynarray_is_initialized(&ctext->fonts) == 0);
  nv_assert(nv_dynarray_is_initialized(&ctext->labels) == 0);

  const VkDescriptorSetLayoutBinding bindings[] = {
    // binding; descriptorType; descriptorCount; stageFlags;
    // pImmutableSamplers;
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CTEXT_MAX_FONT_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
  };

  nv_allocate_descriptor_set(&g_pool, bindings, nv_arrlen(bindings), &ctext->desc_set);

  const VkDescriptorImageInfo empty_img_info = { .sampler     = nv_sprite_get_sampler(nv_sprite_empty),
                                                 .imageView   = nv_sprite_get_vk_image_view(nv_sprite_empty),
                                                 .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

  VkWriteDescriptorSet write_set = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = ctext->desc_set->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &empty_img_info,
  };
  for (int i = 0; i < CTEXT_MAX_FONT_COUNT; i++)
  {
    write_set.dstArrayElement = i;
    nv_descriptor_set_submit_write(ctext->desc_set, &write_set);
  }
}

void
ctext_shutdown(struct nv_renderer_t* rd)
{
  if (!rd || !rd->ctext) return;
  nv_dynarray_destroy(&rd->ctext->fonts);
  nv_dynarray_destroy(&rd->ctext->labels);
  nv_free(rd->ctext);
}

flt_t
ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox)
{
  if (!fnt || !str || ctext_validate_font(fnt) != 0) return INFINITY;

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
  set->writes = nv_realloc(set->writes, (set->nwrites + 1) * sizeof(VkWriteDescriptorSet));
  nv_assert(set->writes != NULL);
  if (!nv_memcpy(&set->writes[set->nwrites], write, sizeof(VkWriteDescriptorSet))) { return -1; }
  set->nwrites++;
  vkUpdateDescriptorSets(device, 1, write, 0, 0);
  return 0;
}

void
nv_descriptor_set_destroy(nv_descriptor_set_t* set)
{
  vkDestroyDescriptorSetLayout(device, set->layout, NOVA_VK_ALLOCATOR);
  nv_free(set->writes);
  nv_free(set);
}

void
nv_descriptor_pool_destroy(nv_descriptor_pool_t* pool)
{
  for (int i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_destroy(pool->sets[i]);
  }
  nv_free(pool->sets);
  vkDestroyDescriptorPool(device, pool->pool, NOVA_VK_ALLOCATOR);
}

int
_nv_descriptor_pool_allocate(nv_descriptor_pool_t* pool)
{
  nv_assert(pool != NULL);

  VkDescriptorPoolSize allocations[11]     = {};
  int                  descriptors_written = 0;

  for (int i = 0; i < 11; i++)
  {
    if (pool->descriptors[i].capacity == 0) { continue; }
    allocations[descriptors_written] = (VkDescriptorPoolSize){ pool->descriptors[i].type, pool->descriptors[i].capacity };
    descriptors_written++;
  }

  if (descriptors_written == 0) { return 0; }

  bool need_more_max_sets = (pool->nsets + 1) > pool->max_child_sets;
  if (need_more_max_sets) { pool->max_child_sets = NVM_MAX(pool->max_child_sets * 2, 1); }

  VkDescriptorPoolCreateInfo poolInfo = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = pool->max_child_sets, .poolSizeCount = descriptors_written, .pPoolSizes = allocations
  };

  VkDescriptorPool new_pool;
  nvvk_result_check(vkCreateDescriptorPool(device, &poolInfo, NOVA_VK_ALLOCATOR, &new_pool));
  if (!new_pool) { return -1; }

  VkDescriptorSet* new_sets = nv_malloc(sizeof(VkDescriptorSet) * pool->nsets);
  nv_assert(new_sets != NULL);

  if (pool->nsets > 0)
  {
    VkDescriptorSetLayout* layouts = nv_malloc(sizeof(VkDescriptorSetLayout) * pool->nsets);
    for (int i = 0; i < pool->nsets; i++)
    {
      layouts[i] = pool->sets[i]->layout;
      nv_assert(layouts[i] != NULL);
    }

    VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
    setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool              = new_pool;
    setAllocInfo.descriptorSetCount          = pool->nsets;
    setAllocInfo.pSetLayouts                 = layouts;
    nvvk_result_check(vkAllocateDescriptorSets(device, &setAllocInfo, new_sets));
    if (!new_sets) { return -1; }

    nv_free(layouts);
  }

  int ncopies = 0;
  for (int i = 0; i < pool->nsets; i++)
  {
    ncopies += pool->sets[i]->nwrites;
  }
  VkCopyDescriptorSet* copies = nv_malloc(sizeof(VkCopyDescriptorSet) * ncopies);

  nv_assert(pool->sets != NULL);

  ncopies = 0;
  for (int i = 0; i < pool->nsets; i++)
  {
    nv_descriptor_set_t* old_set = pool->sets[i];

    for (int writei = 0; writei < old_set->nwrites; writei++)
    {
      nv_assert(new_sets[i] != NULL);

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
  vkUpdateDescriptorSets(device, 0, NULL, ncopies, copies);

  if (pool->pool) { vkDestroyDescriptorPool(device, pool->pool, NOVA_VK_ALLOCATOR); }
  pool->pool = new_pool;

  nv_free(new_sets);
  nv_free(copies);
  return 0;
}

int
nv_descriptor_pool_init(nv_descriptor_pool_t* dst)
{
  *dst                                 = (nv_descriptor_pool_t){};
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
  if (_nv_descriptor_pool_allocate(dst) != 0) { return -1; }
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
      nv_descriptor_pool_size* descriptor = &pool->descriptors[i];
      if (descriptor->type == bindings[j].descriptorType)
      {
        descriptor->capacity = NVM_MAX(descriptor->capacity * 2, (int)bindings[j].descriptorCount + descriptor->capacity);
        need_realloc         = 1;
      }
    }
  }

  if (need_realloc || ((pool->nsets + 1) > pool->max_child_sets))
  {
    if ((pool->nsets + 1) > pool->max_child_sets)
    {
      pool->max_child_sets = NVM_MAX(pool->max_child_sets * 2, 1);
      pool->sets           = nv_realloc(pool->sets, pool->max_child_sets * sizeof(nv_descriptor_set_t));
      nv_assert(pool->sets != NULL);
    }

    _nv_descriptor_pool_allocate(pool);
  }

  nv_descriptor_set_t* set = nv_calloc(sizeof(nv_descriptor_set_t));
  nv_assert(set != NULL);

  pool->sets[pool->nsets] = set;
  pool->nsets++;

  (*dst) = set;

  set->pool   = pool;
  set->writes = nv_malloc(sizeof(VkWriteDescriptorSet));
  nv_assert(set->writes != NULL);

  VkDescriptorSetLayoutCreateInfo layoutinfo = nv_zero_init(VkDescriptorSetLayoutCreateInfo);
  layoutinfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutinfo.pBindings                       = bindings;
  layoutinfo.bindingCount                    = nbindings;
  nvvk_result_check(vkCreateDescriptorSetLayout(device, &layoutinfo, NOVA_VK_ALLOCATOR, &set->layout));
  if (set->layout == NULL) return -1;

  VkDescriptorSetAllocateInfo setAllocInfo = nv_zero_init(VkDescriptorSetAllocateInfo);
  setAllocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAllocInfo.descriptorPool              = pool->pool;
  setAllocInfo.descriptorSetCount          = 1;
  setAllocInfo.pSetLayouts                 = &set->layout;
  nvvk_result_check(vkAllocateDescriptorSets(device, &setAllocInfo, &set->set));
  if (set->set == NULL) return -1;

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
  nvvk_result_check(vkCreateDescriptorSetLayout(device, &layoutinfo, NOVA_VK_ALLOCATOR, &g_Pipelines.Unlit.descriptor_layout));

  nvsm_shader_t *vertex, *fragment;
  nv_assert(nvsm_load_shader("Unlit/vert", &vertex) == 0);
  nv_assert(nvsm_load_shader("Unlit/frag", &fragment) == 0);

  nv_assert(vertex != NULL && fragment != NULL);

  const nvsm_shader_t*        shaders[] = { vertex, fragment };
  const VkDescriptorSetLayout layouts[] = { camera.sets->layout, g_Pipelines.Unlit.descriptor_layout };

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
  pc.format                      = swap_chain_image_format;
  pc.subpass                     = 0;
  pc.render_pass                 = nv_renderer_get_render_pass(rd);

  pc.nAttributeDescriptions = nv_arrlen(attributeDescriptions);
  pc.pAttributeDescriptions = attributeDescriptions;

  pc.nPushConstants = nv_arrlen(pushConstants);
  pc.pPushConstants = pushConstants;

  pc.nBindingDescriptions = nv_arrlen(bindingDescriptions);
  pc.pBindingDescriptions = bindingDescriptions;

  pc.nShaders = nv_arrlen(shaders);
  pc.pShaders = shaders;

  pc.nDescriptorLayouts = nv_arrlen(layouts);
  pc.pDescriptorLayouts = layouts;

  pc.extent.width  = RenderExtent.width;
  pc.extent.height = RenderExtent.height;
  pc.samples       = samples;
  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.Unlit.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.Unlit.pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.Unlit.pipeline, 0);
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
  VkDescriptorSetLayout layouts[] = { camera.sets->layout, rd->ctext->desc_set->layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.format                      = swap_chain_image_format;
  pc.subpass                     = 0;
  pc.render_pass                 = nv_renderer_get_render_pass(rd);

  pc.nAttributeDescriptions = nv_arrlen(attributeDescriptions);
  pc.pAttributeDescriptions = attributeDescriptions;

  pc.nPushConstants = nv_arrlen(pushConstants);
  pc.pPushConstants = pushConstants;

  pc.nBindingDescriptions = nv_arrlen(bindingDescriptions);
  pc.pBindingDescriptions = bindingDescriptions;

  pc.nShaders = nv_arrlen(shaders);
  pc.pShaders = (const struct nvsm_shader_t* const*)shaders;

  pc.nDescriptorLayouts = nv_arrlen(layouts);
  pc.pDescriptorLayouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.extent.width                = RenderExtent.width;
  pc.extent.height               = RenderExtent.height;
  pc.blend_state                 = &blend;
  pc.samples                     = samples;

  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.Ctext.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.Ctext.pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.Ctext.pipeline, 0);
}

void
__BakeDebugLinePipeline(nv_renderer_t* rd)
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
  nv_assert(nvsm_load_shader("Debug/Line/vert", &vertex) != -1);
  nv_assert(nvsm_load_shader("Debug/Line/frag", &fragment) != -1);

  nvsm_shader_t*        shaders[] = { vertex, fragment };
  VkDescriptorSetLayout layouts[] = { camera.sets->layout };

  const nv_gpu_pipeline_blend_state blend = nv_gpu_init_pipeline_blend_state(NVVK_BLEND_PRESET_ALPHA);

  nv_gpu_pipeline_create_info pc = nv_gpu_init_pipeline_create_info();
  pc.format                      = swap_chain_image_format;

  pc.topology    = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  pc.render_pass = nv_renderer_get_render_pass(rd);

  pc.nAttributeDescriptions = 0;
  pc.pAttributeDescriptions = NULL;

  pc.nPushConstants = nv_arrlen(pushConstants);
  pc.pPushConstants = pushConstants;

  pc.nBindingDescriptions = 0;
  pc.pBindingDescriptions = NULL;

  pc.nShaders = nv_arrlen(shaders);
  pc.pShaders = (const struct nvsm_shader_t* const*)shaders;

  pc.nDescriptorLayouts = nv_arrlen(layouts);
  pc.pDescriptorLayouts = layouts;

  const nv_extent2d RenderExtent = nv_renderer_get_render_extent(rd);
  pc.extent.width                = RenderExtent.width;
  pc.extent.height               = RenderExtent.height;
  pc.blend_state                 = &blend;
  pc.samples                     = samples;

  nv_gpu_create_pipeline_layout(&pc, &g_Pipelines.Line.pipeline_layout);
  pc.pipeline_layout = g_Pipelines.Line.pipeline_layout;
  nv_gpu_create_graphics_pipeline(&pc, &g_Pipelines.Line.pipeline, 0);
}

void
nv_vk_bake_global_pipelines(nv_renderer_t* rd)
{
  __BakeUnlitPipeline(rd);
  __BakeDebugLinePipeline(rd);
  __BakeCtextPipeline(rd);
}

void
nv_vk_destroy_pipeline(nv_vk_pipeline* pipeline)
{
  vkDestroyPipeline(device, pipeline->pipeline, NOVA_VK_ALLOCATOR);
  vkDestroyPipelineLayout(device, pipeline->pipeline_layout, NOVA_VK_ALLOCATOR);
  vkDestroyDescriptorSetLayout(device, pipeline->descriptor_layout, NOVA_VK_ALLOCATOR);
}

void
nv_vk_destroy_global_pipelines()
{
  nv_vk_pipeline pipelines[] = {
    g_Pipelines.Unlit,
    g_Pipelines.Ctext,
    g_Pipelines.Line,
  };
  for (int i = 0; i < (int)nv_arrlen(pipelines); i++)
  {
    nv_vk_destroy_pipeline(&pipelines[i]);
  }
}

void
nv_gpu_create_graphics_pipeline(const nv_gpu_pipeline_create_info* pCreateInfo, VkPipeline* dstPipeline, u32 flags)
{
  NVVK_REQUIRED_PTR(device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstPipeline);
  NVVK_REQUIRED_PTR(pCreateInfo->render_pass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->nShaders, 0);
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
    .vertexBindingDescriptionCount   = pCreateInfo->nBindingDescriptions,
    .pVertexBindingDescriptions      = pCreateInfo->pBindingDescriptions,
    .vertexAttributeDescriptionCount = pCreateInfo->nAttributeDescriptions,
    .pVertexAttributeDescriptions    = pCreateInfo->pAttributeDescriptions,
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
      .srcColorBlendFactor = blendState->srcColorBlendFactor,
      .dstColorBlendFactor = blendState->dstColorBlendFactor,
      .colorBlendOp        = blendState->colorBlendOp,
      .srcAlphaBlendFactor = blendState->srcAlphaBlendFactor,
      .dstAlphaBlendFactor = blendState->dstAlphaBlendFactor,
      .alphaBlendOp        = blendState->alphaBlendOp,
      .colorWriteMask      = blendState->colorWriteMask,
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

  VkPipelineShaderStageCreateInfo* shader_infos = (VkPipelineShaderStageCreateInfo*)nv_calloc(pCreateInfo->nShaders * sizeof(VkPipelineShaderStageCreateInfo));
  for (int i = 0; i < pCreateInfo->nShaders; i++)
  {
    shader_infos[i].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_infos[i].stage  = (VkShaderStageFlagBits)pCreateInfo->pShaders[i]->stage;
    shader_infos[i].module = (VkShaderModule)pCreateInfo->pShaders[i]->shader_module;
    shader_infos[i].pName  = "main";
  }

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {
    .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount          = pCreateInfo->nShaders,
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
      .front                 = (VkStencilOpState){},
      .back                  = (VkStencilOpState){},
      .minDepthBounds        = 0.0f,
      .maxDepthBounds        = 1.0f,
    };

    graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilState;
  }

  graphicsPipelineCreateInfo.basePipelineHandle = base_pipeline;

  // if(cacheIsNull) cacheCreator.join();
  nvvk_result_check(vkCreateGraphicsPipelines(device, pCreateInfo->cache, 1, &graphicsPipelineCreateInfo, NOVA_VK_ALLOCATOR, dstPipeline));

  base_pipeline = *dstPipeline;

  nv_free(shader_infos);
}

void
nv_gpu_create_render_pass(nv_gpu_render_pass_create_info const* pCreateInfo, VkRenderPass* dstRenderPass, u32 flags)
{
  NVVK_REQUIRED_PTR(device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstRenderPass);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);

  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK)) { NVVK_NOT_EQUAL_TO(pCreateInfo->depthBufferFormat, NOVA_FORMAT_UNDEFINED); }

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

  nv_dynarray_t attachments;
  nv_dynarray_init(sizeof(VkAttachmentDescription), 5, &nv_allocator_default, &attachments);
  nv_dynarray_push_back(&attachments, &colorAttachmentDescription);

  VkAttachmentDescription depthAttachment    = nv_zero_init(VkAttachmentDescription);
  VkAttachmentReference   depthAttachmentRef = nv_zero_init(VkAttachmentReference);
  if (HAS_FLAG(NVVK_PIPELINE_FLAGS_FORCE_DEPTH_CHECK))
  {
    depthAttachment = (VkAttachmentDescription){
      .flags          = 0,
      .format         = nv_format_to_vk_format(pCreateInfo->depthBufferFormat),
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

    depthAttachmentRef.attachment = nv_dynarray_size(&attachments);
    depthAttachmentRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    nv_dynarray_push_back(&attachments, &depthAttachment);
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

    colorAttachmentResolveRef.attachment = nv_dynarray_size(&attachments);
    colorAttachmentResolveRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    nv_dynarray_push_back(&attachments, &colorAttachmentResolve);

    subpass.pResolveAttachments = &colorAttachmentResolveRef;
  }

  VkRenderPassCreateInfo renderPassInfo = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .pNext           = NULL,
    .flags           = 0,
    .attachmentCount = nv_dynarray_size(&attachments),
    .pAttachments    = (const VkAttachmentDescription*)nv_dynarray_data(&attachments),
    .subpassCount    = 1,
    .pSubpasses      = &subpass,
    .dependencyCount = 0,
    .pDependencies   = NULL,
  };
  nvvk_result_check(vkCreateRenderPass(device, &renderPassInfo, NOVA_VK_ALLOCATOR, dstRenderPass));

  nv_dynarray_destroy(&attachments);
}

void
nv_gpu_create_pipeline_layout(nv_gpu_pipeline_create_info const* pCreateInfo, VkPipelineLayout* dstLayout)
{
  NVVK_REQUIRED_PTR(device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_REQUIRED_PTR(dstLayout);

  // int totalLayouts = 0;
  // for (int i = 0; i < pCreateInfo->nShaders; i++) {
  // 	totalLayouts += pCreateInfo->pShaders[i]->nsetlayouts;
  // }

  // nv_dynarray_t *sets = nv_dynarray_init(sizeof(VkDescriptorSetLayout, &nv_allocator_default),
  // totalLayouts);

  // for (int i = 0; i < pCreateInfo->nShaders; i++) {
  // 	const nvsm_shader_t *shader = pCreateInfo->pShaders[i];
  // 	for (int j = 0; j < shader->nsetlayouts; j++) {
  // 		nv_dynarray_push_back(sets, &shader->setlayouts[j]);
  // 	}
  // }

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .setLayoutCount         = pCreateInfo->nDescriptorLayouts,
    .pSetLayouts            = pCreateInfo->pDescriptorLayouts,
    .pushConstantRangeCount = pCreateInfo->nPushConstants,
    .pPushConstantRanges    = pCreateInfo->pPushConstants,
  };
  nvvk_result_check(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, NOVA_VK_ALLOCATOR, dstLayout));
}

void
nv_gpu_create_swapchain(nv_gpu_swapchain_create_info const* pCreateInfo, VkSwapchainKHR* dstSwapchain)
{
  NVVK_REQUIRED_PTR(device);
  NVVK_REQUIRED_PTR(pCreateInfo);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.width, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->extent.height, 0);
  NVVK_NOT_EQUAL_TO(pCreateInfo->format, NOVA_FORMAT_UNDEFINED);
  NVVK_NOT_EQUAL_TO(pCreateInfo->image_count, 0);

  VkSwapchainCreateInfoKHR swapChainCreateInfo = {
    .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext                 = NULL,
    .flags                 = 0,
    .surface               = surface,
    .minImageCount         = pCreateInfo->image_count,
    .imageFormat           = nv_format_to_vk_format(pCreateInfo->format),
    .imageColorSpace       = pCreateInfo->color_space,
    .imageExtent           = pCreateInfo->extent,
    .imageArrayLayers      = 1,
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode           = pCreateInfo->present_mode,
    .clipped               = VK_TRUE,
    .oldSwapchain          = pCreateInfo->old_swapchain,
  };
  nvvk_result_check(vkCreateSwapchainKHR(device, &swapChainCreateInfo, NOVA_VK_ALLOCATOR, dstSwapchain));
}

nv_gpu_pipeline_blend_state
nv_gpu_init_pipeline_blend_state(nv_gpu_pipeline_blend_preset preset)
{
  nv_gpu_pipeline_blend_state ret = nv_zero_init(nv_gpu_pipeline_blend_state);
  ret.colorWriteMask              = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  switch (preset)
  {
    case NVVK_BLEND_PRESET_NONE:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ALPHA:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_ADDITIVE:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_MULTIPLICATIVE:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_PREMULTIPLIED_ALPHA:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
    case NVVK_BLEND_PRESET_SUBTRACTIVE:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.colorBlendOp        = VK_BLEND_OP_REVERSE_SUBTRACT;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      ret.alphaBlendOp        = VK_BLEND_OP_REVERSE_SUBTRACT;
      break;
    default:
      ret.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.colorBlendOp        = VK_BLEND_OP_ADD;
      ret.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      ret.alphaBlendOp        = VK_BLEND_OP_ADD;
      break;
  }
  return ret;
}

void
nv_vk_create_buffer(size_t size, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags propertyFlags, VkBuffer* dstBuffer, VkDeviceMemory* retMem, bool externallyAllocated)
{
  if (size == 0) { return; }

  VkBuffer       newBuffer;
  VkDeviceMemory newMemory;

  VkBufferCreateInfo bufferCreateInfo = nv_zero_init(VkBufferCreateInfo);
  bufferCreateInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size               = size;
  bufferCreateInfo.usage              = usageFlags;
  bufferCreateInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(vkCreateBuffer(device, &bufferCreateInfo, NOVA_VK_ALLOCATOR, &newBuffer));

  VkMemoryRequirements bufferMemoryRequirements;
  vkGetBufferMemoryRequirements(device, newBuffer, &bufferMemoryRequirements);

  if (!externallyAllocated)
  {
    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = bufferMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = nv_vk_get_mem_type(bufferMemoryRequirements.memoryTypeBits, propertyFlags);
    nvvk_result_check(vkAllocateMemory(device, &allocInfo, NOVA_VK_ALLOCATOR, &newMemory));

    nvvk_result_check(vkBindBufferMemory(device, newBuffer, newMemory, 0));
    *retMem = newMemory;
  }

  *dstBuffer = newBuffer;
}

void
nv_vk_stage_buffer_transfer(VkBuffer dst, void* data, size_t size)
{
  VkBuffer       stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  VkBufferCreateInfo stagingBufferInfo = nv_zero_init(VkBufferCreateInfo);
  stagingBufferInfo.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  stagingBufferInfo.size               = size;
  stagingBufferInfo.usage              = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  stagingBufferInfo.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;
  nvvk_result_check(vkCreateBuffer(device, &stagingBufferInfo, NOVA_VK_ALLOCATOR, &stagingBuffer));

  VkMemoryRequirements stagingBufferMemoryRequirements;
  vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferMemoryRequirements);

  VkMemoryAllocateInfo stagingBufferAlloc = nv_zero_init(VkMemoryAllocateInfo);
  stagingBufferAlloc.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  stagingBufferAlloc.allocationSize       = stagingBufferMemoryRequirements.size;
  stagingBufferAlloc.memoryTypeIndex =
      nv_vk_get_mem_type(stagingBufferMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  nvvk_result_check(vkAllocateMemory(device, &stagingBufferAlloc, NOVA_VK_ALLOCATOR, &stagingBufferMemory));

  nvvk_result_check(vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0));

  void* mapped;
  nvvk_result_check(vkMapMemory(device, stagingBufferMemory, 0, size, 0, &mapped));
  nv_memcpy(mapped, data, size);
  vkUnmapMemory(device, stagingBufferMemory);

  const VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = size };

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();
  vkCmdCopyBuffer(cmd, stagingBuffer, dst, 1, &copy);
  nvvk_result_check(nv_vk_end_command_buffer(cmd, transfer_queue, 1));

  vkDestroyBuffer(device, stagingBuffer, NOVA_VK_ALLOCATOR);
  vkFreeMemory(device, stagingBufferMemory, NOVA_VK_ALLOCATOR);
}

u32
nv_vk_get_mem_type(const u32 memoryTypeBits, const VkMemoryPropertyFlags memoryProperties)
{
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(phys_device, &properties);

  for (u32 i = 0; i < properties.memoryTypeCount; i++)
  {
    if ((memoryTypeBits & (1 << i)) && (properties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties) return i;
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
nv_vk_begin_command_buffer()
{
  if (!cmd_pool)
  {
    VkCommandPoolCreateInfo cmdPoolCreateInfo = nv_zero_init(VkCommandPoolCreateInfo);
    cmdPoolCreateInfo.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.queueFamilyIndex        = graphics_family_index;
    cmdPoolCreateInfo.flags                   = 0;
    nvvk_result_check(vkCreateCommandPool(device, &cmdPoolCreateInfo, NOVA_VK_ALLOCATOR, &cmd_pool));
  }

  VkCommandBufferAllocateInfo cmdAllocInfo = nv_zero_init(VkCommandBufferAllocateInfo);
  cmdAllocInfo.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmdAllocInfo.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmdAllocInfo.commandBufferCount          = 1;
  cmdAllocInfo.commandPool                 = cmd_pool;
  nvvk_result_check(vkAllocateCommandBuffers(device, &cmdAllocInfo, &buffer));

  return nv_vk_begin_command_buffer_from(buffer);
}

VkResult
nv_vk_end_command_buffer(VkCommandBuffer cmd, VkQueue queue, bool waitForExecution)
{
  VkResult res = vkEndCommandBuffer(cmd);
  if (res != VK_SUCCESS) return res;

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
    res = vkCreateFence(device, &fenceInfo, NOVA_VK_ALLOCATOR, &fence);
    if (res != VK_SUCCESS) return res;
  }

  static pthread_mutex_t vk_queue_mutex;
  static bool            mutex_init = 0;
  if (!mutex_init)
  {
    pthread_mutex_init(&vk_queue_mutex, NULL);
    mutex_init = 1;
  }

  pthread_mutex_lock(&vk_queue_mutex);

  res = vkQueueSubmit(queue, 1, &submitInfo, fence);
  if (res != VK_SUCCESS) return res;

  pthread_mutex_unlock(&vk_queue_mutex);

  if (waitForExecution)
  {
    if (fence != VK_NULL_HANDLE)
    {
      res = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
      if (res != VK_SUCCESS) return res;
      vkDestroyFence(device, fence, NOVA_VK_ALLOCATOR);
    }
    vkDeviceWaitIdle(device);
    vkFreeCommandBuffers(device, cmd_pool, 1, &cmd);
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
  int   file_size = -1;

  f = fopen(path, "rb");
  if (!f)
  {
    nv_log_error("fopen error: %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fseek(f, 0, SEEK_END) != 0)
  {
    nv_log_error("fseek error: %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  file_size = ftell(f);

  if (dst == NULL) { goto CLEANUP_AND_RETURN; }

  rewind(f);
  if (errno != 0)
  {
    nv_log_error("error in rewind?? %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

  if (fread(dst, file_size, 1, f) != 1)
  {
    nv_log_error("fread error %s", strerror(errno));
    goto CLEANUP_AND_RETURN;
  }

CLEANUP_AND_RETURN:
  *dstSize = file_size;
  if (f) nv_safecall_c_fn(fclose(f));
}

void
nv_vk_stage_image_transfer(VkImage dst, const void* data, int width, int height, int image_size)
{
  VkBuffer       stagingBuffer       = VK_NULL_HANDLE;
  VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(device, dst, &mem_req);

  const VkBufferCreateInfo stagingBufferInfo = {
    .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size        = mem_req.size,
    .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  nvvk_result_check(vkCreateBuffer(device, &stagingBufferInfo, NOVA_VK_ALLOCATOR, &stagingBuffer));

  VkMemoryRequirements stagingBufferRequirements;
  vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingBufferRequirements);

  const VkMemoryAllocateInfo stagingBufferAllocInfo = {
    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize  = stagingBufferRequirements.size,
    .memoryTypeIndex = nv_vk_get_mem_type(stagingBufferRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
  };

  nvvk_result_check(vkAllocateMemory(device, &stagingBufferAllocInfo, NOVA_VK_ALLOCATOR, &stagingBufferMemory));
  nvvk_result_check(vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0));

  void* stagingBufferMapped;
  nvvk_result_check(vkMapMemory(device, stagingBufferMemory, 0, stagingBufferRequirements.size, 0, &stagingBufferMapped));
  nv_memcpy(stagingBufferMapped, data, image_size);
  vkUnmapMemory(device, stagingBufferMemory);

  const VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  nv_vk_transition_texture_layout(
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

  nv_vk_transition_texture_layout(
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

  nv_vk_end_command_buffer(cmd, transfer_queue, true);

  vkDestroyBuffer(device, stagingBuffer, NOVA_VK_ALLOCATOR);
  vkFreeMemory(device, stagingBufferMemory, NOVA_VK_ALLOCATOR);
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
  vkGetPhysicalDeviceFormatProperties(phys_device, nv_format_to_vk_format(fmt), &formatProperties);

  if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0)
  {
    // format not supported
    const char* fmt_str;
    nv_format_to_string(fmt, &fmt_str);
    nv_log_warning("Format %i (%s) unsupported. Using NOVA_FORMAT_RGBA8", fmt, fmt_str);
    return NOVA_FORMAT_RGBA8;
  }

  return fmt;
}

void
nv_vk_create_texture_empty(
    u32 width, u32 height, nv_format format, VkSampleCountFlagBits samples, VkImageUsageFlags usage, int* image_size, VkImage* dst, VkDeviceMemory* dstMem)
{
  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) { format = nv_vk_get_supported_format_for_draw(format); }

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
  nvvk_result_check(vkCreateImage(device, &imageCreateInfo, NOVA_VK_ALLOCATOR, dst));

  VkMemoryRequirements imageMemoryRequirements;
  vkGetImageMemoryRequirements(device, *dst, &imageMemoryRequirements);

  if (image_size) { *image_size = imageMemoryRequirements.size; }

  // allow for preallocated memory.
  if (dstMem != NULL)
  {
    const u32 localDeviceMemoryIndex = nv_vk_get_mem_type(imageMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkMemoryAllocateInfo allocInfo = nv_zero_init(VkMemoryAllocateInfo);
    allocInfo.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize       = imageMemoryRequirements.size;
    allocInfo.memoryTypeIndex      = localDeviceMemoryIndex;

    nvvk_result_check(vkAllocateMemory(device, &allocInfo, NOVA_VK_ALLOCATOR, dstMem));
    nvvk_result_check(vkBindImageMemory(device, *dst, *dstMem, 0));
  }
}

u8*
nv_vk_create_texture_from_disk(const char* path, u32* width, u32* height, nv_format* channels, VkImage* dst, VkDeviceMemory* dstMem)
{
  nv_image_t tex = nv_image_load(path);

  nv_assert(tex.data != NULL);

  *width    = tex.w;
  *height   = tex.h;
  *channels = tex.fmt;

  nv_vk_create_texture_from_memory(tex.data, tex.w, tex.h, *channels, dst, dstMem);
  return tex.data;
}

void
nv_vk_transition_texture_layout(
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
  NVVK_REQUIRED_PTR(device);
  NVVK_REQUIRED_PTR(phys_device);
  NVVK_REQUIRED_PTR(surface);
  NVVK_REQUIRED_PTR(dst_format);
  NVVK_REQUIRED_PTR(dst_color_space);

  u32 formatCount = 0;
  nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, VK_NULL_HANDLE));
  nv_dynarray_t surface_formats;
  nv_dynarray_init(sizeof(VkSurfaceFormatKHR), formatCount, &nv_allocator_default, &surface_formats);
  nvvk_result_check(vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device, surface, &formatCount, (VkSurfaceFormatKHR*)nv_dynarray_data(&surface_formats)));

  VkSurfaceFormatKHR selected_format = { VK_FORMAT_MAX_ENUM, VK_COLOR_SPACE_MAX_ENUM_KHR };

  const VkSurfaceFormatKHR desired_formats[] = { { VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
                                                 { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } };

  for (u32 i = 0; i < formatCount; i++)
  {
    const VkSurfaceFormatKHR* surface_format = (VkSurfaceFormatKHR*)nv_dynarray_get(&surface_formats, i);
    for (u32 j = 0; j < nv_arrlen(desired_formats); j++)
    {
      if (surface_format->format == desired_formats[j].format && surface_format->colorSpace == desired_formats[j].colorSpace)
      {
        selected_format.format     = surface_format->format;
        selected_format.colorSpace = surface_format->colorSpace;
      }
    }
  }

  nv_dynarray_destroy(&surface_formats);
  if (selected_format.format == VK_FORMAT_MAX_ENUM || selected_format.colorSpace == VK_COLOR_SPACE_MAX_ENUM_KHR) { return VK_FALSE; }
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
  if (requestedImageCount < surfaceCapabilities.maxImageCount) requestedImageCount = surfaceCapabilities.maxImageCount;

  return requestedImageCount;
}

// NOVA_GPU_OBJECTS

struct nv_gpu_memory_t
{
  VkDeviceMemory      memory;
  void*               mapped;
  size_t              map_size, map_offset; // if mapped, the mapping size and offset
  nv_gpu_memory_usage usage;
  size_t              size;
};

// these parameters should be replaced
// properties should be replaced by usage. Like NOVA_GPU_MEMORY_USAGE_GPU,
// CPU_TO_GPU, GPU_TO_CPU, etc.
void
nv_gpu_allocate_memory(size_t size, nv_gpu_memory_usage usage, nv_gpu_memory_t** dst)
{
  (*dst)                                 = nv_calloc(sizeof(nv_gpu_memory_t));
  (*dst)->size                           = size;
  (*dst)->usage                          = usage;
  const VkMemoryPropertyFlags properties = (VkMemoryPropertyFlags)usage;

  VkMemoryAllocateInfo alloc_info = nv_zero_init(VkMemoryAllocateInfo);
  alloc_info.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize       = size;

  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_properties);

  for (u32 i = 0; i < mem_properties.memoryTypeCount; i++)
  {
    if ((properties & mem_properties.memoryTypes[i].propertyFlags) == properties)
    {
      alloc_info.memoryTypeIndex = i;
      break;
    }
  }

  nvvk_result_check(vkAllocateMemory(device, &alloc_info, NOVA_VK_ALLOCATOR, &(*dst)->memory));
}

void
nv_gpu_create_buffer(size_t size, int alignment, VkBufferUsageFlags usage, nv_gpu_buffer_t* dst)
{
  dst->size      = size;
  dst->alignment = alignment;
  dst->type      = usage;

  const int aligned_sz = ALIGN_UP(size, alignment);

  VkBufferCreateInfo buffer_info = nv_zero_init(VkBufferCreateInfo);
  buffer_info.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size               = aligned_sz;
  buffer_info.usage              = usage;
  nvvk_result_check(vkCreateBuffer(device, &buffer_info, NOVA_VK_ALLOCATOR, &dst->buffer));
}

void
nv_gpu_write_to_local_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset)
{
  nv_gpu_buffer_t staging_buffer;
  nv_gpu_create_buffer(size, NOVA_GPU_ALIGNMENT_UNNECESSARY, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging_buffer);

  nv_gpu_memory_t* staging_memory;
  nv_gpu_allocate_memory(size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE, &staging_memory);

  nv_gpu_bind_buffer_to_memory(staging_memory, 0, &staging_buffer);

  void* mapped = NULL;
  nv_gpu_map_memory(staging_memory, size, 0, &mapped);
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(staging_memory);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  VkBufferCopy copy = {
    .srcOffset = 0,
    .dstOffset = offset,
    .size      = size,
  };
  vkCmdCopyBuffer(cmd, staging_buffer.buffer, buffer->buffer, 1, &copy);

  if (nv_vk_end_command_buffer(cmd, graphics_queue, 1) != VK_SUCCESS) { nv_log_error("Failed to write data to GPU buffer"); }
}

void
nv_gpu_write_to_uniform_buffer(nv_gpu_buffer_t* buffer, size_t size, void* data, size_t offset)
{
  void* mapped = NULL;
  nv_gpu_map_memory(buffer->memory, size, offset, &mapped);
  if (mapped == NULL)
  {
    nv_log_error("error in mapping");
    return;
  }
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(buffer->memory);
}

void
nv_gpu_map_buffer(nv_gpu_buffer_t* buffer)
{
  nv_assert(!buffer->is_mapped);
  nv_gpu_map_memory(buffer->memory, buffer->size, buffer->offset, &buffer->mapping);
  buffer->is_mapped = 1;
}

void
nv_gpu_unmap_buffer(nv_gpu_buffer_t* buffer)
{
  nv_assert(buffer->is_mapped);
  nv_gpu_unmap_memory(buffer->memory);
  buffer->is_mapped = 0;
}

void
nv_gpu_write_to_buffer(nv_gpu_buffer_t* buffer, size_t size, const void* data, size_t offset)
{
  if (buffer->is_mapped)
  {
    nv_assert(buffer->mapping != NULL);
    nv_memcpy(buffer->mapping + offset, data, size);
    return;
  }
  if (!(buffer->memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE))
  {
    nv_gpu_write_to_local_buffer(buffer, size, data, offset);
    return;
  }
  void* mapped = NULL;
  nv_gpu_map_memory(buffer->memory, size, offset, &mapped);
  nv_memcpy(mapped, data, size);
  nv_gpu_unmap_memory(buffer->memory);
}

void
nv_gpu_map_memory(nv_gpu_memory_t* memory, size_t size, size_t offset, void** out)
{
  nv_assert(memory != NULL);
  nv_assert(out != NULL);
  nv_assert(size != 0);

  if (!(memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE))
  {
    nv_log_error("Memory usage does not have NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE "
                 "Use nv_gpu_write_to_buffer() instead.");
    *out = NULL;
    return;
  }
  if (memory->map_size != 0)
  {
    *out = memory->mapped;
    return;
  }
  if (vkMapMemory(device, memory->memory, offset, size, 0, &memory->mapped) != VK_SUCCESS)
  {
    nv_log_error("Memory could not be mapped for write");
    *out = NULL;
    return;
  }
  memory->map_size   = size;
  memory->map_offset = offset;
  *out               = memory->mapped;
}

void
nv_gpu_unmap_memory(nv_gpu_memory_t* memory)
{
  // if (!(memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE)) {
  //     VkMappedMemoryRange range = {
  //         .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
  //         .memory = memory->memory,
  //         .offset = memory->map_offset,
  //         .size = memory->map_size,
  //     };
  //     vkFlushMappedMemoryRanges(device, 1, &range);
  // }
  memory->map_offset = 0;
  memory->map_size   = 0;
  vkUnmapMemory(device, memory->memory);
}

void
nv_gpu_free_memory(nv_gpu_memory_t* mem)
{
  if (mem && mem->memory)
  {
    vkFreeMemory(device, mem->memory, NULL);
    nv_free(mem);
  }
}

// I think these given an error when, say offset is too big so maybe we can
// check their return values?
void
nv_gpu_bind_buffer_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_buffer_t* buffer)
{
  buffer->memory = mem;
  buffer->offset = offset;

  nvvk_result_check(vkBindBufferMemory(device, buffer->buffer, mem->memory, offset));
}

void
nv_gpu_texture_attach_view(nv_gpu_texture* tex, VkImageView view)
{
  tex->view = view;
}

void
nv_gpu_bind_texture_to_memory(nv_gpu_memory_t* mem, size_t offset, nv_gpu_texture* tex)
{
  tex->memory = mem;
  tex->offset = offset;
  nvvk_result_check(vkBindImageMemory(device, tex->image, mem->memory, offset));

  VkImageAspectFlags aspect = 0;
  if (nv_format_has_depth_channel(tex->format)) { aspect = VK_IMAGE_ASPECT_DEPTH_BIT; }
  else if (nv_format_has_color_channel(tex->format)) { aspect = VK_IMAGE_ASPECT_COLOR_BIT; }

  if (nv_format_has_stencil_channel(tex->format)) { aspect |= VK_IMAGE_ASPECT_STENCIL_BIT; }

  VkImageSubresourceRange subresourceRange = {
    .aspectMask = aspect, .baseMipLevel = 0, .levelCount = VK_REMAINING_MIP_LEVELS, .baseArrayLayer = 0, .layerCount = VK_REMAINING_ARRAY_LAYERS
  };

  nv_format dst_format = tex->format;
  dst_format           = nv_vk_get_supported_format_for_draw(dst_format);

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
  nvvk_result_check(vkCreateImageView(device, &view_info, NOVA_VK_ALLOCATOR, &tex->view));
}

void
nv_gpu_destroy_buffer(nv_gpu_buffer_t* buffer)
{
  if (!buffer->buffer)
  {
    nv_log_info("Attempt to destroy a buffer %u which has a NULL VkBuffer", buffer);
    return;
  }
  vkDeviceWaitIdle(device);
  vkDestroyBuffer(device, buffer->buffer, NOVA_VK_ALLOCATOR);
  nv_memset(buffer, 0, sizeof(nv_gpu_buffer_t));
}

void
nv_gpu_destroy_texture(nv_gpu_texture* tex)
{
  vkDeviceWaitIdle(device);
  if (!tex->view)
  {
    nv_log_info("Attempt to destroy an image view which is NULL");
    return;
  }
  if (!tex->image)
  {
    nv_log_info("Attempt to destroy an image which is NULL");
    return;
  }
  vkDestroyImage(device, tex->image, NOVA_VK_ALLOCATOR);
  vkDestroyImageView(device, tex->view, NOVA_VK_ALLOCATOR);
  nv_free(tex);
}

int
nv_gpu_get_buffer_size(const nv_gpu_buffer_t* buffer)
{
  return buffer->size;
}

void
nv_gpu_buffer_readback(const nv_gpu_buffer_t* buffer, void* dest)
{
  if (!(buffer->memory->usage & NOVA_GPU_BUFFER_TYPE_TRANSFER_SOURCE))
  {
    nv_log_error("Cannot readback from buffer that is not transfer source");
    return;
  }

  nv_gpu_buffer_t  staging;
  nv_gpu_memory_t* staging_mem;
  nv_gpu_create_buffer(buffer->size, NOVA_GPU_ALIGNMENT_UNNECESSARY, NOVA_GPU_BUFFER_TYPE_TRANSFER_DESTINATION, &staging);
  nv_gpu_allocate_memory(buffer->size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &staging_mem);
  nv_gpu_bind_buffer_to_memory(staging_mem, 0, &staging);

  VkCommandBuffer cmd = nv_vk_begin_command_buffer();

  VkBufferCopy copy = { .srcOffset = 0, .dstOffset = 0, .size = buffer->size };
  vkCmdCopyBuffer(cmd, buffer->buffer, staging.buffer, 1, &copy);

  nv_vk_end_command_buffer(cmd, transfer_queue, 1);

  void* mapped = NULL;
  nv_gpu_map_memory(staging_mem, buffer->size, 0, &mapped);
  nv_assert(mapped != NULL);
  nv_memcpy(dest, mapped, buffer->size);
  nv_gpu_unmap_memory(staging_mem);

  nv_gpu_destroy_buffer(&staging);
  nv_gpu_free_memory(staging_mem);
}

void
nv_gpu_get_texture_size(const nv_gpu_texture* tex, int* w, int* h)
{
  if (w) { *w = tex->extent.width; }
  if (h) { *h = tex->extent.height; }
}

void
nv_gpu_create_sampler(const nv_gpu_sampler_create_info* pInfo, nv_gpu_sampler** sampler)
{
  for (int i = 0; i < (int)g_Samplers.m_size; i++)
  {
    nv_gpu_sampler* cache = &((nv_gpu_sampler*)nv_dynarray_data(&g_Samplers))[i];
    if (cache != NULL && cache->filter == pInfo->filter && cache->mipmap_mode == pInfo->mipmap_mode && cache->address_mode == pInfo->address_mode
        && cache->max_anisotropy == pInfo->max_anisotropy && cache->mip_lod_bias == pInfo->mip_lod_bias && cache->min_lod == pInfo->min_lod && cache->max_lod == pInfo->max_lod
        && cache->vksampler != NULL)
    {
      *sampler = cache;
      return;
    }
  }

  nv_gpu_sampler smap = {
    .filter         = pInfo->filter,
    .mipmap_mode    = pInfo->mipmap_mode,
    .address_mode   = pInfo->address_mode,
    .max_anisotropy = pInfo->max_anisotropy,
    .mip_lod_bias   = pInfo->mip_lod_bias,
    .min_lod        = pInfo->min_lod,
    .max_lod        = pInfo->max_lod,
  };

  VkSamplerCreateInfo samplerInfo = nv_zero_init(VkSamplerCreateInfo);
  samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter           = pInfo->filter;
  samplerInfo.minFilter           = pInfo->filter;
  samplerInfo.mipmapMode          = pInfo->mipmap_mode;
  samplerInfo.addressModeU        = pInfo->address_mode;
  samplerInfo.addressModeV        = pInfo->address_mode;
  samplerInfo.addressModeW        = pInfo->address_mode;
  samplerInfo.anisotropyEnable    = pInfo->max_anisotropy > 1.0f;
  samplerInfo.maxLod              = pInfo->max_lod;
  samplerInfo.minLod              = pInfo->min_lod;
  nvvk_result_check(vkCreateSampler(device, &samplerInfo, NOVA_VK_ALLOCATOR, &smap.vksampler));

  nv_dynarray_push_back(&g_Samplers, &smap);
  (*sampler) = &((nv_gpu_sampler*)g_Samplers.m_data)[g_Samplers.m_size - 1];
}

void
nv_gpu_write_to_texture(nv_gpu_texture* tex, const nv_image_t* src)
{
  if (!(tex->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
  {
    nv_log_error("Cannot write to an image which does not have usage "
                 "VK_IMAGE_USAGE_TRANSFER_DST_BIT");
  }

  const int tex_size = tex->extent.width * tex->extent.height * nv_format_get_bytes_per_pixel(tex->format);
  // if (tex->memory->usage & NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE) {
  //     void *mapped;
  //     nv_gpu_map_memory(tex->memory, tex_size, tex->offset, &mapped);
  //     memcpy(mapped, src->data, tex_size);
  //     nv_gpu_unmap_memory(tex->memory);
  //     return;
  // }

  nv_vk_stage_image_transfer(tex->image, src->data, src->w, src->h, tex_size);
}

VkImage
nv_gpu_texture_get(const nv_gpu_texture* tex)
{
  return tex ? tex->image : NULL;
}

VkImageView
nv_gpu_texture_get_view(const nv_gpu_texture* tex)
{
  return tex ? tex->view : NULL;
}

VkSampler
nv_gpu_sampler_get(const nv_gpu_sampler* sampler)
{
  return sampler ? sampler->vksampler : NULL;
}

void
nv_gpu_create_texture(const nv_gpu_texture_create_info* pInfo, nv_gpu_texture** tex)
{
  (*tex) = nv_calloc(sizeof(nv_gpu_texture));

  nv_format fmt = pInfo->format;
  if (pInfo->usage & VK_IMAGE_USAGE_SAMPLED_BIT) { fmt = nv_vk_get_supported_format_for_draw(fmt); }

  (*tex)->type   = pInfo->type;
  (*tex)->format = pInfo->format;
  (*tex)->extent = (VkExtent3D){ pInfo->extent.width, pInfo->extent.height, pInfo->extent.depth };

  VkImageUsageFlags usage = 0;

  switch (pInfo->usage)
  {
    case NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE: usage |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_COLOR_TEXTURE: usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_DEPTH_TEXTURE: usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_STENCIL_TEXTURE: usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_STORAGE_TEXTURE: usage |= VK_IMAGE_USAGE_STORAGE_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_INPUT_ATTACHMENT: usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_RESOLVE_TEXTURE: usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_TRANSIENT_ATTACHMENT: usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    case NOVA_GPU_TEXTURE_USAGE_PRESENTATION: usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; break;
    default: nv_log_error("Unknown texture usage: %u", pInfo->usage); break;
  }
  (*tex)->usage = usage;

  if (pInfo->usage & NOVA_GPU_TEXTURE_USAGE_PRESENTATION) { return; }

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
  nvvk_result_check(vkCreateImage(device, &imageCreateInfo, NOVA_VK_ALLOCATOR, &(*tex)->image));
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
  int                  w, h;
  int                  rcount;
  nv_format            fmt;
  nv_gpu_texture*      tex;
  nv_gpu_memory_t*     mem;
  nv_gpu_sampler*      sampler;
  nv_descriptor_set_t* set;
};

nv_sprite* nv_sprite_empty = NULL;

nv_sprite*
nv_sprite_load_from_memory(const unsigned char* data, int w, int h, nv_format fmt)
{
  nv_sprite* spr = nv_calloc(sizeof(struct nv_sprite));

  spr->rcount = 1;

  fmt      = nv_vk_get_supported_format_for_draw(fmt);
  spr->fmt = fmt;

  nv_gpu_texture_create_info tex_info = {
    .format      = fmt,
    .samples     = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .type        = VK_IMAGE_TYPE_2D,
    .usage       = NOVA_GPU_TEXTURE_USAGE_SAMPLED_TEXTURE,
    .extent      = (nv_extent3D){ .width = w, .height = h, .depth = 1 },
    .arraylayers = 1,
    .miplevels   = 1,
  };
  nv_gpu_create_texture(&tex_info, &spr->tex);

  VkMemoryRequirements mem_req;
  vkGetImageMemoryRequirements(device, nv_gpu_texture_get(spr->tex), &mem_req);
  nv_gpu_allocate_memory(mem_req.size, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE, &spr->mem);
  nv_gpu_bind_texture_to_memory(spr->mem, 0, spr->tex);

  const nv_image_t img = (const nv_image_t){ .w = w, .h = h, .fmt = fmt, .data = (unsigned char*)data };
  nv_gpu_write_to_texture(spr->tex, &img);

  nv_gpu_sampler_create_info sampler_info = {
    .filter         = VK_FILTER_NEAREST,
    .mipmap_mode    = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .address_mode   = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .max_anisotropy = 1.0f,
    .mip_lod_bias   = 0.0f,
    .min_lod        = 0.0f,
    .max_lod        = VK_LOD_CLAMP_NONE,
  };
  nv_gpu_create_sampler(&sampler_info, &spr->sampler);

  VkDescriptorSetLayoutBinding binding = (VkDescriptorSetLayoutBinding){
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  nv_allocate_descriptor_set(&g_pool, &binding, 1, &spr->set);

  VkDescriptorImageInfo desc_img = {
    .sampler     = nv_gpu_sampler_get(spr->sampler),
    .imageView   = nv_sprite_get_vk_image_view(spr),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = spr->set->set,
    .dstBinding      = 0,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &desc_img,
  };
  nv_descriptor_set_submit_write(spr->set, &write);

  return spr;
}

nv_sprite*
nv_sprite_load_from_disk(const char* path)
{
  nv_image_t tex = nv_image_load(path);
  nv_sprite* spr = nv_sprite_load_from_memory(tex.data, tex.w, tex.h, tex.fmt);
  spr->rcount    = 1;
  nv_free(tex.data);
  return spr;
}

void
nv_sprite_destroy(nv_sprite* spr)
{
  nv_gpu_destroy_texture(spr->tex);
  nv_gpu_free_memory(spr->mem);
}

void
nv_sprite_lock(nv_sprite* spr)
{
  spr->rcount++;
}

void
nv_sprite_release(nv_sprite* spr)
{
  spr->rcount--;
  if (spr->rcount <= 0) { nv_sprite_destroy(spr); }
}

void
nv_sprite_get_dimensions(const nv_sprite* spr, int* w, int* h)
{
  if (w) { *w = spr->w; }
  if (h) { *h = spr->h; }
}

VkImage
nv_sprite_get_vk_image(const nv_sprite* spr)
{
  return nv_gpu_texture_get(spr->tex);
}

VkImageView
nv_sprite_get_vk_image_view(const nv_sprite* spr)
{
  return nv_gpu_texture_get_view(spr->tex);
}

VkDescriptorSet
nv_sprite_get_descriptor_set(const nv_sprite* spr)
{
  return spr->set->set;
}

VkSampler
nv_sprite_get_sampler(const nv_sprite* spr)
{
  return nv_gpu_sampler_get(spr->sampler);
}

nv_format
nv_sprite_get_format(const nv_sprite* spr)
{
  return spr->fmt;
}
// SPRITE

void
nv_camera_destroy(nv_camera_t* cam)
{
  // nv_descriptor_set_t_destroy(cam->sets);
  nv_gpu_destroy_buffer(&cam->ub);
  nv_gpu_free_memory(cam->mem);
}

void
nv_camera_init(nv_camera_t* cam)
{
  const flt_t ortho_w = 10.0, ortho_h = 10.0;
  *cam = (nv_camera_t){
    .perspective = {},
    .ortho_size  = (vec2){ ortho_w, ortho_h },
    .ortho       = m4ortho(-ortho_w, ortho_w, -ortho_h, ortho_h, 0.1f, 100.0f),
    .position    = (vec3){ 0.0f, 0.0f, 10.0f },
    .actual_pos  = (vec3){ 0.0f, 0.0f, 10.0f },
    .front       = (vec3){ 0.0f, 0.0f, 1.0f },
    .up          = (vec3){ 0.0f, 1.0f, 0.0f },
    .right       = (vec3){ 1.0f, 0.0f, 0.0f },

    // These angles should not be in radians because they're converted at update() time.
    .yaw   = -90.0,
    .pitch = 0.0,
    .fov   = 90.0f,

    .near_clip = 0.1f,
    .far_clip  = 1000.0f,
  };

  VkPhysicalDeviceProperties phys_device_properties;
  vkGetPhysicalDeviceProperties(phys_device, &phys_device_properties);

  int ub_align = phys_device_properties.limits.minUniformBufferOffsetAlignment;

  // the size of a single uniform buffer.
  int ub_size = ALIGN_UP(sizeof(nv_camera_uniform_buffer), ub_align);

  nv_gpu_create_buffer(ub_size * CAMERA_FAKE_BUFFER_COUNT, ub_align, NOVA_GPU_BUFFER_TYPE_UNIFORM_BUFFER, &cam->ub);
  nv_gpu_allocate_memory(ub_size * CAMERA_FAKE_BUFFER_COUNT, NOVA_GPU_MEMORY_USAGE_CPU_VISIBLE | NOVA_GPU_MEMORY_USAGE_CPU_WRITEABLE, &cam->mem);
  nv_gpu_bind_buffer_to_memory(cam->mem, 0, &cam->ub);

  VkDescriptorSetLayoutBinding bindings[] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL } };
  nv_allocate_descriptor_set(&g_pool, bindings, 1, &cam->sets);

  VkDescriptorBufferInfo bufferinfo = { .buffer = cam->ub.buffer, .offset = 0, .range = (VkDeviceSize)ub_size };
  VkWriteDescriptorSet   write      = {
           .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
           .dstSet          = cam->sets->set,
           .dstBinding      = 0,
           .descriptorCount = 1,
           .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
           .pBufferInfo     = &bufferinfo,
  };
  nv_descriptor_set_submit_write(cam->sets, &write);
  nv_gpu_map_memory(cam->mem, VK_WHOLE_SIZE, 0, (void**)&cam->mem_mapped);
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
  cam->yaw += yaw_;
  cam->pitch -= pitch_;

  cam->yaw = fmodf(cam->yaw, 360.0f);

  const flt_t bound = 89.9f;
  cam->pitch        = NVM_CLAMP(cam->pitch, -bound, bound);
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
  const flt_t yaw_rads = NVM_DEG2RAD(cam->yaw), pitch_rads = NVM_DEG2RAD(cam->pitch);
  const flt_t cospitch = cosf(pitch_rads);
  vec3        new_front;
  new_front.x = cosf(yaw_rads) * cospitch;
  new_front.y = sinf(pitch_rads);
  new_front.z = sinf(yaw_rads) * cospitch;

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
nv_camera_get_global_mouse_position(const nv_camera_t* cam)
{
  vec2 ortho_pos = v2mulv(nv_input_get_mouse_position(), cam->ortho_size);
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