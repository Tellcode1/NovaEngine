#ifndef __FONTC_H__
#define __FONTC_H__

// implementation: vk.c

#include "../../common/rectpack.h"
#include "../../std/semver.h"
#include "../../std/stdafx.h"
#include <stdbool.h>
#include <stdio.h>

NOVA_HEADER_START

#define FONTC_MAGIC 0xDED

#define FONTC_VERSION_MAJOR 0
#define FONTC_VERSION_MINOR 0
#define FONTC_VERSION_PATCH 0

typedef struct fontc_file_t        fontc_file_t;
typedef struct fontc_file_header_t fontc_file_header_t;
typedef struct fontc_glyph_t       fontc_glyph_t;

typedef enum fontc_err_t
{
  FONTC_SUCCESS                           = 0,
  FONTC_FONT_FILE_NOT_FOUND               = -3,
  FONTC_FONT_FILE_NOT_VALID               = -2,
  FONTC_OTHER_IO_ERROR                    = -1,
  FONTC_FREETYPE_ERROR                    = 1,
  FONTC_MEMORY_ALLOCATION_FAILED          = 2,
  FONTC_COMPRESSION_FAILED                = 3,
  FONTC_DECOMPRESSION_FAILED              = 4,
  FONTC_ATLAS_ERROR                       = 5,
  FONTC_INVALID_CANARY                    = 6,
  FONTC_INVALID_ARGUMENT                  = 7,
  FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG = 0x7fffffff
} fontc_err_t;

struct fontc_file_header_t
{
  int       magic;
  version_t version;
  int       float_size;
  flt_t     line_height, space_width;
  int       bmpwidth, bmpheight;
  int       img_compressed_sz, glyphs_compressed_sz;
  int       numglyphs;
  int       magic2;
};

struct fontc_glyph_t
{
  unsigned codepoint;
  flt_t    advance;
  flt_t    x0, x1, y0, y1;
  flt_t    l, b, r, t;
};

struct fontc_file_t
{
  fontc_file_header_t header;
  fontc_glyph_t*      glyphs; // numglyphs is in header.
  unsigned char*      bitmap;
};

extern fontc_err_t fontc_read_font(const char* path, fontc_file_t* file);
extern fontc_err_t fontc_bake_font_to_cache(const char* font_path, int pixel_size, int init_atlas_w, int init_atlas_h, int num_threads, fontc_file_t* out_file);
extern fontc_err_t fontc_write_font_file(const char* out, fontc_file_t* file);
extern void        fontc_clean_font_file(fontc_file_t* file);

/**
 * An abstraction to fontc_read_font and bake_font().
 * Will bake the file if needed. Load the cache if available.
 * Pass in the path to the font TTF or OTF FILE!!
 */
extern fontc_err_t fontc_load_font(const char* font_source_path, fontc_file_t* file);

NOVA_HEADER_END

#endif //__FONTC_H__