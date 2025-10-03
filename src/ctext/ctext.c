#include "../../include/engine/ctext.h"
#include "../../external/volk/volk.h"
#include "../../include/engine/atlas.h"
#include "../../include/engine/fontc.h"
#include "../../include/engine/format.h"
#include "../../include/engine/renderer.h"
#include "../../include/engine/sprite.h"
#include "../../include/iris/buffer.h"
#include "../../include/iris/descriptors.h"
#include "../../include/iris/memory.h"
#include "../../include/iris/ringbuffer.h"
#include "../../include/iris/sampler.h"
#include "../../include/iris/texture.h"
#include "../../include/iris/types.h"
#include "../../include/std/include/alloc.h"
#include "../../include/std/include/containers/hashmap.h"
#include "../../include/std/include/containers/list.h"
#include "../../include/std/include/errorcodes.h"
#include "../../include/std/include/hash.h"
#include "../../include/std/include/math/vec2.h"
#include "../../include/std/include/stdafx.h"
#include "../../include/std/include/string.h"
#include "../../include/std/include/types.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* I have no idea what any of this is */

static inline void
ctext_load_font_upload_glyph_atlas(nv_renderer_t* rd, const nv_texture_atlas_t* atlas, cfont_t* dst)
{
  iris_texture_create_info_t const image_info = {
    .extent        = (nv_extent3D){ .width = atlas->width, .height = atlas->height, .depth = 1 },
    .alignment     = 1,
    .array_layers  = 1,
    .format        = NOVA_FORMAT_R8,
    .samples       = rd->samples,
    .flags         = IRIS_TEXTURE_SAMPLED_BIT,
    .linear_tiling = false,
    .extra         = NULL,
  };
  iris_texture_init(rd->driver, &image_info, &dst->texture);

  const size_t atlas_w = atlas->width;
  const size_t atlas_h = atlas->height;

  const size_t whole_image_size = atlas_w * atlas_h * nv_format_get_bytes_per_pixel(NOVA_FORMAT_R8);

  iris_texture_region_t region = {
    .offset      = (vec3i){ 0, 0, 0 },
    .extent      = (nv_extent3D){ atlas_w, atlas_h, 1 },
    .mip_level   = 0,
    .array_level = 0,
  };

  iris_texture_write_data(&dst->texture, &region, atlas->data, whole_image_size);

  const iris_sampler_create_info sampler_info = {
    .min_filter = NV_FILTER_LINEAR,
    .mag_filter = NV_FILTER_LINEAR,
    .wrapu      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .wrapv      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .wrapw      = IRIS_SAMPLER_WRAP_MODE_REPEAT,
    .anisotropy = 1.0F,
  };
  iris_create_sampler(rd->driver, &sampler_info, &dst->sampler);
}

static inline void
ctext_load_font_update_descriptors(nvvk_ctx_t* vkctx, nv_ctext_module* ctext, cfont_t* dst)
{
  const VkDescriptorImageInfo ctext_bitmap_image_info = {
    .sampler     = iris_sampler_get(dst->sampler),
    .imageView   = iris_texture_get_image_view(&dst->texture),
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  };

  VkWriteDescriptorSet writeSet = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = ctext->desc_set->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &ctext_bitmap_image_info,
  };
  for (size_t i = 0; i < (size_t)CTEXT_MAX_FONT_COUNT; i++)
  {
    writeSet.dstArrayElement = i;
    nv_descriptor_set_submit_write(vkctx, ctext->desc_set, &writeSet);
  }
}

void
ctext_load_font(nvvk_ctx_t* vkctx, nv_renderer_t* rdr, const char* font_path, int scale, cfont_t* dst)
{
  if ((rdr == NULL) || (dst == NULL))
  {
    nv_raise_error(NV_ERROR_INVALID_ARG, "rdr or dst is NULL!\n");
    return;
  }

  if (scale <= 0)
  {
    nv_raise_error(NV_ERROR_INVALID_ARG, "attempting to load a font with 0 fontscale.\n");
    return;
  }

  *dst = nv_zero_init(cfont_t);

  struct fontc_file_t f_file;
  if (fontc_load_font(font_path, scale, &f_file) != 0)
  {
    nv_raise_error(NV_ERROR_IO_ERROR, "There was an error loading the font file. Skipping\n");
    return;
  }

  // Store a pointer to the font for future reference
  *(cfont_t**)nv_list_push_empty(&rdr->ctext->fonts) = dst;

  dst->rd = rdr;

  nv_hashmap_init(256, sizeof(u32), sizeof(ctext_glyph_t), nv_hash_murmur3, nv_allocator_c, NULL, &dst->glyph_map);
  nv_list_init(sizeof(ctext_drawcall_t), 4, nv_allocator_c, NULL, &dst->drawcalls);

  nv_texture_atlas_t atlas;

  dst->line_height = f_file.header.line_height;
  dst->space_width = f_file.header.space_width;
  atlas.width      = f_file.header.bmpwidth;
  atlas.height     = f_file.header.bmpheight;
  atlas.data       = f_file.bitmap;

  for (size_t i = 0; i < f_file.header.numglyphs; i++)
  {
    ctext_glyph_t glyph = {
      .x0      = f_file.glyphs[i].x0,
      .x1      = f_file.glyphs[i].x1,
      .y0      = f_file.glyphs[i].y0,
      .y1      = f_file.glyphs[i].y1,
      .l       = f_file.glyphs[i].l,
      .r       = f_file.glyphs[i].r,
      .b       = f_file.glyphs[i].b,
      .t       = f_file.glyphs[i].t,
      .advance = f_file.glyphs[i].advance,
    };
    u32 codepoint = f_file.glyphs[i].codepoint;
    nv_hashmap_insert(&dst->glyph_map, &codepoint, &glyph, NULL);
  }

  ctext_load_font_upload_glyph_atlas(rdr, &atlas, dst);
  ctext_load_font_update_descriptors(vkctx, rdr->ctext, dst);

  fontc_clean_font_file(&f_file);

  const size_t initial_font_gpu_buffer_size = 1024;

  iris_buffer_extra_create_info_t extra_info = nv_zero_init(iris_buffer_extra_create_info_t);
  extra_info.multibuffering_enable           = true;
  extra_info.multibuffering_frames           = nv_renderer_get_frames_in_flight(rdr);
  extra_info.custom_memory_flags             = IRIS_MEMORY_FLAGS_DEFAULT_BIT;

  nv_error code =
      iris_ring_buffer_init(rdr->driver, initial_font_gpu_buffer_size, 4, &extra_info, IRIS_BUFFER_FLAGS_VERTEX_BUFFER_BIT | IRIS_BUFFER_FLAGS_INDEX_BUFFER_BIT, &dst->buffer);
  nv_assert_else_return(code == NV_SUCCESS, );

  extra_info.multibuffering_enable = false;
  extra_info.multibuffering_frames = 1;
  extra_info.custom_memory_flags   = IRIS_MEMORY_FLAGS_PERSISTENT_MAPPED_BIT;

  code = iris_buffer_init(rdr->driver, initial_font_gpu_buffer_size, 4, &extra_info, IRIS_BUFFER_FLAGS_TRANSFER_ONLY_BIT, &dst->staging_buffer);
  nv_assert_else_return(code == NV_SUCCESS, );

  if (ctext_validate_font(dst) != 0)
  {
    // nv_log_error("Broken font. Something has gone horribly wrong\n");
    return;
  }
}

int
ctext_validate_font(const cfont_t* fnt)
{
  // If the renderer of the font has died, die along with the renderer.
  if ((fnt == NULL) || (fnt->rd == NULL))
  {
    return -1;
  }
  // TODO: Add support for a constant canary across the whole project?
  if (fnt->glyph_map.canary != NOVA_CONT_CANARY || fnt->drawcalls.canary != NOVA_CONT_CANARY)
  {
    return -1;
  }
  return 0;
}

void
ctext_destroy_font(cfont_t* fnt)
{
  if (ctext_validate_font(fnt) != 0)
  {
    // nv_log_error("Broken font\n");
    return;
  }

  iris_texture_destroy(&fnt->texture);

  iris_ring_buffer_destroy(&fnt->buffer);
  iris_buffer_destroy(&fnt->staging_buffer);

  nv_list_destroy(&fnt->drawcalls);
  nv_hashmap_destroy(&fnt->glyph_map);

  nv_bzero(fnt, sizeof(cfont_t));
}

nv_error
ctext_init(struct nv_renderer* rd)
{
  nv_assert_else_return(rd != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(nvvk_ctx_is_valid(rd->vkctx) == true, NV_ERROR_INVALID_ARG);

  rd->ctext = (nv_ctext_module*)nv_calloc(sizeof(nv_ctext_module));
  nv_assert_else_return(rd->ctext != NULL, NV_ERROR_MALLOC_FAILED);

  nv_ctext_module* ctext = rd->ctext;

  nv_error code = NV_ERROR_SUCCESS;

  if ((code = nv_list_init(sizeof(cfont_t*), 4, nv_allocator_c, NULL, &ctext->fonts)) != NV_ERROR_SUCCESS)
  {
    return code;
  }

  nv_assert_else_return(nv_list_is_valid(&ctext->fonts), NV_ERROR_BROKEN_STATE);

  const VkDescriptorSetLayoutBinding bindings[] = {
    // binding; descriptorType; descriptorCount; stageFlags;
    // pImmutableSamplers;
    { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, CTEXT_MAX_FONT_COUNT, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
  };

  nv_allocate_descriptor_set(rd->vkctx, &g_pool, bindings, nv_arrlen(bindings), &ctext->desc_set);
  nv_assert_else_return(ctext->desc_set != NULL, NV_ERROR_INVALID_RETVAL);

  nv_assert_else_return(nv_sprite_get_sampler(&rd->sprite_empty) != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);
  nv_assert_else_return(nv_sprite_get_vk_image_view(&rd->sprite_empty) != VK_NULL_HANDLE, NV_ERROR_BROKEN_STATE);

  const VkDescriptorImageInfo empty_img_info = {
    .sampler     = nv_sprite_get_sampler(&rd->sprite_empty),
    .imageView   = nv_sprite_get_vk_image_view(&rd->sprite_empty),
    .imageLayout = rd->sprite_empty.tex.current_layout,
  };

  VkWriteDescriptorSet write_set = {
    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet          = ctext->desc_set->set,
    .dstBinding      = 0,
    .descriptorCount = 1,
    .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo      = &empty_img_info,
  };
  for (size_t i = 0; i < (size_t)CTEXT_MAX_FONT_COUNT; i++)
  {
    write_set.dstArrayElement = i;
    if (nv_descriptor_set_submit_write(rd->vkctx, ctext->desc_set, &write_set) != NV_ERROR_SUCCESS)
    {
      return NV_ERROR_INVALID_RETVAL;
    }
  }

  return NV_ERROR_SUCCESS;
}

void
ctext_shutdown(struct nv_renderer* rd)
{
  if ((rd == NULL) || (rd->ctext == NULL))
  {
    return;
  }
  nv_list_destroy(&rd->ctext->fonts);
  nv_free(rd->ctext);
}

double
ctext_get_scale_for_fit(const cfont_t* fnt, const char* str, vec2 bbox)
{
  if ((fnt == NULL) || (str == NULL) || ctext_validate_font(fnt) != 0)
  {
    return 0.0F;
  }

  double width, height;
  ctext_get_text_size(fnt, str, &width, &height);

  double scale_x = bbox.x / width;
  double scale_y = bbox.y / height;
  return NV_MIN(scale_x, scale_y);
}
