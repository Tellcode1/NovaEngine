
#ifndef NOVA_RENDERER_H
#define NOVA_RENDERER_H

// just keeping this here for reference
//  god tier article btw
//  https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/

// I strive for a world where I do not have to call vulkan functions myself again

#include "engine.h"
#include "format.h"
#include "sprite.h"

#include "../std/include/errorcodes.h"
#include "../std/include/math/vec2.h"
#include "../std/include/math/vec3.h"
#include "../std/include/math/vec4.h"
#include "../std/include/stdafx.h"
#include "../std/include/types.h"

#include "../iris/buffer.h"
#include "../iris/descriptors.h"
#include "../iris/driver.h"
#include "../iris/framebuffer.h"
#include "../iris/memory.h"
#include "../iris/texture.h"
#include "../iris/types.h"

#include "../std/include/containers/list.h"

#include "../../external/volk/volk.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_ctext_module     nv_ctext_module;
  typedef struct nv_quad_draw_call_t nv_quad_draw_call_t;
  typedef struct nv_line_draw_call_t nv_line_draw_call_t;
  typedef struct nv_draw_call_t      nv_draw_call_t;
  typedef struct nv_renderer         nv_renderer_t;

  struct nvsm_ctx;
  struct nv_ctx;
  struct iris_driver;

  extern nv_descriptor_pool_t g_pool;
  extern struct nv_camera     camera;

  // Move ownership to camera VV
  typedef struct nv_renderer_frame_render_info
  {
    VkImage            swapchain_image;
    VkImageView        swapchain_image_view;
    iris_texture_t     depth_image;
    iris_framebuffer_t color_framebuffer;
  } nv_renderer_frame_render_info;

  typedef struct nv_rdr_per_image_data
  {
    VkCommandBuffer cmd;
    VkFence         image_in_flight; // Set to nv_rdr_per_frame_data::in_flight_fence to signify its in use.
  } nv_rdr_per_image_data_t;

  typedef struct nv_rdr_per_frame_data
  {
    VkSemaphore render_finish_semaphore;
    VkSemaphore image_available_semaphore;
    VkFence     in_flight_fence;
  } nv_rdr_per_frame_data_t;

  typedef enum nv_renderer_flag_bits
  {
    NOVA_RENDERER_MULTISAMPLING_ENABLE = 1 << 0,
    NOVA_RENDERER_VSYNC_ENABLE         = 1 << 1,
    NOVA_RENDERER_WINDOW_RESIZABLE     = 1 << 2,
  } nv_renderer_flag_bits;

  typedef struct nv_renderer_config
  {
    nv_sample_count samples;
    nv_buffer_mode  buffer_mode;
    nv_extent2d     initial_window_size;
    int             exit_key;
    bool            multisampling_enable;
    bool            window_resizable;
    nv_window_vsync vsync_enabled;
  } nv_renderer_config;

  static inline nv_renderer_config
  nv_renderer_config_init(void)
  {
    return (nv_renderer_config){
      .samples              = NOVA_SAMPLE_COUNT_NO_EXTRA_SAMPLES,
      .buffer_mode          = NOVA_BUFFER_MODE_DOUBLE_BUFFERED,
      .initial_window_size  = { 800, 600 },
      .multisampling_enable = false,
      .window_resizable     = false,
      .vsync_enabled        = true,
    };
  }

  struct nv_ctext_module
  {
    nv_list_t            fonts;
    nv_descriptor_set_t* desc_set;
    unsigned             flags;
  };

  struct nv_quad_draw_call_t
  {
    nv_sprite_t* spr;
    vec3         siz, pos;
    vec2         tex_multiplier;
    vec4         color;
  };

  struct nv_line_draw_call_t
  {
    vec3 begin, end;
    vec4 color;
  };

  typedef enum nv_draw_call_type
  {
    NOVA_DRAWCALL_QUAD    = 0,
    NOVA_DRAWCALL_LINE    = 1,
    NOVA_DRAWCALL_INVALID = 0x7fffffff
  } nv_draw_call_type;

  struct nv_draw_call_t
  {
    nv_draw_call_type type;
    int               layer;
    union nv_draw_call_data
    {
      nv_line_draw_call_t line;
      nv_quad_draw_call_t quad;
    } drawcall;
  };

  struct nv_swapchain
  {
    VkSwapchainKHR handle;
    nv_format      image_format;
    u32            color_space;
    u32            image_count;
  };

  struct nv_main_pass
  {
    /**
     * the size of ONE depth texture. Multiply by
     * SwapchainImageCount to get total size
     */
    size_t shadow_image_size;

    iris_texture_t color_image;

    VkFormat depth_buffer_format;

    nv_list_t render_data;
    nv_list_t per_frame_data;
    nv_list_t per_image_data;
  };

  typedef struct nv_renderer
  {
    nv_ctx_t*      ctx;
    nvvk_ctx_t*    vkctx;
    iris_driver_t* driver;

    struct nv_swapchain swapchain;

    struct nv_main_pass main_pass;

    unsigned       flags;
    nv_buffer_mode buffer_mode;

    nv_sprite_t sprite_empty;

    bool will_render_this_frame;

    nv_sample_count samples;

    VkRenderPass render_pass;
    nv_extent2d  render_extent;

    VkCommandPool command_pool;

    u32 frame_index;
    u32 image_index;

    nv_list_t drawcalls;

    nv_ctext_module* ctext;

    // These are used to render all the sprites in the game (quad based sprites
    // that is)
    iris_buffer_t quad_vb;

    void* mapped;
  } nv_renderer;

  extern nv_error nv_renderer_init(struct nv_ctx* ctx, struct nvsm_ctx* nvsmctx, iris_driver_t* driver, const nv_renderer_config* conf, nv_renderer_t* dst);
  extern void     nv_renderer_destroy(nv_renderer_t* rd);

  extern bool     nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color);
  extern nv_error nv_renderer_end(nv_renderer_t* rd);

  extern u32             nv_renderer_get_frame(const nv_renderer_t* rd);
  extern u32             nv_renderer_get_frames_in_flight(const nv_renderer_t* rd);
  extern VkCommandBuffer nv_renderer_get_draw_buffer(const nv_renderer_t* rd);
  extern VkRenderPass    nv_renderer_get_render_pass(const nv_renderer_t* rd);
  extern nv_extent2d     nv_renderer_get_render_extent(const nv_renderer_t* rd);

  extern void nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite_t* spr, vec2 tex_coord_multiplier, vec3 position, vec3 size, vec4 color, int layer);
  extern void nv_renderer_render_line(nv_renderer_t* rd, vec3 start, vec3 end, vec4 color, int layer);

  extern nv_extent2d nv_get_window_size(nv_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif // LUNA_RENDERER_H
