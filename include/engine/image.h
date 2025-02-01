#ifndef __NOVA_IMAGE_H__
#define __NOVA_IMAGE_H__

#include "../../common/stdafx.h"
#include "../GPU/format.h"

NOVA_HEADER_START;

// CPU Image
typedef struct nv_image {
  int w, h;
  nv_format fmt;
  unsigned char *data;
} nv_image;

extern nv_image nv_img_load(const char *path);
extern nv_image nv_img_load_png(const char *path);

// jpg and jpeg (they're the same thing by the way)
extern nv_image nv_img_load_jpeg(const char *path);

extern void nv_img_write_(const nv_image *tex, const char *path);
extern void nv_img_write_png(const nv_image *tex, const char *path);
extern void nv_img_write_jpeg(const nv_image *tex, const char *path);

// dst_channels must be greater than src channels!
unsigned char *nv_img_pad_channels(const nv_image *src, int dst_channels);

NOVA_HEADER_END;

#endif //__NOVA_IMAGE_H__