/*
  MIT License

  Copyright (c) 2025 Fouzan MD Ishaque (fouzanmdishaque@gmail.com)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef ENGINE_IMAGE_H
#define ENGINE_IMAGE_H

#include "../std/include/attributes.h"
#include "../std/include/errorcodes.h"
#include "../std/include/stdafx.h"
#include "format.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct nv_image nv_image;
  struct SDL_Surface;

  // CPU Image
  struct nv_image
  {
    size_t         width, height;
    nv_format      format;
    unsigned char* data;
  } NOVA_ATTR_ALIGNED(32);

  /**
   * If an error occurs, then a zero initialized image is returned.
   * TODO: Add an error code return
   */
  extern nv_error nv_image_load(const char* path, nv_image* dst);
  extern nv_error nv_image_load_png(const char* path, nv_image* dst);

  // jpg and jpeg (they're the same thing by the way)
  extern nv_image nv_image_load_jpeg(const char* path);

  /**
   * Routines to convert from or to SDL surfaces
   */
  extern struct SDL_Surface* _nv_image_to_sdl_surface(const nv_image* tex);
  extern nv_image            _nv_sdl_surface_to_image(struct SDL_Surface* surface);

  extern void nv_image_write_png(const nv_image* tex, const char* path);
  extern void nv_image_write_jpeg(const nv_image* tex, const char* path, int quality);

  // dst_channels must be greater than src channels!
  extern unsigned char* nv_image_pad_channels(const nv_image* src, size_t dst_channels);

  // Copy an image on to another.
  // Does not modify the src image
  extern bool nv_image_overlay(nv_image* dst, const nv_image* src, int dst_x_offset, int dst_y_offset, int src_x_offset, int src_y_offset);

  extern void nv_image_enlarge(nv_image* dst, const nv_image* src, size_t scale);

  // Does not allocate memory for the dst image, or modify anything except the data buffer of the dst image
  // however, dst->w and dst->h is also set by the function
  // You can allocate the image with size {.w = src->w / scale, .h = src->h / scale}
  extern void nv_image_bilinear_filter(nv_image* dst, const nv_image* src, float scale);

#ifdef __cplusplus
}
#endif

#endif // ENGINE_IMAGE_H
