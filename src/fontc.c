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
#include <string.h>
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
    nv_log_error("PROPS error: %s", error);
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
  fontc_bake_font(input, output, pixel_size, atlas_w, atlas_h, num_threads);
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
    nv_log_error("Failed to open font file for reading: %s", strerror(errno));
    retcode = FONTC_FONT_FILE_NOT_FOUND;
    goto CLEANUP;
  }

  if (fread(&file->header, sizeof(fontc_file_header_t), 1, f) != 1)
  {
    nv_log_error("Failed to read font file header %s", strerror(errno));
    retcode = FONTC_FONT_FILE_NOT_VALID;
    goto CLEANUP;
  }

  if (file->header.magic != FONTC_MAGIC || file->header.magic2 != FONTC_MAGIC)
  {
    nv_log_error("Invalid magic number for font file");
    retcode = FONTC_INVALID_CANARY;
    goto CLEANUP;
  }

  if (file->header.float_size != sizeof(flt_t))
  {
    nv_log_error("Mismatch in float size");
    retcode = FONTC_FONT_FILE_NOT_VALID;
    goto CLEANUP;
  }

  size_t total_glyph_size = file->header.numglyphs * sizeof(fontc_glyph_t);
  if (total_glyph_size > __INT32_MAX__)
  {
    nv_log_error("glyph storage size is > __INT32_MAX__");
    retcode = FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG;
    goto CLEANUP;
  }

  file->glyphs = nv_malloc(total_glyph_size);
  if (!file->glyphs)
  {
    nv_log_error("malloc for glyphs failed %s", strerror(errno));
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  size_t bitmap_size = file->header.bmpwidth * file->header.bmpheight;
  if (bitmap_size > __INT32_MAX__)
  {
    nv_log_error("image storage size is > __INT32_MAX__");
    retcode = FONTC_SOMETHING_HAS_GONE_HORRIBLY_WRONG;
    goto CLEANUP;
  }

  file->bitmap = nv_malloc(bitmap_size);
  if (!file->bitmap)
  {
    nv_log_error("malloc for bitmap failed %s", strerror(errno));
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  compressed_glyphs = nv_malloc(file->header.glyphs_compressed_sz);
  if (!compressed_glyphs)
  {
    nv_log_error("malloc for compressed glyphs failed %s", strerror(errno));
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  compressed_bitmap = nv_malloc(file->header.img_compressed_sz);
  if (!compressed_glyphs)
  {
    nv_log_error("malloc for compressed data failed %s", strerror(errno));
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP;
  }

  if ((int)fread(compressed_glyphs, 1, file->header.glyphs_compressed_sz, f) != file->header.glyphs_compressed_sz)
  {
    nv_log_error("Failed to read compressed glyph data %s", strerror(errno));
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP;
  }

  if ((int)fread(compressed_bitmap, 1, file->header.img_compressed_sz, f) != file->header.img_compressed_sz)
  {
    nv_log_error("Failed to read compressed bitmap %s", strerror(errno));
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP;
  }

  if (nv_bufdecompress(compressed_glyphs, file->header.glyphs_compressed_sz, file->glyphs, total_glyph_size) < 0)
  {
    nv_log_error("Error decompressing glyphs");
    retcode = FONTC_DECOMPRESSION_FAILED;
    goto CLEANUP;
  }

  if (nv_bufdecompress(compressed_bitmap, file->header.img_compressed_sz, file->bitmap, bitmap_size) < 0)
  {
    nv_log_error("Error decompressing bitmap");
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
fontc_bake_font(const char* font_path, const char* out, int pixel_size, int init_atlas_w, int init_atlas_h, int num_threads)
{
  nv_assert(font_path != NULL);
  nv_assert(out != NULL);
  nv_assert(pixel_size > 0);
  nv_assert(init_atlas_w > 0);
  nv_assert(init_atlas_h > 0);
  nv_assert(num_threads > 0);

  fontc_err_t retcode = FONTC_SUCCESS;

  bool           freetype_library_is_open = 0;
  fontc_glyph_t* glyphs                   = NULL;
  unsigned char* compressed_image         = NULL;
  fontc_glyph_t* compressed_glyphs        = NULL;
  FILE*          f                        = NULL;

  FT_Library lib;
  FT_Face    face;
  if (FT_Init_FreeType(&lib))
  {
    nv_log_error("Failed to initialize ft");
    retcode = FONTC_FREETYPE_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  freetype_library_is_open = 1;

  if (FT_New_Face(lib, font_path, 0, &face))
  {
    retcode = FONTC_FONT_FILE_NOT_VALID;
    nv_log_error("Failed to load font file: %s", font_path);
    goto CLEANUP_AND_RETURN;
  }
  FT_Set_Pixel_Sizes(face, 0, pixel_size);

  fontc_file_t       file = nv_zero_init(fontc_file_t);
  nv_texture_atlas_t atlas;
  nv_texture_atlas_init(&atlas, init_atlas_w, init_atlas_h, NOVA_FORMAT_R8, 4);

  file.header.magic       = FONTC_MAGIC;
  file.header.magic2      = FONTC_MAGIC;
  file.header.line_height = -face->size->metrics.height / (flt_t)face->height;

  int glyph_alloc_size = 256;
  glyphs               = nv_malloc(sizeof(fontc_glyph_t) * glyph_alloc_size);
  if (!glyphs)
  {
    nv_log_error("Failed to allocate memory for glyphs");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
  }

  int glyph_count = 0;

  omp_set_num_threads(num_threads);
  nv_log_info("Using %i threads", num_threads);

#pragma omp parallel
  {
    FT_Face thread_face;
    if (FT_New_Face(lib, font_path, 0, &thread_face))
    {
      nv_log_error("Failed to load font file: %s", font_path);
#pragma omp atomic write
      retcode = FONTC_FREETYPE_ERROR;
    }

    FT_Set_Pixel_Sizes(thread_face, 0, pixel_size);

    fontc_glyph_t local_glyphs[256];
    int           local_count = 0;

#pragma omp for schedule(dynamic)
    for (int i = 0; i < 256; i++)
    {
      FT_UInt glyph_index = FT_Get_Char_Index(thread_face, i);
      if (glyph_index == 0) { continue; }

      if (FT_Load_Glyph(thread_face, glyph_index, FT_LOAD_DEFAULT)) continue;

      if (i == ' ')
      {
#pragma omp critical
        file.header.space_width = (flt_t)thread_face->glyph->metrics.horiAdvance / (flt_t)thread_face->units_per_EM;
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
          nv_log_error("Atlas error");
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

    FT_Done_Face(thread_face);
  }

  nv_log_info("final atlas size w=%i h=%i (uncompressed %b)", atlas.w, atlas.h, atlas.w * atlas.h * nv_format_get_bytes_per_pixel(atlas.fmt));

  nv_texture_atlas_finish(&atlas);

  const flt_t atlas_w = atlas.w, atlas_h = atlas.h;
  const flt_t units_per_em = (flt_t)face->units_per_EM;
  if (atlas_w == 0.0 || atlas_h == 0.0) { retcode = FONTC_ATLAS_ERROR; }

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

  file.header.bmpwidth  = atlas_w;
  file.header.bmpheight = atlas_h;
  file.header.numglyphs = glyph_count;

  size_t image_o_size = atlas_w * atlas_h * nv_format_get_bytes_per_pixel(atlas.fmt);
  size_t glyph_o_size = file.header.numglyphs * sizeof(fontc_glyph_t);

  compressed_image = nv_malloc(image_o_size);
  if (!compressed_image)
  {
    nv_log_error("malloc failed for compressed image");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }

  compressed_glyphs = nv_malloc(glyph_o_size);
  if (!compressed_glyphs)
  {
    nv_log_error("malloc failed for compressed glyphs");
    retcode = FONTC_MEMORY_ALLOCATION_FAILED;
    goto CLEANUP_AND_RETURN;
  }

  nv_bufcompress(atlas.data, image_o_size, compressed_image, &image_o_size);
  nv_bufcompress(glyphs, glyph_o_size, compressed_glyphs, &glyph_o_size);

  size_t old_img_size = atlas_w * atlas_h * nv_format_get_bytes_per_pixel(atlas.fmt);
  nv_log_info("atlas size compressed from %b to %b (-%.2f%%)", old_img_size, image_o_size, (1.0f - (image_o_size / (flt_t)old_img_size)) * 100.0f);

  file.glyphs = compressed_glyphs;

  file.header.img_compressed_sz    = image_o_size;
  file.header.glyphs_compressed_sz = glyph_o_size;

  file.header.version    = nv_semver_pack_version(FONTC_VERSION_MAJOR, FONTC_VERSION_MINOR, FONTC_VERSION_PATCH);
  file.header.float_size = sizeof(flt_t);

  f = fopen(out, "wb");
  if (!f)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  if (fwrite(&file.header, sizeof(fontc_file_header_t), 1, f) != 1)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }
  if (fwrite(compressed_glyphs, glyph_o_size, 1, f) != 1)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }
  if (fwrite(compressed_image, image_o_size, 1, f) != 1)
  {
    retcode = FONTC_OTHER_IO_ERROR;
    goto CLEANUP_AND_RETURN;
  }

  size_t bytes_written = 0;

  bytes_written += sizeof(fontc_file_header_t);
  bytes_written += glyph_o_size;
  bytes_written += image_o_size;

  char buf[128];
  nv_btoa(bytes_written, 1, buf, 127);
  buf[127] = 0;
  nv_log_info("Wrote %s to %s", buf, out);
  nv_log_info("Here's a summary of what was written:");
  nv_log_info("file header: %b of %b (%.2f%%)", sizeof(fontc_file_header_t), bytes_written, (sizeof(fontc_file_header_t) / (flt_t)bytes_written) * 100.0);
  nv_log_info("glyph vertices: %b of %b (%.2f%%)", glyph_o_size, bytes_written, (glyph_o_size / (flt_t)bytes_written) * 100.0);
  nv_log_info("the bitmap: %b of %b (%.2f%%)", image_o_size, bytes_written, (image_o_size / (flt_t)bytes_written) * 100.0);

CLEANUP_AND_RETURN:

  if (f) nv_safecall_c_fn(fclose(f));

  if (glyphs) nv_free(glyphs);

  if (compressed_image) nv_free(compressed_image);
  if (compressed_glyphs) nv_free(compressed_glyphs);

  if (freetype_library_is_open)
  {
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    nv_texture_atlas_destroy(&atlas);
  }

  return retcode;
}

void
fontc_clean_font_file(fontc_file_t* file)
{
  nv_free(file->glyphs);
  nv_free(file->bitmap);
}