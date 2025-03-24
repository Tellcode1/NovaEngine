#ifndef __LUNA_RENDERER_H__
#define __LUNA_RENDERER_H__

// implementation: vk.c

// just keeping this here for reference
// god tier article btw
// https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/

// I strive for a world where I do not have to call vulkan functions myself again

#include "../GPU/descriptors.h"
#include "sprite.h"

#include "../std/math/vec2.h"
#include "../std/math/vec3.h"
#include "../std/math/vec4.h"

#include "../GPU/buffer.h"
#include "../GPU/fbf.h"
#include "../GPU/texture.h"
#include "../GPU/types.h"

NOVA_HEADER_START

typedef struct nv_ctext_module     nv_ctext_module;
typedef struct nv_quad_draw_call_t nv_quad_draw_call_t;
typedef struct nv_line_draw_call_t nv_line_draw_call_t;
typedef struct nv_draw_call_t      nv_draw_call_t;
typedef struct nv_renderer_t       nv_renderer_t;

extern nv_descriptor_pool_t g_pool;
extern struct nv_camera_t   camera;

// Move ownership to camera VV
typedef struct nv_renderer_frame_render_info
{
  nv_gpu_texture       m_sc_image; // sc -> swapchain owned
  nv_gpu_texture       m_depth_image;
  nv_gpu_framebuffer_t m_color_framebuffer;
  VkSemaphore          m_image_available_semaphore;
  VkSemaphore          m_render_finish_semaphore;
  VkFence              m_in_flight_fence;
} nv_renderer_frame_render_info;

typedef enum nv_renderer_flag_bits
{
  NOVA_RENDERER_MULTISAMPLING_ENABLE = 1 << 0,
  NOVA_RENDERER_VSYNC_ENABLE         = 1 << 1,
  NOVA_RENDERER_WINDOW_RESIZABLE     = 1 << 2,
} nv_renderer_flag_bits;

typedef struct nv_renderer_config
{
  nv_sample_count m_samples;
  nv_buffer_mode  m_buffer_mode;
  nv_extent2d     m_initial_window_size;
  int             m_exit_key;
  bool            m_multisampling_enable;
  bool            m_window_resizable;
  nv_window_vsync m_vsync_enabled;
} nv_renderer_config;

static inline nv_renderer_config
nv_renderer_config_init(void)
{
  return (nv_renderer_config){
    .m_samples              = NOVA_SAMPLE_COUNT_NO_EXTRA_SAMPLES,
    .m_buffer_mode          = NOVA_BUFFER_MODE_DOUBLE_BUFFERED,
    .m_initial_window_size  = { 800, 600 },
    .m_multisampling_enable = false,
    .m_window_resizable     = false,
    .m_vsync_enabled        = true,
  };
}

struct nv_ctext_module
{
  nv_list_t            m_fonts;
  nv_list_t            m_labels;
  nv_descriptor_set_t* m_desc_set;
  unsigned             m_flags;
};

struct nv_quad_draw_call_t
{
  nv_sprite* m_spr;
  vec3f      m_siz, m_pos;
  vec2f      m_tex_multiplier;
  vec4f      m_col;
};

struct nv_line_draw_call_t
{
  vec2f m_begin, m_end;
  vec4f m_col;
};

typedef enum nv_draw_call_type
{
  NOVA_DRAWCALL_QUAD    = 0,
  NOVA_DRAWCALL_LINE    = 1,
  NOVA_DRAWCALL_INVALID = 0x7fffffff
} nv_draw_call_type;

struct nv_draw_call_t
{
  nv_draw_call_type m_type;
  int               m_layer;
  union nv_DrawCallData
  {
    nv_line_draw_call_t m_line;
    nv_quad_draw_call_t m_quad;
  } m_drawcall;
};

struct nv_renderer_t
{
  unsigned       m_flags;
  nv_buffer_mode m_buffer_mode;

  VkRenderPass m_render_pass;
  nv_extent2d  m_render_extent;

  VkSwapchainKHR m_swapchain;
  VkCommandPool  m_command_pool;

  u32 m_attachment_count;
  u32 m_frame;
  u32 m_image_index;

  size_t m_shadow_image_size; // the size of ONE depth texture. Multiply by
                              // SwapchainImageCount to get total size
  nv_gpu_memory_t m_depth_image_memory;

  nv_gpu_texture  m_color_image;
  nv_gpu_memory_t m_color_image_memory;

  VkFormat m_depth_buffer_format;

  nv_list_t m_render_data;
  nv_list_t m_draw_cmd_buffers;

  /* stored to avoid creating one for literally every texture. nv_gpu_sampler** */
  nv_list_t m_samplers;

  nv_list_t m_drawcalls;

  nv_ctext_module* m_ctext;

  // These are used to render all the sprites in the game (quad based sprites
  // that is)
  nv_gpu_buffer_t m_quad_vb;
  nv_gpu_memory_t m_quad_memory;

  void* m_mapped;
};

extern int  nv_renderer_init(const nv_renderer_config* conf, nv_renderer_t* dst);
extern void nv_renderer_destroy(nv_renderer_t* rd);

extern bool nv_renderer_begin(nv_renderer_t* rd, vec4 clear_color);
extern void nv_renderer_end(nv_renderer_t* rd);

extern u32                       nv_renderer_get_frame(const nv_renderer_t* rd);
extern u32                       nv_renderer_get_max_frames_in_flight(const nv_renderer_t* rd);
extern struct VkCommandBuffer_T* nv_renderer_get_draw_buffer(const nv_renderer_t* rd);
extern struct VkRenderPass_T*    nv_renderer_get_render_pass(const nv_renderer_t* rd);
extern struct nv_extent2d        nv_renderer_get_render_extent(const nv_renderer_t* rd);

extern void nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer);
extern void nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer);

extern nv_extent2d nv_get_window_size(void);

NOVA_HEADER_END

#endif //__LUNA_RENDERER_H__
