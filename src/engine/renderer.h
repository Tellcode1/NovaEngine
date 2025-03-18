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

NOVA_HEADER_START

NVVK_FORWARD_DECLARE(VkFramebuffer)
NVVK_FORWARD_DECLARE(VkSemaphore)
NVVK_FORWARD_DECLARE(VkFence)

typedef struct nv_gpu_texture nv_gpu_texture;

extern nv_descriptor_pool_t g_pool;
extern struct nv_camera_t   camera;

typedef enum nv_window_flag_bits
{
  NOVA_WINDOW_VSYNC         = 1 << 0,
  NOVA_WINDOW_RESIZABLE     = 1 << 1,
  NOVA_WINDOW_BORDERLESS    = 1 << 2,
  NOVA_WINDOW_FULLSCREEN    = 1 << 3,
  NOVA_WINDOW_MAXIMIZED     = 1 << 4,
  NOVA_WINDOW_MINIMIZED     = 1 << 5,
  NOVA_WINDOW_HIDDEN        = 1 << 6,
  NOVA_WINDOW_HIGH_DPI      = 1 << 7,
  NOVA_WINDOW_ALWAYS_ON_TOP = 1 << 8,
} nv_window_flag_bits;

typedef enum nv_window_v_sync_bits
{
  NOVA_WINDOW_VSYNC_DISABLED = 0,
  NOVA_WINDOW_VSYNC_ENABLED  = 1
} nv_window_v_sync_bits;
typedef bool nv_window_vsync;

typedef enum nv_buffer_mode_bits
{
  NOVA_BUFFER_MODE_SINGLE_BUFFERED = 0,
  NOVA_BUFFER_MODE_DOUBLE_BUFFERED = 1,
  NOVA_BUFFER_MODE_TRIPLE_BUFFERED = 2,
} nv_buffer_mode_bits;
typedef unsigned nv_buffer_mode;

typedef enum nv_sample_count_bits
{
  NOVA_SAMPLE_COUNT_MAX_SUPPORTED    = 0x7FFFFFFF,
  NOVA_SAMPLE_COUNT_NO_EXTRA_SAMPLES = 1,
  NOVA_SAMPLE_COUNT_1_SAMPLES        = 1,
  NOVA_SAMPLE_COUNT_2_SAMPLES        = 2,
  NOVA_SAMPLE_COUNT_4_SAMPLES        = 4,
  NOVA_SAMPLE_COUNT_8_SAMPLES        = 8,
  NOVA_SAMPLE_COUNT_16_SAMPLES       = 16,
  NOVA_SAMPLE_COUNT_32_SAMPLES       = 32,
} nv_sample_count_bits;
typedef unsigned nv_sample_count;

typedef struct nv_extent2d
{
  size_t m_width, m_height;
} nv_extent2d;

typedef struct nv_extent3D
{
  size_t m_width, m_height, m_depth;
} nv_extent3D;

// Move ownership to camera VV
typedef struct nv_renderer_frame_render_info
{
  nv_gpu_texture* m_sc_image; // sc -> swapchain owned
  nv_gpu_texture* m_depth_image;
  VkFramebuffer   m_color_framebuffer;
  VkSemaphore     m_image_available_semaphore;
  VkSemaphore     m_render_finish_semaphore;
  VkFence         m_in_flight_fence;
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

typedef struct nv_renderer_t nv_renderer_t;

extern nv_renderer_t* nv_renderer_init(const nv_renderer_config* conf);
extern void           nv_renderer_destroy(struct nv_renderer_t* rd);

extern bool nv_renderer_begin(struct nv_renderer_t* rd);
extern void nv_renderer_end(struct nv_renderer_t* rd);

extern void nv_renderer_set_clear_color(struct nv_renderer_t* rd, vec4 col);

extern u32                       nv_renderer_get_frame(const struct nv_renderer_t* rd);
extern u32                       nv_renderer_get_max_frames_in_flight(const struct nv_renderer_t* rd);
extern struct VkCommandBuffer_T* nv_renderer_get_draw_buffer(const nv_renderer_t* rd);
extern struct VkRenderPass_T*    nv_renderer_get_render_pass(const nv_renderer_t* rd);
extern struct nv_extent2d        nv_renderer_get_render_extent(const nv_renderer_t* rd);

extern void nv_renderer_render_quad(nv_renderer_t* rd, nv_sprite* spr, vec2f tex_coord_multiplier, vec3f position, vec3f size, vec4f color, int layer);
extern void nv_renderer_render_line(nv_renderer_t* rd, vec2f start, vec2f end, vec4f color, int layer);

extern nv_extent2d nv_get_window_size(void);

NOVA_HEADER_END

#endif //__LUNA_RENDERER_H__
