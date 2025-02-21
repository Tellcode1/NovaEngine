#ifndef __NOVA_IMAGE_H__
#define __NOVA_IMAGE_H__

#include "../std/stdafx.h"
#include "format.h"

NOVA_HEADER_START

typedef struct nv_image_t nv_image_t;

// CPU Image
struct nv_image_t
{
  size_t         w, h;
  nv_format      fmt;
  unsigned char* data;
};

extern nv_image_t nv_image_load(const char* path);
extern nv_image_t nv_image_load_png(const char* path);

// jpg and jpeg (they're the same thing by the way)
extern nv_image_t nv_image_load_jpeg(const char* path);

extern void nv_image_write_(const nv_image_t* tex, const char* path);
extern void nv_image_write_png(const nv_image_t* tex, const char* path);
extern void nv_image_write_jpeg(const nv_image_t* tex, const char* path);

// dst_channels must be greater than src channels!
extern unsigned char* nv_image_pad_channels(const nv_image_t* src, int dst_channels);

// Copy an image on to another.
// Does not modify the src image
extern bool nv_image_overlay(nv_image_t* dest, const nv_image_t* src, int dst_x_offset, int dst_y_offset, int src_x_offset, int src_y_offset);

extern void nv_image_enlarge(nv_image_t* dst, const nv_image_t* src, int scale);

// Does not allocate memory for the dst image, or modify anything except the data buffer of the dst image
// however, dst->w and dst->h is also set by the function
// You can allocate the image with size {.w = src->w / scale, .h = src->h / scale}
extern void nv_image_bilinear_filter(nv_image_t* dst, const nv_image_t* src, flt_t scale);

NOVA_HEADER_END

#endif //__NOVA_IMAGE_H__