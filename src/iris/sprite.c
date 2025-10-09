#include "../../include/engine/sprite.h"
#include "../../external/volk/volk.h"
#include "../../include/engine/format.h"
#include "../../include/engine/image.h"
#include "../../include/engine/renderer.h"
#include "../../include/iris/descriptors.h"
#include "../../include/iris/driver.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/texture.h"
#include "../../include/iris/types.h"
#include "../../include/iris/utils.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <stddef.h>
#include <stdint.h>

nv_error
nv_sprite_load_from_memory(iris_driver_t* driver, const unsigned char* data, size_t w, size_t h, nv_format fmt, nv_sprite_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(data != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(w != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(h != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(fmt != NOVA_FORMAT_UNDEFINED, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_sprite_t));

  dst->rcount = 1;

  fmt      = nv_vk_get_supported_format_for_draw(driver->vkctx, fmt);
  dst->fmt = fmt;

  iris_texture_create_info_t const tex_info = {
    .extent        = (nv_extent3D){ .width = w, .height = h, .depth = 1 },
    .alignment     = 1,
    .array_layers  = 1,
    .format        = fmt,
    .samples       = NOVA_SAMPLE_COUNT_1_SAMPLES,
    .flags         = IRIS_TEXTURE_SAMPLED_BIT,
    .linear_tiling = false,
    .extra         = NULL,
  };
  nv_return_error_if_fail(iris_texture_init(driver, &tex_info, &dst->tex));

  const size_t image_size = w * h * nv_format_get_bytes_per_pixel(fmt);

  iris_texture_region_t region = {
    .offset      = (vec3i){ 0, 0, 0 },
    .extent      = (nv_extent3D){ w, h, 1 },
    .mip_level   = 0,
    .array_level = 0,
  };
  nv_return_error_if_fail(iris_texture_write_data(&dst->tex, &region, data, image_size));

  iris_sampler_create_info const sampler_info = {
    .min_filter = NV_FILTER_NEAREST,
    .mag_filter = NV_FILTER_NEAREST,
    .wrapu      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .wrapv      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .wrapw      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .anisotropy = 1.0f,
    .min_lod    = 0.0f,
    .max_lod    = VK_LOD_CLAMP_NONE,
  };
  iris_create_sampler(driver, &sampler_info, &dst->sampler);

  VkDescriptorSetLayoutBinding const binding = (VkDescriptorSetLayoutBinding){
    .binding         = 0,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .descriptorCount = 1,
    .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  nv_allocate_descriptor_set(driver, &g_pool, &binding, 1, &dst->set);

  VkDescriptorImageInfo const desc_img = {
    .sampler     = iris_sampler_get(&dst->sampler),
    .imageView   = nv_sprite_get_vk_image_view(dst),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  VkWriteDescriptorSet const write = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = dst->set->set,
    .dstBinding      = 0,
    .dstArrayElement = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &desc_img,
  };
  nv_descriptor_set_submit_write(driver->vkctx, dst->set, &write);

  return NV_SUCCESS;
}

nv_error
nv_sprite_load_from_disk(iris_driver_t* driver, const char* path, nv_sprite_t* dst)
{
  nv_assert_else_return(driver != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(driver->vkctx) == true, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(path != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);

  nv_bzero(dst, sizeof(nv_sprite_t));

  nv_image tex = nv_zero_init(nv_image);

  nv_error code = nv_image_load(path, &tex);
  nv_assert_else_return(code == NV_SUCCESS, code);

  // TODO: should these be io errors?
  // I mean a read from the disk probably failed
  // TODO: this will be fixed by returning an error code from image loading function

  nv_assert_else_return(tex.width != 0, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.height != 0, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.data != NULL, NV_ERROR_IO_ERROR);
  nv_assert_else_return(tex.format != NOVA_FORMAT_UNDEFINED, NV_ERROR_IO_ERROR);

  code = nv_sprite_load_from_memory(driver, tex.data, tex.width, tex.height, tex.format, dst);
  if (code != NV_SUCCESS)
  {
    return code;
  }

  dst->rcount = 1;
  if (tex.data != NULL)
  {
    nv_free(tex.data);
  }

  return code;
}

void
nv_sprite_destroy(nv_sprite_t* spr)
{
  if (spr == NULL)
  {
    return;
  }

  iris_texture_destroy(&spr->tex);
}

void
nv_sprite_lock(nv_sprite_t* spr)
{
  spr->rcount++;
}

void
nv_sprite_release(nv_sprite_t* spr)
{
  spr->rcount--;
  if (spr->rcount <= 0)
  {
    nv_sprite_destroy(spr);
  }
}

void
nv_sprite_get_dimensions(const nv_sprite_t* spr, size_t* w, size_t* h)
{
  if (w != NULL)
  {
    *w = spr->w;
  }
  if (h != NULL)
  {
    *h = spr->h;
  }
}

VkImage
nv_sprite_get_vk_image(const nv_sprite_t* spr)
{
  return iris_texture_get_image(&spr->tex);
}

VkImageView
nv_sprite_get_vk_image_view(const nv_sprite_t* spr)
{
  return iris_texture_get_image_view(&spr->tex);
}

VkDescriptorSet
nv_sprite_get_descriptor_set(const nv_sprite_t* spr)
{
  return spr->set->set;
}

VkSampler
nv_sprite_get_sampler(const nv_sprite_t* spr)
{
  return iris_sampler_get(&spr->sampler);
}

nv_format
nv_sprite_get_format(const nv_sprite_t* spr)
{
  return spr->fmt;
}