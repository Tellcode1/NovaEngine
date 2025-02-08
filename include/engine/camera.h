#ifndef __NOVA_CAMERA_H__
#define __NOVA_CAMERA_H__

// implementation: vk.c

#include "../../std/math/mat.h"
#include "../../std/math/vec2.h"
#include "../../std/math/vec3.h"

#include "../GPU/buffer.h"
#include "../engine/input.h"
#include "renderer.h"

NOVA_HEADER_START;

typedef struct nv_camera_uniform_buffer nv_camera_uniform_buffer;
typedef struct nv_camera_t              nv_camera_t;
typedef struct nv_descriptor_set_t      nv_descriptor_set_t;

#define CAMERA_FAKE_BUFFER_COUNT 3

#define ALIGN_UP(sz, align) ((sz + align - 1) & ~(align - 1))

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
  float yaw;
  float pitch;

  float fov;
  float near_clip;
  float far_clip;

  nv_gpu_buffer_t           ub;
  nv_gpu_memory_t*          mem;
  nv_descriptor_set_t*      sets;
  nv_camera_uniform_buffer* mem_mapped;

  // nv_gpu_texture *render_texture;
  // VkFramebuffer framebuffer;
  // VkRenderPass render_pass;
};

extern void        nv_camera_destroy(nv_camera_t* cam);
extern nv_camera_t nv_camera_init();
extern mat4        nv_camera_get_projection(nv_camera_t* cam);
extern mat4        nv_camera_get_view(nv_camera_t* cam);
extern vec3        nv_camera_get_up_vector(nv_camera_t* cam);
extern vec3        nv_camera_get_front_vector(nv_camera_t* cam);
extern void        nv_camera_rotate(nv_camera_t* cam, float yaw_, float pitch_);
extern void        nv_camera_move(nv_camera_t* cam, const vec3 amt);
extern void        nv_camera_set_position(nv_camera_t* cam, const vec3 pos);
extern void        nv_camera_update(nv_camera_t* cam, struct nv_renderer_t* rd);
extern vec2        nv_camera_get_global_mouse_position(const nv_camera_t* cam);

NOVA_HEADER_END;

#endif //__NOVA_CAMERA_H__