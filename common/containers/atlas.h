#ifndef __NOVA_ATLAS_H__
#define __NOVA_ATLAS_H__

#include "../../std/stdafx.h"
#include "../format.h"
#include "../image.h"
#include "../rectpack.h"
#include <pthread.h>

NOVA_HEADER_START

// Possibly add features for removing textures?

typedef struct nv_texture_atlas_t nv_texture_atlas_t;

struct nv_texture_atlas_t
{
  unsigned char*   data;
  size_t           w, h;
  nv_format        fmt;
  int              padding;
  nv_skyline_bin_t bin;
  pthread_mutex_t  mutex;
};

extern void nv_texture_atlas_init(nv_texture_atlas_t* atlas, size_t width, size_t height, nv_format fmt, int padding);

// Returns false if the image was not packed
extern int nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image_t* img, size_t* out_x, size_t* out_y);

extern void nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale);

// Resizes the atlas to only fit the current amount of glyphs
// returns 1 (true) if the atlas was truncated.
// this need not be checked.
extern int nv_texture_atlas_finish(nv_texture_atlas_t* atlas);

extern void nv_texture_atlas_destroy(nv_texture_atlas_t* atlas);

NOVA_HEADER_END

#endif //__NOVA_ATLAS_H__