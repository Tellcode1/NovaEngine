#include "../std/stdafx.h"
#include "../std/strconv.h"
#include "../std/string.h"

#include <errno.h>

#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/engine/fontc.h"

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include "../common/containers/atlas.h"

#if defined(FONTC_EXECUTABLE)
#  include "../std/print.h"
#  include "../std/props.h"
#  include "../std/timer.h"

int
main(int argc, char* argv[])
{
  char input[256]  = "No path given";
  char output[256] = "bakedfont";

  int  pixel_size = 128;
  bool help       = 0;
  int  atlas_w = 256, atlas_h = 256;
  int  num_threads = 4;
  // clang-format off
  nv_option_t options[] = {
    { NV_OP_TYPE_STRING, "i", "input", input, sizeof(input) },
    { NV_OP_TYPE_STRING, "o", "output", output, sizeof(output) },
    { NV_OP_TYPE_INT, "p", "pixel-size", &pixel_size, 0 },
    { NV_OP_TYPE_INT, "aw", "atlas-width", &atlas_w, 0 },
    { NV_OP_TYPE_INT, "ah", "atlas-height", &atlas_h, 0 },
    { NV_OP_TYPE_INT, "t", "threads", &num_threads, 0 },
    { NV_OP_TYPE_BOOL, "h", "help", &help, 0 }
  };
  // clang-format on

  char error[256];
  if (nv_props_parse(argc, argv, options, nv_arrlen(options), error, sizeof(error)) == -1)
  {
    nv_push_error("PROPS error: %s", error);
    help = 1;
  }

  if (help)
  {
    nv_props_gen_help(options, nv_arrlen(options), error, sizeof(error));
    nv_printf("%s", error);
    return 0;
  }

  nv_log_info("read:  %s", input);
  nv_log_info("write: %s", output);
  nv_log_info("pixel size: %i", pixel_size);
  nv_log_info("atlas size: w=%i h=%i", atlas_w, atlas_h);

  timer tm = timer_begin(__FLT_MAX__);

  fontc_file_t font_file = nv_zero_init(fontc_file_t);
  fontc_bake_font_to_cache(input, pixel_size, atlas_w, atlas_h, num_threads, &font_file);
  fontc_write_font_file(output, &font_file);
  fontc_clean_font_file(&font_file);
  nv_log_info("finished in %.2f s", timer_time_since_start(&tm));
  return 0;
}

#endif // FONTC_EXECUTABLE

fontc_err_t
fontc_read_font(const char* path, fontc_file_t* file)
{
  nv_assert(path != NULL);
  nv_assert(file != NULL);

  fontc_err_t    retcode           = FONTC_SUCCESS;
  FILE*          f                 = NULL;
  fontc_glyph_t* compressed_glyphs = NULL;
  uchar*         compressed_bitmap = NULL;

  nv_memset(file, 0, sizeof(fontc_file_t));

  f = fopen(path, "rb");
  if (!f)
  {
    retcode = FONTC_FONT_FILE_NOT_FOUND;
    goto CLEANUP;
  }

  if (fread(&file->header, sizeof(fontc_file_header_t), 1, f) != 1)
  {
    retcode = FONTC_FONT_FILE_NOT_VALID;
    goto CLEANUP;
  }

  if (file->header.magic != FONTC_MAGIC || file->header.magic2 != FONTC_MAGIC)
  {
    retcode = FONTC_INVALID_CANARY;
    goto CLEANUP;
  }

  if (file->header.float_size != sizeof(flt_t))
  {
    retcode = FONTC_FONT_FILE_NOT_VALID;
    goto CLEANUP;
  }

  size_t total_glyph_size = file->header.numglyphs * sizeof(fontc_glyph_t);
  if (total_glyph_size > __INT32_MAX__)
  {
    retcode = FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG;
    goto CLEANUP;
  }

  file->glyphs = nv_malloc(total_glyph_size);
  if (!file->glyphs)
  {
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  size_t bitmap_size = file->header.bmpwidth * file->header.bmpheight;
  if (bitmap_size > __INT32_MAX__)
  {
    nv_push_error("image storage size is > __INT32_MAX__");
    retcode = FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG;
    goto CLEANUP;
  }

  file->bitmap = nv_malloc(bitmap_size);
  if (!file->bitmap)
  {
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  compressed_glyphs = nv_malloc(file->header.glyphs_compressed_sz);
  if (!compressed_glyphs)
  {
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  compressed_bitmap = nv_malloc(file->header.img_compressed_sz);
  if (!compressed_glyphs)
  {
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  if ((int)fread(compressed_glyphs, 1, file->header.glyphs_compressed_sz, f) != file->header.glyphs_compressed_sz)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP;
  }

  if ((int)fread(compressed_bitmap, 1, file->header.img_compressed_sz, f) != file->header.img_compressed_sz)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP;
  }

  if (nv_bufdecompress(compressed_glyphs, file->header.glyphs_compressed_sz, file->glyphs, total_glyph_size) < 0)
  {
    retcode = FONTC_DECOMPRESSION_FAILED;
    goto CLEANUP;
  }

  if (nv_bufdecompress(compressed_bitmap, file->header.img_compressed_sz, file->bitmap, bitmap_size) < 0)
  {
    retcode = FONTC_DECOMPRESSION_FAILED;
    goto CLEANUP;
  }

CLEANUP:
  if (f) nv_safecall_c_fn(fclose(f));
  if (compressed_glyphs) nv_free(compressed_glyphs);
  if (compressed_bitmap) nv_free(compressed_bitmap);
  return retcode;
}

fontc_err_t
fontc_bake_font_to_cache(const char* font_path, int pixel_size, int init_atlas_w, int init_atlas_h, int num_threads, fontc_file_t* out_file)
{
  nv_assert(font_path != NULL);
  nv_assert(out_file != NULL);
  nv_assert(pixel_size > 0);
  nv_assert(init_atlas_w > 0);
  nv_assert(init_atlas_h > 0);
  nv_assert(num_threads > 0);

  fontc_err_t    retcode                  = FONTC_SUCCESS;
  FT_Face*       faces                    = NULL;
  bool           freetype_library_is_open = false;
  fontc_glyph_t* glyphs                   = NULL;

  unsigned char* atlas_image = NULL;

  FT_Library lib;
  FT_Face    face;
  if (FT_Init_FreeType(&lib))
  {
    nv_push_error("Failed to initialize ft");
    retcode = FONTC_FREETYPE_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  freetype_library_is_open = 1;

  if (FT_New_Face(lib, font_path, 0, &face))
  {
    retcode = FONTC_FONT_FILE_NOT_VALID;
    nv_push_error("Failed to load font file: %s", font_path);
    goto CLEANUP_AND_RETURN;
  }
  FT_Set_Pixel_Sizes(face, 0, pixel_size);

  *out_file = nv_zero_init(fontc_file_t);
  nv_texture_atlas_t atlas;
  nv_texture_atlas_init(&atlas, init_atlas_w, init_atlas_h, NOVA_FORMAT_R8, 4);

  out_file->header.magic       = FONTC_MAGIC;
  out_file->header.magic2      = FONTC_MAGIC;
  out_file->header.line_height = -face->size->metrics.height / (flt_t)face->height;

  int glyph_alloc_size = 256;
  glyphs               = nv_malloc(sizeof(fontc_glyph_t) * glyph_alloc_size);
  if (!glyphs)
  {
    nv_push_error("Failed to allocate memory for glyphs");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }

  int glyph_count = 0;

  omp_set_num_threads(num_threads);
  nv_log_info("Using %i threads", num_threads);

  faces = nv_malloc(sizeof(FT_Face) * num_threads);
  if (!faces)
  {
    nv_push_error("malloc faces failed");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }

#pragma omp parallel
  {
    if (FT_New_Face(lib, font_path, 0, &faces[omp_get_thread_num()]))
    {
      nv_push_error("Failed to load font file: %s", font_path);
#pragma omp atomic write
      retcode = FONTC_FREETYPE_ERROR;
      // Skip further processing in this thread.
      // continue;
    }
    FT_Face thread_face = faces[omp_get_thread_num()];

    FT_Set_Pixel_Sizes(thread_face, 0, pixel_size);
    fontc_glyph_t local_glyphs[256];
    int           local_count = 0;

#pragma omp for schedule(dynamic)
    for (int i = 0; i < 256; i++)
    {
      FT_UInt glyph_index = FT_Get_Char_Index(thread_face, i);
      if (glyph_index == 0) { continue; }

      if (FT_Load_Glyph(thread_face, glyph_index, FT_LOAD_DEFAULT)) { continue; }

      if (i == ' ')
      {
#pragma omp critical
        out_file->header.space_width = (flt_t)thread_face->glyph->metrics.horiAdvance / (flt_t)thread_face->units_per_EM;
      }

      FT_Render_Glyph(thread_face->glyph, FT_RENDER_MODE_SDF);
      FT_GlyphSlot g = thread_face->glyph;

      const int            w      = g->bitmap.width;
      const int            h      = g->bitmap.rows;
      const unsigned char* buffer = g->bitmap.buffer;

      size_t x = SIZE_MAX, y = SIZE_MAX;
      if (buffer)
      {
#pragma omp critical
        if (!nv_texture_atlas_add(&atlas, &(nv_image_t){ .w = w, .h = h, .fmt = NOVA_FORMAT_R8, .data = (unsigned char*)buffer }, &x, &y))
        {
          nv_push_error("Atlas error");
#pragma omp atomic write
          retcode = FONTC_ATLAS_ERROR;
        }
      }

      FT_Glyph gl;
      FT_Get_Glyph(thread_face->glyph, &gl);

      FT_BBox box;
      FT_Glyph_Get_CBox(gl, FT_GLYPH_BBOX_UNSCALED, &box);
      FT_Done_Glyph(gl);

      local_glyphs[local_count++] = (fontc_glyph_t){
        .codepoint = i,
        .x0        = box.xMin,
        .x1        = box.xMax,
        .y0        = box.yMin,
        .y1        = box.yMax,
        .l         = x,
        .r         = x + w,
        .b         = y + h,
        .t         = y,
        .advance   = thread_face->glyph->metrics.horiAdvance,
      };
    }

#pragma omp critical
    {
      if (local_count > 0) nv_memcpy(&glyphs[glyph_count], local_glyphs, local_count * sizeof(fontc_glyph_t));
      glyph_count += local_count;
    }
  }

  for (int i = 0; i < num_threads; i++)
  {
    FT_Done_Face(faces[i]);
  }

  nv_log_info("final atlas size w=%i h=%i (uncompressed %b)", atlas.w, atlas.h, atlas.w * atlas.h * nv_format_get_bytes_per_pixel(atlas.fmt));

  nv_texture_atlas_finish(&atlas);

  const flt_t atlas_w = atlas.w, atlas_h = atlas.h;
  const flt_t units_per_em = (flt_t)face->units_per_EM;
  if (atlas_w == 0.0 || atlas_h == 0.0)
  {
    retcode = FONTC_ATLAS_ERROR;
    goto CLEANUP_AND_RETURN;
  }
  nv_assert(units_per_em != 0.0);

#pragma omp parallel for
  for (int i = 0; i < glyph_count; i++)
  {
    fontc_glyph_t* glyph = &glyphs[i];
    glyph->x0 /= units_per_em;
    glyph->x1 /= units_per_em;
    glyph->y0 /= units_per_em;
    glyph->y1 /= units_per_em;
    glyph->l /= atlas_w;
    glyph->r /= atlas_w;
    glyph->b /= atlas_h;
    glyph->t /= atlas_h;
    glyph->advance /= units_per_em;
  }
  nv_log_info("%i glyphs processed", glyph_count);

  out_file->header.bmpwidth  = atlas_w;
  out_file->header.bmpheight = atlas_h;
  out_file->header.numglyphs = glyph_count;

  size_t image_size                     = atlas.w * atlas.h * nv_format_get_bytes_per_pixel(atlas.fmt);
  size_t glyphs_size                    = out_file->header.numglyphs * sizeof(fontc_glyph_t);
  out_file->header.img_compressed_sz    = image_size; // uncompressed size stored here for cache
  out_file->header.glyphs_compressed_sz = glyphs_size;

  out_file->glyphs = glyphs;
  glyphs           = NULL;

  atlas_image = nv_malloc(image_size);
  if (!atlas_image)
  {
    nv_push_error("Failed to allocate memory for atlas image");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }
  nv_memcpy(atlas_image, atlas.data, image_size);
  out_file->bitmap = atlas_image;

CLEANUP_AND_RETURN:
  if (freetype_library_is_open)
  {
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    nv_texture_atlas_destroy(&atlas);
  }
  if (faces) { nv_free(faces); }
  return retcode;
}

fontc_err_t
fontc_write_font_file(const char* out, const fontc_file_t* file)
{
  nv_assert_and_ret(out != NULL, FONTC_INVALID_ARGUMENT);
  nv_assert_and_ret(file != NULL, FONTC_INVALID_ARGUMENT);
  nv_assert_and_ret(file->bitmap != NULL, FONTC_INVALID_ARGUMENT);
  nv_assert_and_ret(file->glyphs != NULL, FONTC_INVALID_ARGUMENT);
  nv_assert_and_ret(file->header.magic != FONTC_MAGIC, FONTC_INVALID_ARGUMENT);
  nv_assert_and_ret(file->header.magic2 != FONTC_MAGIC, FONTC_INVALID_ARGUMENT);

  fontc_err_t retcode = FONTC_SUCCESS;

  unsigned char* compressed_image  = NULL;
  fontc_glyph_t* compressed_glyphs = NULL;
  FILE*          f                 = NULL;

  f = fopen(out, "wb");
  if (!f)
  {
    nv_push_error("Failed to open output file: %s", out);
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  size_t uncompressed_image_size  = file->header.img_compressed_sz;
  size_t uncompressed_glyphs_size = file->header.glyphs_compressed_sz;

  compressed_image = nv_malloc(uncompressed_image_size);
  if (!compressed_image)
  {
    nv_push_error("Failed to allocate memory for compressed image");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }
  size_t compressed_image_size = uncompressed_image_size;
  nv_bufcompress(file->bitmap, uncompressed_image_size, compressed_image, &compressed_image_size);

  compressed_glyphs = nv_malloc(uncompressed_glyphs_size);
  if (!compressed_glyphs)
  {
    nv_push_error("Failed to allocate memory for compressed glyphs");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }
  size_t compressed_glyphs_size = uncompressed_glyphs_size;
  nv_bufcompress(file->glyphs, uncompressed_glyphs_size, compressed_glyphs, &compressed_glyphs_size);

  fontc_file_header_t header  = file->header;
  header.img_compressed_sz    = compressed_image_size;
  header.glyphs_compressed_sz = compressed_glyphs_size;
  header.version              = nv_semver_pack_version(FONTC_VERSION_MAJOR, FONTC_VERSION_MINOR, FONTC_VERSION_PATCH);
  header.float_size           = sizeof(flt_t);

  if (fwrite(&header, sizeof(fontc_file_header_t), 1, f) != 1)
  {
    nv_push_error("Failed to write header");
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }
  if (fwrite(compressed_glyphs, compressed_glyphs_size, 1, f) != 1)
  {
    nv_push_error("Failed to write glyph data");
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }
  if (fwrite(compressed_image, compressed_image_size, 1, f) != 1)
  {
    nv_push_error("Failed to write image data");
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  nv_log_info("wrotebaked font to %s", out);

CLEANUP_AND_RETURN:
  if (f) { nv_free(f); }
  if (compressed_image) { nv_free(compressed_image); }
  if (compressed_glyphs) { nv_free(compressed_glyphs); }
  return retcode;
}

void
fontc_clean_font_file(fontc_file_t* file)
{
  if (file->glyphs) { nv_free(file->glyphs); }
  if (file->bitmap) { nv_free(file->bitmap); }
}

fontc_err_t
fontc_load_font(const char* font_source_path, fontc_file_t* font_file)
{
  char buf[256] = { 0 };
  nv_strcat_max(buf, font_source_path, 256);
  nv_strcat_max(buf, ".bkd", 256);

  *font_file = nv_zero_init(fontc_file_t);

  if (fontc_read_font(buf, font_file) != FONTC_SUCCESS)
  {
    fontc_err_t retcode = fontc_bake_font_to_cache(font_source_path, 256, 1024, 1024, 4, font_file);
    fontc_write_font_file(buf, font_file);
    return retcode;
  }
  return FONTC_SUCCESS;
}