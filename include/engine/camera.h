#ifndef NOVA_CAMERA_H
#define NOVA_CAMERA_H

#include "../std/include/error.h"
#include "../std/include/math/mat.h"
#include "../std/include/math/vec2.h"
#include "../std/include/math/vec3.h"
#include "../std/include/types.h"

#include "../iris/descriptors.h"
#include "../iris/driver.h"
#include "../iris/extent.h"
#include "../iris/ringbuffer.h"
#include "renderer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

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
    mat4f perspective; // 64 bytes
    mat4f ortho;       // 64 bytes
    mat4f view;        // 64 bytes

    // camera_up = cross(right, front)
    vec4f camera_position; // vec3 + padding
    vec4f camera_front;    // vec3 + padding
    vec4f camera_right;    // vec3 + padding

    // <fov, near plane, far plane>
    vec4f clip_plane;

    /**
     * The render extent in pixels
     */
    vec2u render_extent;

    /**
     * The swapchain image index that we're rendering to
     */
    u32 image_index;

    u32 _padding;
  };

  struct nv_camera
  {
    /**
     * TODO: move to uniform buffer with data like current app time, delta time, etc.
     * To be honest i dont know what delta time is supposed to be doing on the GPU except for particle simulations but you ought to
     * use something like push constants for that. Not my fault ;D
     */
    mat4 perspective;
    mat4 ortho;
    mat4 view;

    /**
     * The half size of the camera's orthographic view box, along each direction.
     */
    vec2 ortho_half_size;

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
     * I would rather die tbh
     */
    double yaw_degrees;
    double pitch_degrees;

    double fov;
    double near_clip;
    double far_clip;

    iris_ring_buffer_t   uniform_buffer;
    nv_descriptor_set_t* descriptor_sets;
    uchar*               ub_mapped;

    // iris_texture_t *render_texture;
    // VkFramebuffer framebuffer;
    // VkRenderPass render_pass;
  };

  extern void     nv_camera_destroy(nv_camera_t* cam);
  extern nv_error nv_camera_init(iris_driver_t* driver, nv_camera_t* cam);
  extern mat4     nv_camera_get_projection(nv_camera_t* cam);
  extern mat4     nv_camera_get_view(nv_camera_t* cam);

  extern vec3 nv_camera_get_up(nv_camera_t* cam);
  extern vec3 nv_camera_get_front(nv_camera_t* cam);
  extern vec3 nv_camera_get_right(nv_camera_t* cam);

  extern void nv_camera_rotate(nv_camera_t* cam, float yaw_, float pitch_);
  extern void nv_camera_move(nv_camera_t* cam, vec3 amt);
  extern void nv_camera_set_position(nv_camera_t* cam, vec3 pos);
  extern void nv_camera_update(nv_camera_t* cam, struct nv_renderer* rd);
  extern vec2 nv_camera_get_global_mouse_position(struct nv_input_ctx_t* inputctx, const nv_camera_t* cam);

  extern void nv_camera_upload_uniform_buffer(nv_camera_t* cam, struct nv_renderer* rd);

  extern size_t nv_camera_get_read_offset(const nv_camera_t* cam);
  extern size_t nv_camera_get_write_offset(const nv_camera_t* cam);

#ifdef __cplusplus
}
#endif

#endif // NOVA_CAMERA_H
