#ifndef __FONTC_H__
#define __FONTC_H__

#include "../../common/stdafx.h"
#include <stdbool.h>
#include <stdio.h>

NOVA_HEADER_START;

#define FONTC_MAGIC 2222022
#define NVM_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NVM_MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct fontc_atlas_t {
  int width, height, next_x, next_y, current_row_height;
  unsigned char *data;
} fontc_atlas_t;

typedef struct fontc_header {
  int magic;
  float line_height, space_width;
  int bmpwidth, bmpheight;
  int img_compressed_sz, glyphs_compressed_sz;
  int numglyphs;
} fontc_header;

typedef struct fontc_glyph {
  unsigned codepoint;
  float advance;
  float x0, x1, y0, y1;
  float l, b, r, t;
} fontc_glyph;

typedef struct fontc_file_t {
  fontc_header header;
  fontc_glyph *glyphs; // numglyphs is in header.
  unsigned char *bitmap;
} fontc_file_t;

extern fontc_atlas_t fontc_atlas_init(int init_w, int init_h);
extern bool fontc_atlas_add_image(fontc_atlas_t *__restrict__ atlas, int w, int h, const unsigned char *__restrict__ data, int *__restrict__ x,
                                  int *__restrict__ y);
extern void fontc_read_font(const char *path, fontc_file_t *file);
extern void fontc_bake_font(const char *font_path, const char *out);

NOVA_HEADER_END;

#endif //__FONTC_H__