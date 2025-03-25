#ifndef __NOVA_ATLAS_H__
#define __NOVA_ATLAS_H__

#include "../common/format.h"
#include "../common/image.h"
#include "../std/stdafx.h"
#include "freelist.h"
#include "rectpack.h"
#include <SDL2/SDL_mutex.h>

NOVA_HEADER_START

// Possibly add features for removing textures?

typedef struct nv_texture_atlas_t nv_texture_atlas_t;

extern void nv_texture_atlas_init(size_t width, size_t height, nv_format fmt, int padding, nv_texture_atlas_t* dst);

// Returns false if the image was not packed
extern int nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image_t* img, size_t* out_x, size_t* out_y);

extern void nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale);

// Resizes the atlas to only fit the current amount of glyphs
// returns 1 (true) if the atlas was truncated.
// this need not be checked.
extern int nv_texture_atlas_finish(nv_texture_atlas_t* atlas);

extern void nv_texture_atlas_destroy(nv_texture_atlas_t* atlas);

struct nv_texture_atlas_t
{
  unsigned  canary; // = 0xDEADBEEF
  int       padding;
  nv_format format;

  nv_allocator_t* alloc;

  SDL_mutex* mutex;

  unsigned char* data;

  size_t width;
  size_t height;

  nv_skyline_bin_t bin;
};

NOVA_HEADER_END

#endif //__NOVA_ATLAS_H__
