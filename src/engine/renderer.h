#ifndef __LUNA_RENDERER_H__
#define __LUNA_RENDERER_H__

// implementation: vk.c

// just keeping this here for reference
// god tier article btw
// https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/

// I strive for a world where I do not have to call vulkan functions myself again

#include "engine.h"
#include "nvsm.h"
#include "sprite.h"

#include "../std/math/vec2.h"
#include "../std/math/vec3.h"
#include "../std/math/vec4.h"

#include "../GPU/buffer.h"
#include "../GPU/descriptors.h"
#include "../GPU/fbf.h"
#include "../GPU/texture.h"
#include "../GPU/types.h"
#include "../GPU/vk.h"

#include "../containers/list.h"

NOVA_HEADER_START

typedef struct nv_ctext_module     nv_ctext_module;
typedef struct nv_quad_draw_call_t nv_quad_draw_call_t;
typedef struct nv_line_draw_call_t nv_line_draw_call_t;
typedef struct nv_draw_call_t      nv_draw_call_t;
typedef struct nv_renderer_t       nv_renderer_t;

struct nv_ctx_t;
struct nvsm_ctx_t;

extern nv_descriptor_pool_t g_pool;
extern struct nv_camera_t   camera;

// Move ownership to camera VV
typedef struct nv_renderer_frame_render_info
{
  nv_gpu_texture       sc_image; // sc -> swapchain owned
  nv_gpu_texture       depth_image;
  nv_gpu_framebuffer_t color_framebuffer;
  VkSemaphore          image_available_semaphore;
  VkSemaphore          render_finish_semaphore;
  VkFence              in_flight_fence;
} nv_renderer_frame_render_info;

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
  nv_list_t            labels;
  nv_descriptor_set_t* desc_set;
  unsigned             flags;
};

struct nv_quad_draw_call_t
{
  nv_sprite_t* spr;
  vec3f        siz, pos;
  vec2f        tex_multiplier;
  vec4f        col;
};

struct nv_line_draw_call_t
{
  vec2f begin, end;
  vec4f col;
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
  union nv_DrawCallData
  {
    nv_line_draw_call_t line;
    nv_quad_draw_call_t quad;
  } drawcall;
};

struct nv_renderer_t
{
  nv_ctx_t*   ctx;
  nvvk_ctx_t* nvvkctx;

  unsigned       flags;
  nv_buffer_mode buffer_mode;

  nv_sprite_t sprite_empty;

  nv_sample_count samples;

  VkRenderPass render_pass;
  nv_extent2d  render_extent;

  VkSwapchainKHR swapchain;
  VkCommandPool  command_pool;

  u32 attachment_count;
  u32 frame;
  u32 image_index;

  size_t shadow_image_size; // the size of ONE depth texture. Multiply by
                            // SwapchainImageCount to get total size
  nv_gpu_memory_t depth_image_memory;

  nv_gpu_texture  color_image;
  nv_gpu_memory_t color_image_memory;

  VkFormat depth_buffer_format;

  nv_list_t render_data;
  nv_list_t draw_cmd_buffers;

  /* stored to avoid creating one for literally every texture. nv_gpu_sampler** */
  nv_list_t samplers;

  nv_list_t drawcalls;

  nv_ctext_module* ctext;

  // These are used to render all the sprites in the game (quad based sprites
  // that is)
  nv_gpu_buffer_t quad_vb;
  nv_gpu_memory_t quad_memory;

  void* mapped;
};

extern nv_errorc nv_renderer_init(struct nv_ctx_t* ctx, nvvk_ctx_t* nvvkctx, struct nvsm_ctx_t* nvsmctx, const nv_renderer_config* conf, nv_renderer_t* dst);
extern void      nv_renderer_destroy(nv_renderer_t* rd);

extern bool      nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color);
extern nv_errorc nv_renderer_end(nv_renderer_t* rd);

extern u32                nv_renderer_get_frame(const nv_renderer_t* rd);
extern u32                nv_renderer_get_max_frames_in_flight(const nv_renderer_t* rd);
extern VkCommandBuffer    nv_renderer_get_draw_buffer(const nv_renderer_t* rd);
extern VkRenderPass       nv_renderer_get_render_pass(const nv_renderer_t* rd);
extern struct nv_extent2d nv_renderer_get_render_extent(const nv_renderer_t* rd);

extern void nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite_t* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer);
extern void nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer);

extern nv_extent2d nv_get_window_size(nv_ctx_t* ctx);

NOVA_HEADER_END

#endif //__LUNA_RENDERER_H__
