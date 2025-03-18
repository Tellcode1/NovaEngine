#ifndef __FONTC_H__
#define __FONTC_H__

// implementation: vk.c

#include "../containers/rectpack.h"
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
  int       m_magic;
  version_t m_version;
  int       m_pixel_size;
  int       m_float_size;
  flt_t     m_line_height, m_space_width;
  int       m_bmpwidth, m_bmpheight;
  int       m_img_compressed_sz, m_glyphs_compressed_sz;
  int       m_numglyphs;
  int       m_magic2;
};

struct fontc_glyph_t
{
  u32   m_codepoint;
  flt_t m_advance;
  flt_t m_x0, m_x1, m_y0, m_y1;
  flt_t m_l, m_b, m_r, m_t;
};

struct fontc_file_t
{
  fontc_file_header_t m_header;
  fontc_glyph_t*      m_glyphs; // numglyphs is in header.
  unsigned char*      m_bitmap;
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
extern fontc_err_t fontc_load_font(const char* font_source_path, int pixel_size, fontc_file_t* f_file);

NOVA_HEADER_END

#endif //__FONTC_H__
