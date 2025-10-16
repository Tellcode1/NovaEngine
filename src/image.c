#include "../include/engine/image.h"
#include "../include/engine/format.h"

#include "../include/std/include/errorcodes.h"
#include "../include/std/include/math/math.h"
#include "../include/std/include/stdafx.h"
#include "../include/std/include/string.h"
#include "../include/std/include/types.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

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
  const size_t src_channels = nv_format_get_num_channels(src->format);
  nv_assert_else_return(src_channels < dst_channels, NULL);

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
            dst[(((y * src->width) + x) * dst_channels) + c] = 255;
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
nv_image_overlay(nv_image* dst, vec2i dst_offset, const nv_image* src, vec2i src_offset)
{
  nv_assert_else_return(dst != NULL, 1);
  nv_assert_else_return(src != NULL, 1);

  const int src_channels = nv_format_get_num_channels(src->format);

  for (ssize_t y = src_offset.y; y < (ssize_t)src->height; y++)
  {
    for (ssize_t x = src_offset.x; x < (ssize_t)src->width; x++)
    {
      ssize_t const dst_x = dst_offset.x + (x - src_offset.x);
      ssize_t const dst_y = dst_offset.y + (y - src_offset.y);

      if (dst_x >= 0 && dst_x < (ssize_t)dst->width && dst_y >= 0 && dst_y < (ssize_t)dst->height)
      {
        size_t const src_i = (y * src->width + x) * src_channels;
        size_t const dst_i = (dst_y * dst->width + dst_x) * src_channels;

        for (int c = 0; c < src_channels; c++)
        {
          dst->data[dst_i + c] = src->data[src_i + c];
        }
      }
    }
  }

  return false;
}

void
nv_image_enlarge(nv_image* dst, const nv_image* src, size_t scale)
{
  size_t const new_w = src->width * scale;

  nv_assert_else_return(dst->data != NULL, );

  uchar*       write = dst->data;
  const uchar* read  = src->data;

  size_t const bpp = nv_format_get_bytes_per_pixel(src->format); // bytes per pixel
  for (size_t y = 0; y < src->height; y++)
  {
    for (size_t x = 0; x < src->width; x++)
    {
      size_t const src_i = (y * src->width + x) * bpp;
      for (size_t i = 0; i < scale; i++)
      {
        for (size_t j = 0; j < scale; j++)
        {
          size_t const dst_i = ((y * scale + i) * new_w + (x * scale + j)) * bpp;
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
nv_image_bilinear_filter(nv_image* dst, const nv_image* src, double scale)
{
  const int nchannels = nv_format_get_num_channels(src->format);

  dst->width  = (size_t)((double)src->width / scale);
  dst->height = (size_t)((double)src->width / scale);
  dst->format = src->format;
  dst->data   = (uchar*)nv_calloc(dst->width * dst->height * nv_format_get_bytes_per_pixel(dst->format));

  // Calculate the ratios for x and y coordinates
  double x_ratio, y_ratio;
  if (dst->width > 1)
  {
    x_ratio = ((double)src->width - 1.0F) / ((double)dst->width - 1.0F);
  }
  else
  {
    x_ratio = 0;
  }

  if (dst->height > 1)
  {
    y_ratio = ((double)src->height - 1.0F) / ((double)dst->height - 1.0F);
  }
  else
  {
    y_ratio = 0;
  }

  for (size_t y = 0; y < dst->height; y++)
  {
    const double ratiod_y = y_ratio * (double)y;
    double const y_l      = floor(ratiod_y);
    double const y_h      = ceil(ratiod_y);
    double const y_weight = (ratiod_y)-y_l;

    const size_t y_l_offset = (size_t)y_l * src->width * nchannels;
    const size_t y_h_offset = (size_t)y_h * src->width * nchannels;

    for (size_t x = 0; x < dst->width; x++)
    {
      const double ratiod_x = x_ratio * (double)x;

      double const x_l      = floor(ratiod_x);
      double const x_h      = ceil(ratiod_x);
      double const x_weight = (ratiod_x)-x_l;

      const size_t x_l_offset = (size_t)x_l * nchannels;
      const size_t x_h_offset = (size_t)x_h * nchannels;

      uchar* top_left_pixel     = &src->data[y_l_offset + x_l_offset];
      uchar* top_right_pixel    = &src->data[y_l_offset + x_h_offset];
      uchar* bottom_left_pixel  = &src->data[y_h_offset + x_l_offset];
      uchar* bottom_right_pixel = &src->data[y_h_offset + x_h_offset];
      for (int c = 0; c < nchannels; c++)
      {
        double const pixel = (double)top_left_pixel[c] * (1.0F - x_weight) * (1.0F - y_weight) + (double)top_right_pixel[c] * x_weight * (1.0F - y_weight)
            + (double)bottom_left_pixel[c] * y_weight * (1.0F - x_weight) + (double)bottom_right_pixel[c] * x_weight * y_weight;

        dst->data[(y * dst->width + x) * nchannels + c] = (unsigned char)NVM_CLAMP(pixel, 0.0f, 255.0f);
      }
    }
  }
}

SDL_Surface*
_nv_image_to_sdl_surface(const nv_image* tex)
{
  nv_assert_else_return(tex->width != 0, NULL);
  nv_assert_else_return(tex->height != 0, NULL);
  nv_assert_else_return(tex->format != NOVA_FORMAT_UNDEFINED, NULL);

  SDL_Surface* surface = SDL_CreateSurfaceFrom(
      (int)tex->width, (int)tex->height, (SDL_PixelFormat)nv_format_to_sdl_format(tex->format), tex->data, (int)(tex->width * nv_format_get_bytes_per_pixel(tex->format)));

  return surface;
}

nv_image
_nv_sdl_surface_to_image(SDL_Surface* surface)
{
  nv_assert_else_return(surface->w != 0, nv_zero_init(nv_image));
  nv_assert_else_return(surface->h != 0, nv_zero_init(nv_image));

  SDL_LockSurface(surface);

  const int surface_size_bytes = surface->w * surface->h * SDL_GetPixelFormatDetails(surface->format)->bytes_per_pixel;

  nv_image image;
  image.width  = (size_t)surface->w;
  image.height = (size_t)surface->h;
  nv_assert_else_return(image.width != NOVA_FORMAT_UNDEFINED, nv_zero_init(nv_image));
  nv_assert_else_return(image.height != NOVA_FORMAT_UNDEFINED, nv_zero_init(nv_image));

  image.format = nv_format_from_sdl_format((SDL_Format_)surface->format);
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

  SDL_Surface* surface = _nv_image_to_sdl_surface(tex);

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

  SDL_Surface* surface = _nv_image_to_sdl_surface(tex);

  if (IMG_SaveJPG(surface, path, quality) != 0)
  {
    nv_log_error("Failed in writing image %s. Perhaps its parent directories do not exist?. SDL reports: %s\n", path, SDL_GetError());
  }

  SDL_DestroySurface(surface);
}
