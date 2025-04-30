#ifndef __NOVA_CAMERA_H__
#define __NOVA_CAMERA_H__

// implementation: vk.c

#include "../std/errorcodes.h"
#include "../std/math/mat.h"
#include "../std/math/vec2.h"
#include "../std/math/vec3.h"

#include "../GPU/buffer.h"
#include "renderer.h"

NOVA_HEADER_START

typedef struct nv_camera_uniform_buffer nv_camera_uniform_buffer;
typedef struct nv_camera_t              nv_camera_t;

struct nv_input_ctx_t;

#define CAMERA_FAKE_BUFFER_COUNT 3

static inline vk_size_t
_align_up_size(vk_size_t sz, vk_size_t align)
{
  /* The previous implementation was technically */
  /* next power of two, so it was causing errors. This is the closest next power of two. */
  if ((sz % align) != 0)
  {
    sz += align - sz % align;
  }
  nv_assert_else_return((sz % align) == 0, 0);
  return sz;
}

static inline void*
_align_up_ptr(void* ptr, vk_size_t align)
{
  /* The previous implementation was technically */
  /* next power of two, so it was causing errors. This is the closest next power of two. */
  if (((uintptr_t)ptr % align) != 0)
  {
    ptr = (void*)((uintptr_t)ptr + (align - ((uintptr_t)ptr) % align));
  }
  nv_assert_else_return(((uintptr_t)ptr % align) == 0, NULL);
  return ptr;
}

struct nv_camera_uniform_buffer
{
  mat4f perspective;
  mat4f ortho;
  mat4f view;
};

struct nv_camera_t
{
  // If you draw a quad with this width, it'll cover the whole screen
  // oh, and this should technically be HALVED when you're rendering quads as they generally take HALF size
  // that's just to say this is the FULL width along each direction.
  vec2 ortho_size;

  // TODO: move to uniform buffer with data like current app time, delta time, etc.
  // To be honest i dont know what delta time is supposed to be
  // doing on the GPU except for particle simulations but you ought to
  // use something like push constants for that. Not my fault ;D
  mat4 perspective;
  mat4 ortho;
  mat4 view;

  // This reduces "choppiness" created by moving the camera if the camera has moved after transferring to the uniform buffer
  // position is the position occupied by the camera when it was sent to the uniform buffer
  // actual pos is the real time position of the camera.
  vec3  position;
  vec3  actual_pos;
  vec3  front;
  vec3  up;
  vec3  right;
  flt_t yaw_degrees;
  flt_t pitch_degrees;

  flt_t fov;
  flt_t near_clip;
  flt_t far_clip;

  nv_gpu_buffer_t           ub;
  nv_gpu_memory_t           mem;
  nv_descriptor_set_t*      sets;
  nv_camera_uniform_buffer* mem_mapped;

  // nv_gpu_texture *render_texture;
  // VkFramebuffer framebuffer;
  // VkRenderPass render_pass;
};

extern void     nv_camera_destroy(nvvk_ctx_t* nvvkctx, nv_camera_t* cam);
extern nv_error nv_camera_init(nvvk_driver_t* driver, nv_camera_t* cam);
extern mat4     nv_camera_get_projection(nv_camera_t* cam);
extern mat4     nv_camera_get_view(nv_camera_t* cam);
extern vec3     nv_camera_get_up_vector(nv_camera_t* cam);
extern vec3     nv_camera_get_front_vector(nv_camera_t* cam);
extern void     nv_camera_rotate(nv_camera_t* cam, flt_t yaw_, flt_t pitch_);
extern void     nv_camera_move(nv_camera_t* cam, vec3 amt);
extern void     nv_camera_set_position(nv_camera_t* cam, vec3 pos);
extern void     nv_camera_update(nv_camera_t* cam, struct nv_renderer_t* rd);
extern vec2     nv_camera_get_global_mouse_position(struct nv_input_ctx_t* inputctx, const nv_camera_t* cam);

NOVA_HEADER_END

#endif //__NOVA_CAMERA_H__
