#ifndef __FONTC_H__
#define __FONTC_H__

// implementation: vk.c

#include "../../common/rectpack.h"
#include "../../std/stdafx.h"
#include <stdbool.h>
#include <stdio.h>

NOVA_HEADER_START;

#define FONTC_MAGIC 0xDED

typedef struct fontc_file_t        fontc_file_t;
typedef struct fontc_file_header_t fontc_file_header_t;
typedef struct fontc_glyph_t       fontc_glyph_t;

struct fontc_file_header_t
{
  int   magic;
  float line_height, space_width;
  int   bmpwidth, bmpheight;
  int   img_compressed_sz, glyphs_compressed_sz;
  int   numglyphs;
};

struct fontc_glyph_t
{
  unsigned codepoint;
  float    advance;
  float    x0, x1, y0, y1;
  float    l, b, r, t;
};

struct fontc_file_t
{
  fontc_file_header_t header;
  fontc_glyph_t*      glyphs; // numglyphs is in header.
  unsigned char*      bitmap;
};

extern void fontc_read_font(const char* path, fontc_file_t* file);
extern void fontc_bake_font(const char* font_path, const char* out, int pixel_size, int atlas_w, int atlas_h, int num_threads);

NOVA_HEADER_END;

#endif //__FONTC_H__