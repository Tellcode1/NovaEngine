#include "../include/engine/atlas.h"
#include "../include/engine/format.h"
#include "../include/engine/image.h"

#include "../include/std/include/containers/rectpack.h"
#include "../include/std/include/errorcodes.h"
#include "../include/std/include/stdafx.h"
#include "../include/std/include/string.h"
#include "../include/std/include/types.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <stdint.h>
#include <stdio.h>

#include <SDL3_image/SDL_image.h>

nv_error
nv_texture_atlas_init(size_t width, size_t height, nv_format fmt, u32 padding, nv_texture_atlas_t* dst)
{
  nv_assert_else_return(dst != NULL, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(width != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(height != 0, NV_ERROR_INVALID_ARG);
  nv_assert_else_return(fmt != NOVA_FORMAT_UNDEFINED, NV_ERROR_INVALID_ARG);

  *dst = nv_zero_init(nv_texture_atlas_t);

  dst->canary  = NOVA_CONT_CANARY;
  dst->width   = width;
  dst->height  = height;
  dst->format  = fmt;
  dst->padding = padding;
  dst->data    = (unsigned char*)nv_calloc(width * height * nv_format_get_bytes_per_pixel(dst->format));
  nv_assert_else_return(dst->data != NULL, NV_ERROR_MALLOC_FAILED);

  dst->mutex = SDL_CreateMutex();
  if (dst->mutex == NULL)
  {
    return NV_ERROR_EXTERNAL;
  }

  if (nv_skyline_bin_init(width, height, &dst->bin) != 0)
  {
    return NV_ERROR_INVALID_RETVAL;
  }

  nv_assert_else_return(NOVA_CONT_IS_VALID(dst), NV_ERROR_BROKEN_STATE);

  return NV_ERROR_SUCCESS;
}

/**
 * Wow such production ready level code.
 * This code should win a nobel peace prize
 * Or should be tried for war crimes and crimes against humanity.
 */
SDL_Mutex* atlas_resize_mutex = NULL;

int
nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image* img, size_t* out_x, size_t* out_y)
{
  if ((atlas == NULL) || (img == NULL) || img->width <= 0 || img->height <= 0 || (out_x == NULL) || (out_y == NULL))
  {
    return 0;
  }
  nv_assert_else_return(NOVA_CONT_IS_VALID(atlas), -1);

  if (atlas_resize_mutex == NULL)
  {
    atlas_resize_mutex = SDL_CreateMutex();
  }

  SDL_LockMutex(atlas->mutex);

  nv_skyline_rect_t const rect = { .width = img->width + (2ULL * atlas->padding), .height = img->height + (2ULL * atlas->padding) };

  size_t x, y;
  bool   packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y) != 0;

  while (!packed)
  {
    SDL_UnlockMutex(atlas->mutex);

    SDL_LockMutex(atlas_resize_mutex);
    nv_texture_atlas_resize(atlas, 2);
    SDL_UnlockMutex(atlas_resize_mutex);

    SDL_LockMutex(atlas->mutex);

    packed = (nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y) != 0);
  }

  nv_skyline_bin_place_rect(&atlas->bin, &rect, x, y);
  *out_x = x + atlas->padding;
  *out_y = y + atlas->padding;

  nv_image dst = { .width = atlas->width, .height = atlas->height, .format = NOVA_FORMAT_R8, .data = atlas->data };
  nv_image_overlay(&dst, (vec2i){ (int)*out_x, (int)*out_y }, img, v2izero);

  SDL_UnlockMutex(atlas->mutex);
  return 1;
}

void
nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale)
{
  nv_assert_else_return(NOVA_CONT_IS_VALID(atlas), );
  SDL_LockMutex(atlas->mutex);

  if (atlas->width == 0 || atlas->height == 0)
  {
    nv_log_error("zero size atlas? possible corruption\n");
    SDL_UnlockMutex(atlas->mutex);
    return;
  }

  size_t const old_w    = atlas->width;
  size_t const old_h    = atlas->height;
  size_t const new_w    = atlas->width * scale;
  size_t const new_h    = atlas->height * scale;
  size_t const channels = nv_format_get_bytes_per_pixel(atlas->format);

  unsigned char* new_data = (uchar*)nv_calloc(new_w * new_h * channels);
  nv_assert_else_return(new_data != NULL, );

  if (atlas->data != NULL)
  {
    for (size_t y = 0; y < old_h; y++)
    {
      size_t const src_offset = y * old_w * channels;
      size_t const dst_offset = y * new_w * channels;
      nv_memcpy(&new_data[dst_offset], &atlas->data[src_offset], old_w * channels);
    }

    nv_free(atlas->data);
  }

  atlas->data   = new_data;
  atlas->width  = new_w;
  atlas->height = new_h;

  nv_skyline_bin_resize(&atlas->bin, new_w, new_h);

  SDL_UnlockMutex(atlas->mutex);
}

int
nv_texture_atlas_finish(nv_texture_atlas_t* atlas)
{
  nv_assert_else_return(NOVA_CONT_IS_VALID(atlas), -1);

  SDL_LockMutex(atlas->mutex);

  size_t max_w = 0;
  size_t max_h = 0;

  for (size_t i = 0; i < atlas->bin.num_rects; i++)
  {
    nv_skyline_rect_t* r = &atlas->bin.rects[i];
    max_w                = NV_MAX(max_w, r->posx + r->width);
    max_h                = NV_MAX(max_h, r->posy + r->height);
  }

  const size_t optimal_w = max_w, optimal_h = max_h;

  if ((optimal_w == atlas->width && optimal_h == atlas->height) || (optimal_w == 0 || optimal_h == 0))
  {
    SDL_UnlockMutex(atlas->mutex);
    return 0;
  }

  if (atlas->width > optimal_w || atlas->height > optimal_h)
  {
    if (max_w == 0 || max_h == 0)
    {
      nv_log_error("0 optimal w/h??\n");
      return -1;
    }
    size_t const channels = nv_format_get_bytes_per_pixel(atlas->format);
    if (channels == 0)
    {
      nv_log_error("invalid format?\n");
      return -1;
    }
    unsigned char* new_data = (unsigned char*)nv_calloc(max_w * max_h * channels);
    if (new_data != NULL)
    {
      for (size_t y = 0; y < max_h; y++)
      {
        nv_memcpy(new_data + y * max_w * channels, atlas->data + y * atlas->width * channels, max_w * channels);
      }
      nv_free(atlas->data);
      atlas->data   = new_data;
      atlas->width  = max_w;
      atlas->height = max_h;
    }
  }

  SDL_UnlockMutex(atlas->mutex);
  return -1;
}

void
nv_texture_atlas_destroy(nv_texture_atlas_t* atlas)
{
  if (atlas == NULL)
  {
    return;
  }
  nv_assert_else_return(NOVA_CONT_IS_VALID(atlas), );

  SDL_LockMutex(atlas->mutex);
  if (atlas->data != NULL)
  {
    nv_free(atlas->data);
  }
  nv_skyline_bin_destroy(&atlas->bin);
  SDL_UnlockMutex(atlas->mutex);

  SDL_DestroyMutex(atlas->mutex);
}
