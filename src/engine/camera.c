#include "../../include/engine/camera.h"
#include "../../external/volk/volk.h"
#include "../../include/engine/input.h"
#include "../../include/engine/renderer.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/descriptors.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/ringbuffer.h"
#include "../../include/iris/texture.h"
#include "../../include/iris/types.h"
#include "../../include/iris/vkstdafx.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/math/mat.h"
#include "../../include/std/include/math/math.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/math/vec3.h"
#include "../../include/std/include/math/vec4.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vk_platform.h>

void
nv_camera_destroy(nv_camera_t* cam)
{
  // nv_descriptor_set_t_destroy(cam->sets);
  iris_ring_buffer_destroy(&cam->uniform_buffer);
}

nv_error
nv_camera_init(iris_driver_t* driver, nv_camera_t* cam)
{
  nvvk_ctx_t* const vkctx = driver->vkctx;

  const float ortho_half_w = 5.0F, ortho_half_h = 5.0F;
  *cam = (nv_camera_t){
    .ortho_half_size = (vec2){ ortho_half_w, ortho_half_h },
    .perspective     = nv_zero_init(mat4),
    .ortho           = m4ortho(-ortho_half_w, ortho_half_w, -ortho_half_h, ortho_half_h, 0.1f, 100.0f),
    .position        = (vec3){ 0.0f, 0.0f, 10.0f },
    .actual_pos      = (vec3){ 0.0f, 0.0f, 10.0f },
    .front           = (vec3){ 0.0f, 0.0f, 1.0f },

    // These angles should not be in radians because they're converted at update() time.
    .yaw_degrees   = -90.0F,
    .pitch_degrees = 0.0F,
    .fov           = 90.0f,

    .near_clip = 0.1F,
    .far_clip  = 1000.0F,
  };

  VkPhysicalDeviceProperties phys_device_properties;
  vkGetPhysicalDeviceProperties(vkctx->phys_device, &phys_device_properties);

  VkDeviceSize const ub_align = phys_device_properties.limits.minUniformBufferOffsetAlignment;
  nv_assert_else_return(ub_align != 0, NV_ERROR_INVALID_RETVAL);

  // the size of a single uniform buffer.
  size_t const ub_size = align_up_size(sizeof(nv_camera_uniform_buffer_t), ub_align);
  nv_assert_else_return(ub_size != 0, NV_ERROR_INVALID_RETVAL);

  iris_buffer_extra_create_info_t extra_info = nv_zero_init(iris_buffer_extra_create_info_t);
  extra_info.multibuffering_enable           = true;
  extra_info.multibuffering_frames           = CAMERA_FAKE_BUFFER_COUNT;
  extra_info.custom_memory_flags             = IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT;

  nv_error code = iris_ring_buffer_init(driver, ub_size, ub_align, &extra_info, IRIS_BUFFER_FLAGS_UNIFORM_BUFFER_BIT, &cam->uniform_buffer);
  nv_assert_else_return(code == NV_SUCCESS, code);

  VkDescriptorSetLayoutBinding bindings[] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL } };
  if (nv_allocate_descriptor_set(driver, &g_pool, bindings, 1, &cam->descriptor_sets) != NV_ERROR_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_assert_else_return(cam->uniform_buffer.backing.handle != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(cam->descriptor_sets != NULL, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(cam->descriptor_sets->set != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(ub_size != 0, NV_ERROR_BROKEN_STATE);

  size_t whole_buffer_size = iris_buffer_size(&cam->uniform_buffer.backing);
  size_t slice_size        = cam->uniform_buffer.slice_size;

  VkDescriptorBufferInfo const bufferinfo = {
    .buffer = cam->uniform_buffer.backing.handle,
    .offset = 0,
    .range  = (VkDeviceSize)slice_size, // We only need to bind one slice to the shader once, right?
  };

  VkWriteDescriptorSet const write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = cam->descriptor_sets->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
    .pBufferInfo     = &bufferinfo,
  };
  if (nv_descriptor_set_submit_write(vkctx, cam->descriptor_sets, &write) != NV_ERROR_SUCCESS)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  /**
   * We will dynamically offset into this to write the data
   * So we map the whole buffer.
   */
  iris_memory_map(&cam->uniform_buffer.backing.memory, 0, whole_buffer_size, (void**)&cam->ub_mapped);
  nv_assert_else_return(cam->ub_mapped != NULL, NV_ERROR_EXTERNAL);

  return NV_ERROR_SUCCESS;
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
nv_camera_get_up(nv_camera_t* cam)
{
  return v3normalize(v3cross(nv_camera_get_right(cam), nv_camera_get_front(cam)));
}

vec3
nv_camera_get_front(nv_camera_t* cam)
{
  return cam->front;
}

vec3
nv_camera_get_right(nv_camera_t* cam)
{
  return v3normalize(v3cross(nv_camera_get_front(cam), NOVA_CAMERA_WORLD_UP));
}

void
nv_camera_rotate(nv_camera_t* cam, float yaw_, float pitch_)
{
  cam->yaw_degrees += yaw_;
  cam->pitch_degrees -= pitch_;

  cam->yaw_degrees = fmod(cam->yaw_degrees, 360.0);

  const double bound = 89.9;
  cam->pitch_degrees = NVM_CLAMP(cam->pitch_degrees, -bound, bound);
}

void
nv_camera_move(nv_camera_t* cam, const vec3 amt)
{
  cam->actual_pos = v3add(cam->actual_pos, v3muls(nv_camera_get_right(cam), amt.x));
  cam->actual_pos = v3add(cam->actual_pos, v3muls(nv_camera_get_up(cam), amt.y));
  cam->actual_pos = v3add(cam->actual_pos, v3muls(nv_camera_get_front(cam), amt.z));
}

void
nv_camera_set_position(nv_camera_t* cam, const vec3 pos)
{
  cam->actual_pos = pos;
}

void
nv_camera_update(nv_camera_t* cam, struct nv_renderer* rd)
{
  // if (cam->uniform_buffer.frames_in_flight != nv_rdr_get_frames_in_flight(rd))
  // {
  //   iris_ring_buffer_resize(&cam->uniform_buffer, cam->uniform_buffer.slice_size, cam->uniform_buffer.backing.alignment, nv_rdr_get_frames_in_flight(rd), false);
  // }
  iris_ring_buffer_next(&camera.uniform_buffer);

  const double yaw_rads   = NVM_DEG2RAD(cam->yaw_degrees);
  const double pitch_rads = NVM_DEG2RAD(cam->pitch_degrees);
  const double cospitch   = cos(pitch_rads);

  vec3 new_front;
  new_front.x = cos(yaw_rads) * cospitch;
  new_front.y = sin(pitch_rads);
  new_front.z = sin(yaw_rads) * cospitch;

  cam->front = v3normalize(new_front);

  cam->view = m4lookat(cam->actual_pos, v3add(cam->actual_pos, nv_camera_get_front(cam)), nv_camera_get_up(cam));

  const double ortho_half_w = cam->ortho_half_size.x;
  const double ortho_half_h = cam->ortho_half_size.y;

  const nv_extent2 render_extent = nv_rdr_get_render_extent(rd);
  const double     aspect        = (double)render_extent.width / (double)render_extent.height;

  cam->perspective = m4perspective(cam->fov, aspect, cam->near_clip, cam->far_clip);
  cam->ortho       = m4ortho(-ortho_half_w, ortho_half_w, -ortho_half_h, ortho_half_h, 0.1, 100.0);
}

void
nv_camera_upload_uniform_buffer(nv_camera_t* cam, struct nv_renderer* rd)
{
  const nv_extent2 render_extent = nv_rdr_get_render_extent(rd);
  const vec3       right         = nv_camera_get_right(cam);

  nv_camera_uniform_buffer_t ub = nv_zero_init(nv_camera_uniform_buffer_t);

  nvm_mat_copy(ub.perspective, cam->perspective);
  nvm_mat_copy(ub.ortho, cam->ortho);
  nvm_mat_copy(ub.view, cam->view);

  nvm_vec_copy(ub.camera_position, cam->position);
  nvm_vec_copy(ub.camera_front, cam->front);
  nvm_vec_copy(ub.camera_right, right);

  ub.clip_plane    = (vec4f){ (float)cam->fov, (float)cam->near_clip, (float)cam->far_clip, 0.0F };
  ub.render_extent = (vec2u){ (u32)render_extent.width, (u32)render_extent.height };
  ub.image_index   = rd->image_index;

  void* mapping_write = (void*)((uchar*)cam->ub_mapped + nv_camera_get_write_offset(cam));
  nv_memcpy(mapping_write, &ub, sizeof(ub)); // Write uniform buffer data to the frame.

  iris_memory_flush(&cam->uniform_buffer.backing.memory);

  // Rotate advertised position to the camera's updated position
  cam->position = cam->actual_pos;
}

vec2
nv_camera_get_global_mouse_position(nv_input_ctx_t* inputctx, const nv_camera_t* cam)
{
  vec2 mouse_pos  = v2mulv(nv_input_get_mouse_position(inputctx), cam->ortho_half_size);
  vec2 global_pos = v2add((vec2){ cam->position.x, cam->position.y }, mouse_pos);
  return global_pos;
}