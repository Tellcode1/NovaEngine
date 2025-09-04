#include "engine/image.h"
#include "engine/atlas.hpp"
#include "engine/format.hpp"
#include "std/math/math.h"
#include "std/stdafx.h"
#include "std/string.h"

#include "std/containers/list.h"
#include "std/containers/rectpack.h"

#include <SDL3/SDL_pixels.h>
#include <math.h>
#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

nv_error
nv_image_load(const char* path, nv_image* dst)
{
  nv_bzero(dst, sizeof(nv_image));

  SDL_Surface* surface = IMG_Load(path);
  nv_assert_and_exec(surface != NULL, { nv_log_error("load failed $?(%s)\n", SDL_GetError()); });

  *dst = _nv_sdl_surface_to_image(surface);

  return NV_SUCCESS;
}

unsigned char*
nv_image_pad_channels(const nv_image* src, size_t dst_channels)
{
  const size_t src_channels = lr::nv_format_get_num_channels(src->format);
  nv_assert(src_channels < dst_channels);

  uint8_t* dst = (uint8_t*)nv_calloc(src->width * src->height * dst_channels * sizeof(uchar));

  for (size_t y = 0; y < src->height; y++)
  {
    for (size_t x = 0; x < src->width; x++)
    {
      for (size_t c = 0; c < dst_channels; c++)
      {
        if (c < src_channels)
        {
          dst[(((y * src->width) + x) * dst_channels) + c] = src->data[(((y * src->width) + x) * src_channels) + c];
        }
        else
        {
          if (c == 3)
          { // alpha channel
            dst[(((y * src->width) + x) * dst_channels) + c] = __UINT8_MAX__;
          }
          else
          {
            dst[(((y * src->width) + x) * dst_channels) + c] = 0;
          }
        }
      }
    }
  }

  return dst;
}

bool
nv_image_overlay(nv_image* dst, const nv_image* src, int dst_x_offset, int dst_y_offset, int src_x_offset, int src_y_offset)
{
  nv_assert(dst != NULL);
  nv_assert(src != NULL);

  const int src_channels = lr::nv_format_get_num_channels(src->format);

  for (ssize_t y = src_y_offset; y < (ssize_t)src->height; y++)
  {
    for (ssize_t x = src_x_offset; x < (ssize_t)src->width; x++)
    {
      ssize_t dst_x = dst_x_offset + (x - src_x_offset);
      ssize_t dst_y = dst_y_offset + (y - src_y_offset);

      if (dst_x >= 0 && dst_x < (ssize_t)dst->width && dst_y >= 0 && dst_y < (ssize_t)dst->height)
      {
        size_t src_i = (y * src->width + x) * src_channels;
        size_t dst_i = (dst_y * dst->width + dst_x) * src_channels;

        for (int c = 0; c < src_channels; c++)
        {
          dst->data[dst_i + c] = src->data[src_i + c];
        }
      }
    }
  }

  return 0;
}

void
nv_image_enlarge(nv_image* dst, const nv_image* src, size_t scale)
{
  size_t new_w = src->width * scale;

  nv_assert(dst->data != NULL);

  uchar*       write = dst->data;
  const uchar* read  = src->data;

  size_t bpp = lr::nv_format_get_bytes_per_pixel(src->format); // bytes per pixel
  for (size_t y = 0; y < src->height; y++)
  {
    for (size_t x = 0; x < src->width; x++)
    {
      size_t src_i = (y * src->width + x) * bpp;
      for (size_t i = 0; i < scale; i++)
      {
        for (size_t j = 0; j < scale; j++)
        {
          size_t dst_i = ((y * scale + i) * new_w + (x * scale + j)) * bpp;
          for (size_t c = 0; c < bpp; c++)
          {
            write[dst_i + c] = read[src_i + c];
          }
        }
      }
    }
  }
}

void
nv_image_bilinear_filter(nv_image* dst, const nv_image* src, flt_t scale)
{
  const int nchannels = lr::nv_format_get_num_channels(src->format);

  dst->width  = (size_t)((flt_t)src->width / scale);
  dst->height = (size_t)((flt_t)src->width / scale);
  dst->format = src->format;
  dst->data   = (uchar*)nv_calloc(dst->width * dst->height * lr::nv_format_get_bytes_per_pixel(dst->format));

  // Calculate the ratios for x and y coordinates
  flt_t x_ratio, y_ratio;
  if (dst->width > 1)
  {
    x_ratio = ((flt_t)src->width - 1.0F) / ((flt_t)dst->width - 1.0F);
  }
  else
  {
    x_ratio = 0;
  }

  if (dst->height > 1)
  {
    y_ratio = ((flt_t)src->height - 1.0F) / ((flt_t)dst->height - 1.0F);
  }
  else
  {
    y_ratio = 0;
  }

  for (size_t y = 0; y < dst->height; y++)
  {
    const flt_t ratiod_y = y_ratio * (flt_t)y;
    flt_t       y_l      = floorf(ratiod_y);
    flt_t       y_h      = ceilf(ratiod_y);
    flt_t       y_weight = (ratiod_y)-y_l;

    const size_t y_l_offset = (size_t)y_l * src->width * nchannels;
    const size_t y_h_offset = (size_t)y_h * src->width * nchannels;

    for (size_t x = 0; x < dst->width; x++)
    {
      const flt_t ratiod_x = x_ratio * (flt_t)x;

      flt_t x_l      = floorf(ratiod_x);
      flt_t x_h      = ceilf(ratiod_x);
      flt_t x_weight = (ratiod_x)-x_l;

      const size_t x_l_offset = (size_t)x_l * nchannels;
      const size_t x_h_offset = (size_t)x_h * nchannels;

      uchar* top_left_pixel     = &src->data[y_l_offset + x_l_offset];
      uchar* top_right_pixel    = &src->data[y_l_offset + x_h_offset];
      uchar* bottom_left_pixel  = &src->data[y_h_offset + x_l_offset];
      uchar* bottom_right_pixel = &src->data[y_h_offset + x_h_offset];
      for (int c = 0; c < nchannels; c++)
      {
        flt_t pixel = (flt_t)top_left_pixel[c] * (1.0F - x_weight) * (1.0F - y_weight) + (flt_t)top_right_pixel[c] * x_weight * (1.0F - y_weight)
            + (flt_t)bottom_left_pixel[c] * y_weight * (1.0F - x_weight) + (flt_t)bottom_right_pixel[c] * x_weight * y_weight;

        dst->data[(y * dst->width + x) * nchannels + c] = (unsigned char)NVM_CLAMP(pixel, 0.0f, 255.0f);
      }
    }
  }
}

SDL_Surface*
_nv_imageo_sdl_surface(const nv_image* tex)
{
  nv_assert_else_return(tex->width != 0, NULL);
  nv_assert_else_return(tex->height != 0, NULL);
  nv_assert_else_return(tex->format != NOVA_FORMAT_UNDEFINED, NULL);

  SDL_Surface* surface = SDL_CreateSurfaceFrom(
      (int)tex->width,
      (int)tex->height,
      (SDL_PixelFormat)lr::nv_format_to_sdl_format(tex->format),
      tex->data,
      (int)(tex->width * lr::nv_format_get_bytes_per_pixel(tex->format)));

  return surface;
}

nv_image
_nv_sdl_surface_to_image(SDL_Surface* surface)
{
  nv_assert_else_return(surface->w != 0, nv_zero_init(nv_image));
  nv_assert_else_return(surface->h != 0, nv_zero_init(nv_image));

  SDL_LockSurface(surface);

  const size_t surface_size_bytes = surface->w * surface->h * SDL_GetPixelFormatDetails(surface->format)->bytes_per_pixel;

  nv_image image;
  image.width  = (size_t)surface->w;
  image.height = (size_t)surface->h;
  nv_assert_else_return(image.width != NOVA_FORMAT_UNDEFINED, nv_zero_init(nv_image));
  nv_assert_else_return(image.height != NOVA_FORMAT_UNDEFINED, nv_zero_init(nv_image));

  image.format = lr::nv_sdl_format_to_nv_format((SDL_Format_)surface->format);
  nv_assert_else_return(image.format != NOVA_FORMAT_UNDEFINED, nv_zero_init(nv_image));

  image.data = (uchar*)nv_calloc(surface_size_bytes);

  nv_memcpy(image.data, surface->pixels, surface_size_bytes);

  SDL_UnlockSurface(surface);
  SDL_DestroySurface(surface);

  return image;
}

void
nv_image_write_png(const nv_image* tex, const char* path)
{
  if (tex == NULL || path == NULL || tex->data == NULL)
  {
    return;
  }

  SDL_Surface* surface = _nv_imageo_sdl_surface(tex);

  if (IMG_SavePNG(surface, path) != 0)
  {
    nv_log_error("Failed in writing image %s. Perhaps its parent directories do not exist?. SDL reports: %s\n", path, SDL_GetError());
  }

  SDL_DestroySurface(surface);
}

void
nv_image_write_jpeg(const nv_image* tex, const char* path, int quality)
{
  if (tex == NULL || path == NULL || tex->data == NULL)
  {
    return;
  }

  SDL_Surface* surface = _nv_imageo_sdl_surface(tex);

  if (IMG_SaveJPG(surface, path, quality) != 0)
  {
    nv_log_error("Failed in writing image %s. Perhaps its parent directories do not exist?. SDL reports: %s\n", path, SDL_GetError());
  }

  SDL_DestroySurface(surface);
}

// nv_image

const char*
lr::nv_format_to_string(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_UNDEFINED: return "NOVA_FORMAT_UNDEFINED";
    case NOVA_FORMAT_R8: return "NOVA_FORMAT_R8";
    case NOVA_FORMAT_RG8: return "NOVA_FORMAT_RG8";
    case NOVA_FORMAT_RGB8: return "NOVA_FORMAT_RGB8";
    case NOVA_FORMAT_RGBA8: return "NOVA_FORMAT_RGBA8";
    case NOVA_FORMAT_BGR8: return "NOVA_FORMAT_BGR8";
    case NOVA_FORMAT_BGRA8: return "NOVA_FORMAT_BGRA8";
    case NOVA_FORMAT_RGB16: return "NOVA_FORMAT_RGB16";
    case NOVA_FORMAT_RGBA16: return "NOVA_FORMAT_RGBA16";
    case NOVA_FORMAT_RG32: return "NOVA_FORMAT_RG32";
    case NOVA_FORMAT_RGB32: return "NOVA_FORMAT_RGB32";
    case NOVA_FORMAT_RGBA32: return "NOVA_FORMAT_RGBA32";
    case NOVA_FORMAT_R8_SINT: return "NOVA_FORMAT_R8_SINT";
    case NOVA_FORMAT_RG8_SINT: return "NOVA_FORMAT_RG8_SINT";
    case NOVA_FORMAT_RGB8_SINT: return "NOVA_FORMAT_RGB8_SINT";
    case NOVA_FORMAT_RGBA8_SINT: return "NOVA_FORMAT_RGBA8_SINT";
    case NOVA_FORMAT_R8_UINT: return "NOVA_FORMAT_R8_UINT";
    case NOVA_FORMAT_RG8_UINT: return "NOVA_FORMAT_RG8_UINT";
    case NOVA_FORMAT_RGB8_UINT: return "NOVA_FORMAT_RGB8_UINT";
    case NOVA_FORMAT_RGBA8_UINT: return "NOVA_FORMAT_RGBA8_UINT";
    case NOVA_FORMAT_R8_SRGB: return "NOVA_FORMAT_R8_SRGB";
    case NOVA_FORMAT_RG8_SRGB: return "NOVA_FORMAT_RG8_SRGB";
    case NOVA_FORMAT_RGB8_SRGB: return "NOVA_FORMAT_RGB8_SRGB";
    case NOVA_FORMAT_RGBA8_SRGB: return "NOVA_FORMAT_RGBA8_SRGB";
    case NOVA_FORMAT_BGR8_SRGB: return "NOVA_FORMAT_BGR8_SRGB";
    case NOVA_FORMAT_BGRA8_SRGB: return "NOVA_FORMAT_BGRA8_SRGB";
    case NOVA_FORMAT_D16: return "NOVA_FORMAT_D16";
    case NOVA_FORMAT_D24: return "NOVA_FORMAT_D24";
    case NOVA_FORMAT_D32: return "NOVA_FORMAT_D32";
    case NOVA_FORMAT_D24_S8: return "NOVA_FORMAT_D24_S8";
    case NOVA_FORMAT_D32_S8: return "NOVA_FORMAT_D32_S8";
    case NOVA_FORMAT_BC1: return "NOVA_FORMAT_BC1";
    case NOVA_FORMAT_BC3: return "NOVA_FORMAT_BC3";
    case NOVA_FORMAT_BC7: return "NOVA_FORMAT_BC7";
    default: return "(NotAFormat)";
  }
  return "(NotAFormat)";
}

bool
lr::nv_format_has_color_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return 0;
    default: return 1;
  }
}

// Returns false even for stencil/depth and undefined format
bool
lr::nv_format_has_alpha_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;
    default: return 0;
  }
}

bool
lr::nv_format_has_depth_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

bool
lr::nv_format_has_stencil_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

int
lr::nv_format_get_bytes_per_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;

    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_D16: return 2;

    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8: return 3;

    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 4;

    default:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return -1;
  }
}

int
lr::nv_format_get_bytes_per_pixel(nv_format fmt)
{
  return lr::nv_format_get_bytes_per_channel(fmt) * lr::nv_format_get_num_channels(fmt);
}

int
lr::nv_format_get_num_channels(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D32: return 1;

    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 2;

    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB: return 3;

    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 4;

    // FIXME: Implement
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED:
    default: return 0;
  }
}

nv_format
lr::nv_sdl_format_to_nv_format(SDL_Format_ format)
{
  switch (format)
  {
    /* TODO: Add more? I wasn't able to find any more though */
    case SDL_PIXELFORMAT_RGB24: return NOVA_FORMAT_RGB8;
    case SDL_PIXELFORMAT_RGBA32: return NOVA_FORMAT_RGBA8;
    case SDL_PIXELFORMAT_ABGR32: return NOVA_FORMAT_BGRA8;

    case SDL_PIXELFORMAT_YV12:
    case SDL_PIXELFORMAT_IYUV:
    default: return NOVA_FORMAT_UNDEFINED;
  }
}

SDL_Format_
lr::nv_format_to_sdl_format(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_RGB8: return SDL_PIXELFORMAT_RGB24;
    case NOVA_FORMAT_RGBA8: return SDL_PIXELFORMAT_RGBA32;
    case NOVA_FORMAT_BGRA8: return SDL_PIXELFORMAT_ABGR32;
    default: return SDL_PIXELFORMAT_UNKNOWN;
  }
}

// ==============================
// ATLAS
// ==============================

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
  dst->data    = (unsigned char*)nv_calloc(width * height * lr::nv_format_get_bytes_per_pixel(dst->format));
  nv_assert_else_return(dst->data != NULL, NV_ERROR_MALLOC_FAILED);

  dst->mutex = SDL_CreateMutex();
  if (!dst->mutex)
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

SDL_Mutex* atlas_resize_mutex = NULL;

int
nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image* img, size_t* out_x, size_t* out_y)
{
  if (!atlas || !img || img->width <= 0 || img->height <= 0 || !out_x || !out_y)
  {
    return 0;
  }
  nv_assert(NOVA_CONT_IS_VALID(atlas));

  if (!atlas_resize_mutex)
  {
    atlas_resize_mutex = SDL_CreateMutex();
  }

  SDL_LockMutex(atlas->mutex);

  nv_skyline_rect_t rect = { .width = img->width + (2 * atlas->padding), .height = img->height + (2 * atlas->padding) };

  size_t x, y;
  bool   packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);

  while (!packed)
  {
    SDL_UnlockMutex(atlas->mutex);

    SDL_LockMutex(atlas_resize_mutex);
    nv_texture_atlas_resize(atlas, 2);
    SDL_UnlockMutex(atlas_resize_mutex);

    SDL_LockMutex(atlas->mutex);

    packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);
  }

  nv_skyline_bin_place_rect(&atlas->bin, &rect, x, y);
  *out_x = x + atlas->padding;
  *out_y = y + atlas->padding;

  nv_image dst = { .width = atlas->width, .height = atlas->height, .format = NOVA_FORMAT_R8, .data = atlas->data };
  nv_image_overlay(&dst, img, (int)*out_x, (int)*out_y, 0, 0);

  SDL_UnlockMutex(atlas->mutex);
  return 1;
}

void
nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale)
{
  nv_assert(NOVA_CONT_IS_VALID(atlas));
  SDL_LockMutex(atlas->mutex);

  if (atlas->width == 0 || atlas->height == 0)
  {
    nv_log_error("zero size atlas? possible corruption\n");
    SDL_UnlockMutex(atlas->mutex);
    return;
  }

  size_t old_w    = atlas->width;
  size_t old_h    = atlas->height;
  size_t new_w    = atlas->width * scale;
  size_t new_h    = atlas->height * scale;
  size_t channels = lr::nv_format_get_bytes_per_pixel(atlas->format);

  unsigned char* new_data = (uchar*)nv_calloc(new_w * new_h * channels);
  nv_assert(new_data != NULL);

  if (atlas->data)
  {
    for (size_t y = 0; y < old_h; y++)
    {
      size_t src_offset = y * old_w * channels;
      size_t dst_offset = y * new_w * channels;
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
  nv_assert(NOVA_CONT_IS_VALID(atlas));

  SDL_LockMutex(atlas->mutex);

  size_t max_w = 0;
  size_t max_h = 0;

  for (size_t i = 0; i < atlas->bin.num_rects; i++)
  {
    nv_skyline_rect_t* r = &atlas->bin.rects[i];
    max_w                = NV_MAX(max_w, r->posx + r->width);
    max_h                = NV_MAX(max_h, r->posy + r->height);
  }

  size_t optimal_w = max_w, optimal_h = max_h;

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
    size_t channels = lr::nv_format_get_bytes_per_pixel(atlas->format);
    if (channels == 0)
    {
      nv_log_error("invalid format?\n");
      return -1;
    }
    unsigned char* new_data = (unsigned char*)nv_calloc(max_w * max_h * channels);
    if (new_data)
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
  if (!atlas)
  {
    return;
  }
  nv_assert(NOVA_CONT_IS_VALID(atlas));

  SDL_LockMutex(atlas->mutex);
  if (atlas->data)
  {
    nv_free(atlas->data);
  }
  nv_skyline_bin_destroy(&atlas->bin);
  SDL_UnlockMutex(atlas->mutex);

  SDL_DestroyMutex(atlas->mutex);
}
