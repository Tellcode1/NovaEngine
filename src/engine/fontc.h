#ifndef __FONTC_H__
#define __FONTC_H__

// implementation: vk.c

#include "../std/semver.h"
#include "../std/stdafx.h"

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
  FONTC_MALLOC_FAILED                     = 2,
  FONTC_COMPRESSION_FAILED                = 3,
  FONTC_DECOMPRESSION_FAILED              = 4,
  FONTC_ATLAS_ERROR                       = 5,
  FONTC_INVALID_CANARY                    = 6,
  FONTC_INVALID_ARGUMENT                  = 7,
  FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG = 0x7fffffff
} fontc_err_t;

struct fontc_file_header_t
{
  u32       magic;
  version_t version;
  u32       pixel_size;
  u32       float_size;
  flt_t     line_height, space_width;
  u32       bmpwidth, bmpheight;
  u32       img_compressed_sz, glyphs_compressed_sz;
  u32       numglyphs;
  u32       magic2;
};

struct fontc_glyph_t
{
  u32   codepoint;
  flt_t advance;
  flt_t x0, x1, y0, y1;
  flt_t l, b, r, t;
};

struct fontc_file_t
{
  fontc_file_header_t header;
  fontc_glyph_t*      glyphs; // numglyphs is in header.
  unsigned char*      bitmap;
};

extern fontc_err_t fontc_read_font(const char* path, fontc_file_t* file);
extern fontc_err_t fontc_bake_font_to_cache(const char* font_path, size_t pixel_size, size_t init_atlas_w, size_t init_atlas_h, size_t num_threads, fontc_file_t* out_file);
extern fontc_err_t fontc_write_font_file(const char* out, fontc_file_t* file);
extern void        fontc_clean_font_file(fontc_file_t* file);

/**
 * An abstraction to fontc_read_font and bake_font().
 * Will bake the file if needed. Load the cache if available.
 * Pass in the path to the font TTF or OTF FILE!!
 */
extern fontc_err_t fontc_load_font(const char* font_source_path, size_t pixel_size, fontc_file_t* f_file);

NOVA_HEADER_END

#endif //__FONTC_H__
