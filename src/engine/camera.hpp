#ifndef __NOVA_CAMERA_H__
#define __NOVA_CAMERA_H__

// implementation: vk.c

#include "../std/errorcodes.h"
#include "../std/math/mat.h"
#include "../std/math/vec2.h"
#include "../std/math/vec3.h"

#include "../GPU/buffer.hpp"
#include "renderer.hpp"

NOVA_HEADER_START

typedef struct nv_camera_uniform_buffer nv_camera_uniform_buffer_t;
typedef struct nv_camera                nv_camera_t;

struct nv_input_ctx_t;

#define CAMERA_FAKE_BUFFER_COUNT 3

/**
 * The global up vector.
 * If set to (0,-1,0) Then moving -1 units on the y axis moves you up
 */
#define NOVA_CAMERA_WORLD_UP (vec3){ 0.0F, 1.0F, 0.0F }

/**
 * The default projection type
 */
typedef enum nv_camera_projection_type
{
  NV_CAMERA_PROJECTION_PERSPECTIVE  = 0,
  NV_CAMERA_PROJECTION_ORTHOGRAPHIC = 1,
} nv_camera_projection_type;

struct nv_camera_uniform_buffer
{
  mat4f perspective;
  mat4f ortho;
  mat4f view;

  vec3f camera_position;
  vec3f camera_front;
  vec3f camera_right;
  // camera_up = cross(right, front)

  // <fov, near plane, far plane>
  vec3f clip_plane;

  /**
   * The render extent in pixels
   */
  vec2u render_extent;

  /**
   * The swapchain image index that we're rendering to
   */
  u32 image_index;
};

struct nv_camera
{
  /**
   * If you draw a quad with this width, it'll cover the whole screen
   * oh, and this should technically be HALVED when you're rendering quads as they generally take HALF size
   * that's just to say this is the FULL width along each direction.
   */
  vec2 ortho_size;

  /**
   * TODO: move to uniform buffer with data like current app time, delta time, etc.
   * To be honest i dont know what delta time is supposed to be doing on the GPU except for particle simulations but you ought to
   * use something like push constants for that. Not my fault ;D
   */
  mat4 perspective;
  mat4 ortho;
  mat4 view;

  /**
   * This reduces "choppiness" created by moving the camera if the camera has moved after transferring to the uniform buffer (which is nearly always)
   * position is the position occupied by the camera when it was sent to the uniform buffer
   * actual pos is the real time (modifiable) position of the camera.
   */
  vec3 position;
  vec3 actual_pos;

  /**
   * we just calculate everythinng else at runtime
   */
  vec3 front;

  /**
   * TODO: incorporate quaternions :>
   */
  flt_t yaw_degrees;
  flt_t pitch_degrees;

  flt_t fov;
  flt_t near_clip;
  flt_t far_clip;

  nv_gpu_buffer_t             ub;
  nv_gpu_memory_block_t       mem;
  nv_descriptor_set_t*        sets;
  nv_camera_uniform_buffer_t* mem_mapped;

  size_t offset_index;

  // nv_gpu_texture *render_texture;
  // VkFramebuffer framebuffer;
  // VkRenderPass render_pass;
};

extern void     nv_camera_destroy(nvvk_ctx_t* vkctx, nv_camera_t* cam);
extern nv_error nv_camera_init(nvvk_driver* driver, nv_camera_t* cam);
extern mat4     nv_camera_get_projection(nv_camera_t* cam);
extern mat4     nv_camera_get_view(nv_camera_t* cam);

extern vec3 nv_camera_get_up(nv_camera_t* cam);
extern vec3 nv_camera_get_front(nv_camera_t* cam);
extern vec3 nv_camera_get_right(nv_camera_t* cam);

extern void nv_camera_rotate(nv_camera_t* cam, flt_t yaw_, flt_t pitch_);
extern void nv_camera_move(nv_camera_t* cam, vec3 amt);
extern void nv_camera_set_position(nv_camera_t* cam, vec3 pos);
extern void nv_camera_update(nv_camera_t* cam, struct nv_renderer* rd);
extern vec2 nv_camera_get_global_mouse_position(struct nv_input_ctx_t* inputctx, const nv_camera_t* cam);

extern size_t nv_camera_get_read_offset(const nv_camera_t* cam);
extern size_t nv_camera_get_write_offset(const nv_camera_t* cam);

NOVA_HEADER_END

#endif //__NOVA_CAMERA_H__
