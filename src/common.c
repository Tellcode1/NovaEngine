#include <SDL2/SDL.h>
#include <SDL2/SDL_mutex.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/image.h"
#include "common/mem.h"
#include "containers/atlas.h"
#include "containers/bitset.h"
#include "containers/freelist.h"
#include "containers/hashmap.h"
#include "containers/list.h"
#include "containers/pool.h"
#include "containers/rbmap.h"
#include "containers/string.h"
#include "std/math/math.h"

static inline void*
align_up(void* ptr, size_t alignment)
{
  nv_assert((alignment & (alignment - 1)) == 0 && "only pwoer of two alignment supported");
  size_t offset = alignment - ((uintptr_t)ptr % alignment);
  return (void*)((char*)ptr + (offset % alignment));
}

static inline size_t
align_up_size(size_t size, size_t alignment)
{
  nv_assert((alignment & (alignment - 1)) == 0 && "only pwoer of two alignment supported");
  return (size + alignment - 1) & ~(alignment - 1);
}

// nv_image_t
#include <jpeglib.h>
#include <png.h>

const char*
get_file_extension(const char* path)
{
  const char* dot = nv_strrchr(path, '.');
  // Imagine someone actually uses this project.
  // And then they see this.
  if (!dot || dot == path)
  {
    return "piss";
  }
  return dot + 1;
}

nv_image_t
nv_image_load(const char* path)
{
  const char* ext = get_file_extension(path);
  if (nv_strcmp(ext, "jpeg") == 0 || nv_strcmp(ext, "jpg") == 0)
  {
    return nv_image_load_jpeg(path);
  }
  else if (nv_strcmp(ext, "png") == 0)
  {
    return nv_image_load_png(path);
  }
  nv_assert(0);
  return nv_zero_init(nv_image_t);
}

unsigned char*
nv_image_pad_channels(const nv_image_t* src, int dst_channels)
{
  const int src_channels = nv_format_get_num_channels(src->m_format);
  nv_assert(src_channels < dst_channels);

  uint8_t* dst = nv_calloc(src->m_width * src->m_height * dst_channels * sizeof(uchar));

  for (size_t y = 0; y < src->m_height; y++)
  {
    for (size_t x = 0; x < src->m_width; x++)
    {
      for (int c = 0; c < dst_channels; c++)
      {
        if (c < src_channels)
        {
          dst[(((y * src->m_width) + x) * dst_channels) + c] = src->m_data[(((y * src->m_width) + x) * src_channels) + c];
        }
        else
        {
          if (c == 3)
          { // alpha channel
            dst[(((y * src->m_width) + x) * dst_channels) + c] = __UINT8_MAX__;
          }
          else
          {
            dst[(((y * src->m_width) + x) * dst_channels) + c] = 0;
          }
        }
      }
    }
  }

  return dst;
}

bool
nv_image_overlay(nv_image_t* dst, const nv_image_t* src, int dst_x_offset, int dst_y_offset, int src_x_offset, int src_y_offset)
{
  nv_assert(dst != NULL);
  nv_assert(src != NULL);

  const int src_channels = nv_format_get_num_channels(src->m_format);

  for (ssize_t y = src_y_offset; y < (ssize_t)src->m_height; y++)
  {
    for (ssize_t x = src_x_offset; x < (ssize_t)src->m_width; x++)
    {
      ssize_t dst_x = dst_x_offset + (x - src_x_offset);
      ssize_t dst_y = dst_y_offset + (y - src_y_offset);

      if (dst_x >= 0 && dst_x < (ssize_t)dst->m_width && dst_y >= 0 && dst_y < (ssize_t)dst->m_height)
      {
        size_t src_i = (y * src->m_width + x) * src_channels;
        size_t dst_i = (dst_y * dst->m_width + dst_x) * src_channels;

        for (int c = 0; c < src_channels; c++)
        {
          dst->m_data[dst_i + c] = src->m_data[src_i + c];
        }
      }
    }
  }

  return 0;
}

void
nv_image_enlarge(nv_image_t* dst, const nv_image_t* src, int scale)
{
  size_t new_w = src->m_width * scale;

  nv_assert(dst->m_data != NULL);

  uchar*       write = dst->m_data;
  const uchar* read  = src->m_data;

  int bpp = nv_format_get_bytes_per_pixel(src->m_format); // bytes per pixel
  for (size_t y = 0; y < src->m_height; y++)
  {
    for (size_t x = 0; x < src->m_width; x++)
    {
      size_t src_i = (y * src->m_width + x) * bpp;
      for (int i = 0; i < scale; i++)
      {
        for (int j = 0; j < scale; j++)
        {
          size_t dst_i = ((y * scale + i) * new_w + (x * scale + j)) * bpp;
          for (int c = 0; c < bpp; c++)
          {
            write[dst_i + c] = read[src_i + c];
          }
        }
      }
    }
  }
}

void
nv_image_bilinear_filter(nv_image_t* dst, const nv_image_t* src, flt_t scale)
{
  const int nchannels = nv_format_get_num_channels(src->m_format);

  dst->m_width  = (size_t)((flt_t)src->m_width / scale);
  dst->m_height = (size_t)((flt_t)src->m_width / scale);
  dst->m_format = src->m_format;
  dst->m_data   = nv_calloc(dst->m_width * dst->m_height * nv_format_get_bytes_per_pixel(dst->m_format));

  // Calculate the ratios for x and y coordinates
  flt_t x_ratio, y_ratio;
  if (dst->m_width > 1)
  {
    x_ratio = ((flt_t)src->m_width - 1.0F) / ((flt_t)dst->m_width - 1.0F);
  }
  else
  {
    x_ratio = 0;
  }

  if (dst->m_height > 1)
  {
    y_ratio = ((flt_t)src->m_height - 1.0F) / ((flt_t)dst->m_height - 1.0F);
  }
  else
  {
    y_ratio = 0;
  }

  for (size_t y = 0; y < dst->m_height; y++)
  {
    const flt_t ratiod_y = y_ratio * (flt_t)y;
    flt_t       y_l      = floorf(ratiod_y);
    flt_t       y_h      = ceilf(ratiod_y);
    flt_t       y_weight = (ratiod_y)-y_l;

    const size_t y_l_offset = (size_t)y_l * src->m_width * nchannels;
    const size_t y_h_offset = (size_t)y_h * src->m_width * nchannels;

    for (size_t x = 0; x < dst->m_width; x++)
    {
      const flt_t ratiod_x = x_ratio * (flt_t)x;

      flt_t x_l      = floorf(ratiod_x);
      flt_t x_h      = ceilf(ratiod_x);
      flt_t x_weight = (ratiod_x)-x_l;

      const size_t x_l_offset = (size_t)x_l * nchannels;
      const size_t x_h_offset = (size_t)x_h * nchannels;

      uchar* top_left_pixel     = &src->m_data[y_l_offset + x_l_offset];
      uchar* top_right_pixel    = &src->m_data[y_l_offset + x_h_offset];
      uchar* bottom_left_pixel  = &src->m_data[y_h_offset + x_l_offset];
      uchar* bottom_right_pixel = &src->m_data[y_h_offset + x_h_offset];
      for (int c = 0; c < nchannels; c++)
      {
        flt_t pixel = (flt_t)top_left_pixel[c] * (1.0F - x_weight) * (1.0F - y_weight) + (flt_t)top_right_pixel[c] * x_weight * (1.0F - y_weight)
            + (flt_t)bottom_left_pixel[c] * y_weight * (1.0F - x_weight) + (flt_t)bottom_right_pixel[c] * x_weight * y_weight;

        dst->m_data[(y * dst->m_width + x) * nchannels + c] = (unsigned char)NVM_CLAMP(pixel, 0.0f, 255.0f);
      }
    }
  }
}

nv_image_t
nv_image_load_png(const char* path)
{
  nv_image_t texture = nv_zero_init(nv_image_t);

  FILE* f = fopen(path, "rb");
  if (f == NULL)
  {
    return texture;
  }

  png_struct* png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (png == NULL)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    return texture;
  }

  png_info* info = png_create_info_struct(png);
  if (info == NULL)
  {
    NOVA_CALL_FILE_FN(fclose(f));
    return texture;
  }

  png_init_io(png, f);
  png_read_info(png, info);

  if (setjmp(png_jmpbuf(png)))
  {
    nv_assert(0);
  }

  texture.m_width     = png_get_image_width(png, info);
  texture.m_height    = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth  = png_get_bit_depth(png, info);

  if (texture.m_width == 0 || texture.m_height == 0)
  {
    nv_push_error("zero w/h");
    NOVA_CALL_FILE_FN(fclose(f));
    return texture;
  }

  if (color_type == PNG_COLOR_TYPE_PALETTE)
  {
    png_set_palette_to_rgb(png);
  }

  // if image has less than 8 bits per pixel, increase it to 8 bpp
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
  {
    png_set_expand_gray_1_2_4_to_8(png);
  }

  if (png_get_valid(png, info, PNG_INFO_tRNS))
  {
    png_set_tRNS_to_alpha(png);
  }

  png_read_update_info(png, info);

  int channels = png_get_channels(png, info);

  switch (channels)
  {
    case 1: texture.m_format = NOVA_FORMAT_R8; break;
    case 2: texture.m_format = NOVA_FORMAT_RG8; break;
    case 3: texture.m_format = NOVA_FORMAT_RGB8; break;
    case 4: texture.m_format = NOVA_FORMAT_RGBA8; break;
    default:
      nv_push_error("unsupported file(png) format: channels = %d", channels);
      fclose(f);
      png_destroy_read_struct(&png, &info, NULL);
      return nv_zero_init(nv_image_t);
      break;
  }

  size_t rowbytes = png_get_rowbytes(png, info);
  texture.m_data  = (unsigned char*)nv_malloc(rowbytes * texture.m_height * channels);
  nv_assert(texture.m_data != NULL);

  u8** row_pointers = nv_malloc(sizeof(u8*) * texture.m_height);
  for (size_t y = 0; y < texture.m_height; y++)
  {
    row_pointers[y] = texture.m_data + y * texture.m_width * nv_format_get_bytes_per_pixel(texture.m_format);
  }

  png_read_image(png, row_pointers);

  png_destroy_read_struct(&png, &info, NULL);
  fclose(f);
  nv_free(row_pointers);

  return texture;
}

nv_image_t
nv_image_load_jpeg(const char* path)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr         jerr;
  FILE*                         f   = NULL;
  nv_image_t                    img = nv_zero_init(nv_image_t);

  if (!path)
  {
    nv_push_error("invalid input path (NULL)");
    return img;
  }

  if ((f = fopen(path, "rb")) == NULL)
  {
    nv_push_error("couldn't open file \"%s\". Are you sure that it exists?", path);
    return img;
  }

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, f);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
  {
    nv_push_error("failed to read JPEG header from \"%s\"", path);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  jpeg_start_decompress(&cinfo);

  img.m_width  = cinfo.output_width;
  img.m_height = cinfo.output_height;

  switch (cinfo.output_components)
  {
    case 1: img.m_format = NOVA_FORMAT_R8; break;
    case 3: img.m_format = NOVA_FORMAT_RGB8; break;
    default:
      nv_push_error("invalid number of channels: %d", cinfo.output_components);
      jpeg_destroy_decompress(&cinfo);
      fclose(f);
      return img;
  }

  const size_t bytes_per_pixel = nv_format_get_bytes_per_pixel(img.m_format);
  if (bytes_per_pixel == 0)
  {
    nv_push_error("invalid bytes per pixel for format.");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  img.m_data = (unsigned char*)nv_malloc(img.m_width * img.m_height * bytes_per_pixel);
  if (!img.m_data)
  {
    nv_push_error("malloc for imagedata failed");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  unsigned char* bufarr[1];
  for (int i = 0; i < (int)cinfo.output_height; i++)
  {
    bufarr[0] = img.m_data + i * img.m_width * bytes_per_pixel;
    if (jpeg_read_scanlines(&cinfo, bufarr, 1) != 1)
    {
      nv_push_error("failed to read scanline %d", i);
      nv_free(img.m_data);
      jpeg_destroy_decompress(&cinfo);
      fclose(f);
      return nv_zero_init(nv_image_t);
    }
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  fclose(f);

  return img;
}

void
nv_image_write_png(const nv_image_t* tex, const char* path)
{
  if (tex == NULL || path == NULL || tex->m_data == NULL)
  {
    return;
  }

  FILE* f = fopen(path, "wb");
  if (!f)
  {
    nv_push_error("Failed to open file: %s", path);
    return;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
  {
    nv_push_error("png error");
    fclose(f);
    return;
  }

  png_infop info = png_create_info_struct(png);
  if (!info)
  {
    nv_push_error("png error");
    png_destroy_write_struct(&png, NULL);
    fclose(f);
    return;
  }

  if (setjmp(png_jmpbuf(png)))
  {
    nv_push_error("setjmp error");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_init_io(png, f);

  const int numc    = nv_format_get_num_channels(tex->m_format);
  int       coltype = -1;
  switch (numc)
  {
    case 1: coltype = PNG_COLOR_TYPE_GRAY; break;
    case 3: coltype = PNG_COLOR_TYPE_RGB; break;
    case 4: coltype = PNG_COLOR_TYPE_RGBA; break;
    default:
      nv_push_error("Unsupported number of channels: %i", numc);
      png_destroy_write_struct(&png, &info);
      fclose(f);
      return;
  }

  const int bytesperpixel = nv_format_get_bytes_per_pixel(tex->m_format);
  if (bytesperpixel <= 0)
  {
    nv_push_error("invalid bytes per pixel: %i", bytesperpixel);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_set_IHDR(png, info, tex->m_width, tex->m_height, bytesperpixel * 8, coltype, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  png_bytep* row_pointers = (png_bytep*)nv_malloc(sizeof(png_bytep) * tex->m_height);
  if (!row_pointers)
  {
    nv_push_error("malloc row_pointers failed");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  for (size_t y = 0; y < tex->m_height; y++)
  {
    row_pointers[y] = tex->m_data + y * tex->m_width * bytesperpixel;
  }

  png_write_image(png, row_pointers);
  nv_free(row_pointers);

  png_write_end(png, NULL);

  png_destroy_write_struct(&png, &info);
  fclose(f);
}

// nv_image_t

void
nv_format_to_string(nv_format format, const char** dst)
{
  switch (format)
  {
    case NOVA_FORMAT_UNDEFINED: *dst = "NOVA_FORMAT_UNDEFINED"; return;
    case NOVA_FORMAT_R8: *dst = "NOVA_FORMAT_R8"; return;
    case NOVA_FORMAT_RG8: *dst = "NOVA_FORMAT_RG8"; return;
    case NOVA_FORMAT_RGB8: *dst = "NOVA_FORMAT_RGB8"; return;
    case NOVA_FORMAT_RGBA8: *dst = "NOVA_FORMAT_RGBA8"; return;
    case NOVA_FORMAT_BGR8: *dst = "NOVA_FORMAT_BGR8"; return;
    case NOVA_FORMAT_BGRA8: *dst = "NOVA_FORMAT_BGRA8"; return;
    case NOVA_FORMAT_RGB16: *dst = "NOVA_FORMAT_RGB16"; return;
    case NOVA_FORMAT_RGBA16: *dst = "NOVA_FORMAT_RGBA16"; return;
    case NOVA_FORMAT_RG32: *dst = "NOVA_FORMAT_RG32"; return;
    case NOVA_FORMAT_RGB32: *dst = "NOVA_FORMAT_RGB32"; return;
    case NOVA_FORMAT_RGBA32: *dst = "NOVA_FORMAT_RGBA32"; return;
    case NOVA_FORMAT_R8_SINT: *dst = "NOVA_FORMAT_R8_SINT"; return;
    case NOVA_FORMAT_RG8_SINT: *dst = "NOVA_FORMAT_RG8_SINT"; return;
    case NOVA_FORMAT_RGB8_SINT: *dst = "NOVA_FORMAT_RGB8_SINT"; return;
    case NOVA_FORMAT_RGBA8_SINT: *dst = "NOVA_FORMAT_RGBA8_SINT"; return;
    case NOVA_FORMAT_R8_UINT: *dst = "NOVA_FORMAT_R8_UINT"; return;
    case NOVA_FORMAT_RG8_UINT: *dst = "NOVA_FORMAT_RG8_UINT"; return;
    case NOVA_FORMAT_RGB8_UINT: *dst = "NOVA_FORMAT_RGB8_UINT"; return;
    case NOVA_FORMAT_RGBA8_UINT: *dst = "NOVA_FORMAT_RGBA8_UINT"; return;
    case NOVA_FORMAT_R8_SRGB: *dst = "NOVA_FORMAT_R8_SRGB"; return;
    case NOVA_FORMAT_RG8_SRGB: *dst = "NOVA_FORMAT_RG8_SRGB"; return;
    case NOVA_FORMAT_RGB8_SRGB: *dst = "NOVA_FORMAT_RGB8_SRGB"; return;
    case NOVA_FORMAT_RGBA8_SRGB: *dst = "NOVA_FORMAT_RGBA8_SRGB"; return;
    case NOVA_FORMAT_BGR8_SRGB: *dst = "NOVA_FORMAT_BGR8_SRGB"; return;
    case NOVA_FORMAT_BGRA8_SRGB: *dst = "NOVA_FORMAT_BGRA8_SRGB"; return;
    case NOVA_FORMAT_D16: *dst = "NOVA_FORMAT_D16"; return;
    case NOVA_FORMAT_D24: *dst = "NOVA_FORMAT_D24"; return;
    case NOVA_FORMAT_D32: *dst = "NOVA_FORMAT_D32"; return;
    case NOVA_FORMAT_D24_S8: *dst = "NOVA_FORMAT_D24_S8"; return;
    case NOVA_FORMAT_D32_S8: *dst = "NOVA_FORMAT_D32_S8"; return;
    case NOVA_FORMAT_BC1: *dst = "NOVA_FORMAT_BC1"; return;
    case NOVA_FORMAT_BC3: *dst = "NOVA_FORMAT_BC3"; return;
    case NOVA_FORMAT_BC7: *dst = "NOVA_FORMAT_BC7"; return;
  }
}

bool
nv_format_has_color_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return 0;
    default: return 1;
  }
}

// Returns false even for stencil/depth and undefined format
bool
nv_format_has_alpha_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;
    default: return 0;
  }
}

bool
nv_format_has_depth_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

bool
nv_format_has_stencil_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 1;

    default: return 0;
  }
}

int
nv_format_get_bytes_per_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 1;

    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_D16: return 2;

    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D24_S8: return 3;

    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_D32:
    case NOVA_FORMAT_D32_S8: return 4;

    default:
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED: return -1;
  }
}

int
nv_format_get_bytes_per_pixel(nv_format fmt)
{
  return nv_format_get_bytes_per_channel(fmt) * nv_format_get_num_channels(fmt);
}

int
nv_format_get_num_channels(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_R8:
    case NOVA_FORMAT_R8_SINT:
    case NOVA_FORMAT_R8_UINT:
    case NOVA_FORMAT_R8_SRGB:
    case NOVA_FORMAT_D16:
    case NOVA_FORMAT_D24:
    case NOVA_FORMAT_D32: return 1;

    case NOVA_FORMAT_RG8:
    case NOVA_FORMAT_RG32:
    case NOVA_FORMAT_RG8_SINT:
    case NOVA_FORMAT_RG8_UINT:
    case NOVA_FORMAT_RG8_SRGB:
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return 2;

    case NOVA_FORMAT_RGB8:
    case NOVA_FORMAT_BGR8:
    case NOVA_FORMAT_RGB16:
    case NOVA_FORMAT_RGB32:
    case NOVA_FORMAT_RGB8_SINT:
    case NOVA_FORMAT_RGB8_UINT:
    case NOVA_FORMAT_RGB8_SRGB:
    case NOVA_FORMAT_BGR8_SRGB: return 3;

    case NOVA_FORMAT_RGBA8:
    case NOVA_FORMAT_BGRA8:
    case NOVA_FORMAT_RGBA16:
    case NOVA_FORMAT_RGBA32:
    case NOVA_FORMAT_RGBA8_SINT:
    case NOVA_FORMAT_RGBA8_UINT:
    case NOVA_FORMAT_RGBA8_SRGB:
    case NOVA_FORMAT_BGRA8_SRGB: return 4;

    // FIXME: Implement
    case NOVA_FORMAT_BC1:
    case NOVA_FORMAT_BC3:
    case NOVA_FORMAT_BC7:
    case NOVA_FORMAT_UNDEFINED:
    default: return 0;
  }
}

// header of memory block
typedef struct sablock
{
  size_t   m_size;
  unsigned m_canary;
} sablock;

void
nv_allocator_stack_init(nv_allocator_stack* allocator, unsigned char* buf, size_t available)
{
  allocator->m_buf       = buf;
  allocator->m_bufsiz    = available;
  allocator->m_bufoffset = 0;
}

void*
nv_stack_realloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size)
{
  if (!prevblock)
  {
    nv_log_and_abort("invalid pointer\n");
    return NULL;
  }
  sablock* prevblockp = (sablock*)prevblock - 1;
  if (prevblockp->m_size >= size)
  {
    return prevblock;
  }
  if (prevblockp->m_canary != NOVA_ALLOCATION_CANARY)
  {
    nv_log_and_abort("corrupt memory\n");
    return NULL;
  }

  void* new_data = nv_stack_alloc(parent, alignment, size);
  if (new_data == NULL)
  {
    return NULL;
  }
  nv_memset(new_data, 0, size);
  nv_memcpy(new_data, prevblock, prevblockp->m_size);
  nv_stack_free(parent, prevblock);

  return new_data;
}

void*
nv_stack_alloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->m_context;
  size                          = align_up_size(size, alignment);
  if ((allocator->m_bufoffset + size + sizeof(sablock)) > allocator->m_bufsiz)
  {
    nv_push_error("oom"); // out of memory
    return NULL;
  }

  sablock* block  = align_up(allocator->m_buf + allocator->m_bufoffset, alignment);
  block->m_size   = size;
  block->m_canary = NOVA_ALLOCATION_CANARY;
  block++; // move past the header, so return is the memory after header
  allocator->m_bufoffset += size + sizeof(sablock);
  return (void*)block;
}

void*
nv_stack_calloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  void* allocation = nv_stack_alloc(parent, alignment, size);
  nv_memset(allocation, 0, size);
  return allocation;
}

void
nv_stack_free(nv_allocator_t* parent, void* block)
{
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->m_context;
  sablock*            p         = (sablock*)block;
  p--;
  nv_assert(p->m_canary == NOVA_ALLOCATION_CANARY);
  void* allocator_last_block = (allocator->m_buf + allocator->m_bufoffset - p->m_size - sizeof(sablock));
  if (block != allocator_last_block)
  {
    return;
  }
  allocator->m_bufoffset -= p->m_size + sizeof(sablock);
}

nv_allocator_t*
nv_allocator_get_default(void)
{
  static nv_allocator_t nv_allocator_default;
  // if the allocator was corrupted by a function, we'll get foked
  nv_allocator_default.m_alloc     = nv_heap_alloc;
  nv_allocator_default.m_calloc    = nv_heap_calloc;
  nv_allocator_default.m_realloc   = nv_heap_realloc;
  nv_allocator_default.m_free      = nv_heap_free;
  nv_allocator_default.m_context   = NULL;
  nv_allocator_default.m_user_data = NULL;
  return &nv_allocator_default;
}

void*
nv_heap_alloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = malloc(size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void*
nv_heap_calloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = nv_calloc(size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void*
nv_heap_realloc(nv_allocator_t* parent, void* prevblock, size_t alignment, size_t size)
{
  (void)parent;
  nv_assert((alignment & (alignment - 1)) == 0);

  size += alignment - 1 + sizeof(void*);

  void* orig = realloc(((void**)prevblock)[-1], size);
  nv_assert(orig != NULL);

  void* p         = (void*)(((uintptr_t)orig + sizeof(void*) + alignment - 1) & ~(alignment - 1));
  ((void**)p)[-1] = orig;

  return p;
}

void
nv_heap_free(nv_allocator_t* parent, void* block)
{
  (void)parent;
  if (block)
  {
    free(((void**)block)[-1]);
  }
}

// A pool is moade of many chunks
nv_node_t*
heap_alloc_internal(size_t alignment, size_t size)
{
  nv_assert((alignment & (alignment - 1)) == 0 && alignment > 0);
  nv_assert(size < SIZE_MAX - alignment - sizeof(nv_node_t) - sizeof(unsigned));

  size += sizeof(nv_node_t);

  size_t total_size = align_up_size(size, alignment);

  if (total_size <= 0)
  {
    nv_push_error("zero size malloc\n");
    return NULL;
  }

  nv_assert(0);

  // void* mapping = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  // if (mapping == MAP_FAILED || !mapping)
  // {
  // nv_push_error("mmap failed: %s\n", strerror(*(__errno_location())));
  // return NULL;
  // }
  void* mapping = NULL;

  nv_chunk_t* chk     = (nv_chunk_t*)mapping;
  chk->m_mapping      = mapping;
  chk->m_mapping_size = total_size;
  chk->m_available    = total_size;

  nv_node_t* p = (nv_node_t*)((nv_chunk_t*)mapping + 1);
  *p           = (nv_node_t){
              .m_payload      = align_up(p + 1, alignment),
              .m_mapping      = mapping,
              .m_mapping_size = total_size,
              .m_canary       = NOVA_ALLOCATION_CANARY,
              .m_in_use       = 1,
  };
  chk->m_root = p;
  return p;
}

void
heap_free_node_internal(nv_node_t* node)
{
  if (!node)
  {
    nv_log_and_abort("invalid ptr\n\n");
    return;
  }

  if (node->m_canary != NOVA_ALLOCATION_CANARY)
  {
    nv_log_and_abort("memory is corrupt\n\n");
    return;
  }

  // void*  mapping = node->m_mapping;
  // size_t size    = node->m_mapping_size;
  *node = nv_zero_init(nv_node_t);

  nv_assert(0);

  // if (munmap(mapping, size) == -1)
  if (false)
  {
    nv_push_error("munmap failed: %s\n", strerror(*(__errno_location())));
    return;
  }
}

void
nv_allocator_heap_init(nv_allocator_heap* pool)
{
  nv_freelist_init(0, heap_alloc_internal, heap_free_node_internal, nv_allocator_get_default(), &pool->m_freelist);
}

// deadbeef is for losers
#define CONT_CANARY 0xFEEF

#define CONT_IS_VALID(cont) ((cont) && ((cont)->m_canary == CONT_CANARY))

// ==============================
// VECTOR
// ==============================

void
nv_list_init(size_t typesize, size_t init_capacity, nv_allocator_t* allocator, nv_list_t* vec)
{
  nv_assert(typesize > 0);
  nv_assert(allocator != NULL);

  *vec            = nv_zero_init(nv_list_t);
  vec->m_size     = 0;
  vec->m_typesize = typesize;
  vec->m_canary   = CONT_CANARY;
  vec->m_mutex    = SDL_CreateMutex();
  vec->m_alloc    = allocator;

  SDL_LockMutex(vec->m_mutex);
  if (init_capacity > 0)
  {
    vec->m_data     = vec->m_alloc->m_calloc(vec->m_alloc, 1, vec->m_typesize * init_capacity);
    vec->m_capacity = init_capacity;
  }
  else
  {
    vec->m_data = NULL;
  }
  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_destroy(nv_list_t* vec)
{
  if (vec)
  {
    SDL_LockMutex(vec->m_mutex);
    nv_assert(CONT_IS_VALID(vec));
    if (vec->m_data)
    {
      vec->m_alloc->m_free(vec->m_alloc, vec->m_data);
      SDL_UnlockMutex(vec->m_mutex);
      SDL_DestroyMutex(vec->m_mutex);
    }
  }
}

void
nv_list_clear(nv_list_t* vec)
{
  SDL_LockMutex(vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  vec->m_size = 0;
  SDL_UnlockMutex(vec->m_mutex);
}

size_t
nv_list_size(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t sz = vec->m_size;
  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return sz;
}

size_t
nv_list_capacity(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t cap = vec->m_capacity;
  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return cap;
}

size_t
nv_list_typesize(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t tsize = vec->m_typesize;
  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return tsize;
}

void*
nv_list_data(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* ptr = vec->m_data;
  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return ptr;
}

void*
nv_list_back(nv_list_t* vec)
{
  SDL_LockMutex(vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* ptr = nv_list_get(vec, NV_MAX(1ULL, vec->m_size) - 1); // stupid but works
  // that's how I'd describe the entirety of this projetc
  SDL_UnlockMutex(vec->m_mutex);
  return ptr;
}

void*
nv_list_get(const nv_list_t* vec, size_t i)
{
  SDL_LockMutex((SDL_mutex*)vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  uchar* data     = vec->m_data;
  size_t typesize = vec->m_typesize;
  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return data + (typesize * i);
}

void
nv_list_set(nv_list_t* vec, size_t i, void* elem)
{
  SDL_LockMutex(vec->m_mutex);
  nv_assert(CONT_IS_VALID(vec));
  nv_memcpy((char*)vec + (vec->m_typesize * i), elem, vec->m_typesize);
  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_copy_from(const nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst)
{
  SDL_LockMutex((SDL_mutex*)src->m_mutex);
  SDL_LockMutex(dst->m_mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src->m_typesize == dst->m_typesize);
  if (src->m_size >= dst->m_capacity)
  {
    nv_list_resize(dst, src->m_size);
  }
  dst->m_size = src->m_size;
  nv_memcpy(dst->m_data, src->m_data, src->m_size * src->m_typesize);

  SDL_UnlockMutex((SDL_mutex*)src->m_mutex);
  SDL_UnlockMutex(dst->m_mutex);
}

void
nv_list_move_from(nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst)
{
  SDL_LockMutex(src->m_mutex);
  SDL_LockMutex(dst->m_mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  dst->m_size     = src->m_size;
  dst->m_capacity = src->m_capacity;
  dst->m_data     = src->m_data;

  src->m_size     = 0;
  src->m_capacity = 0;
  src->m_data     = NULL;

  SDL_UnlockMutex(src->m_mutex);
  SDL_UnlockMutex(dst->m_mutex);
}

bool
nv_list_empty(const nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));
  return (vec->m_size == 0);
}

bool
nv_list_equal(const nv_list_t* vec1, const nv_list_t* vec2)
{
  nv_assert(CONT_IS_VALID(vec1));
  nv_assert(CONT_IS_VALID(vec2));

  SDL_LockMutex((SDL_mutex*)vec1->m_mutex);
  SDL_LockMutex((SDL_mutex*)vec2->m_mutex);

  bool equal = 1;
  if ((vec1->m_size != vec2->m_size || vec1->m_typesize != vec2->m_typesize) || (nv_memcmp(vec1->m_data, vec2->m_data, vec1->m_size * vec1->m_typesize) != 0))
  {
    equal = 0;
  }

  SDL_UnlockMutex((SDL_mutex*)vec1->m_mutex);
  SDL_UnlockMutex((SDL_mutex*)vec2->m_mutex);

  return equal;
}

void
nv_list_resize(nv_list_t* vec, size_t new_size)
{
  nv_assert(CONT_IS_VALID(vec));

  if (vec->m_data)
  {
    vec->m_data = vec->m_alloc->m_realloc(vec->m_alloc, vec->m_data, 1, vec->m_typesize * new_size);
  }
  else
  {
    vec->m_data = vec->m_alloc->m_calloc(vec->m_alloc, 1, vec->m_typesize * new_size);
  }
  nv_assert(vec->m_data != NULL);

  vec->m_capacity = new_size;
}

void
nv_list_push_back(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (vec->m_size >= vec->m_capacity)
  {
    nv_list_resize(vec, NV_MAX(1, vec->m_capacity * 2));
  }

  nv_assert(vec->m_data != NULL);
  nv_assert(!(elem >= vec->m_data && (unsigned char*)elem <= ((unsigned char*)vec->m_data + vec->m_size))); // breaks restriction rules
  nv_memcpy((uchar*)vec->m_data + (vec->m_size * vec->m_typesize), elem, vec->m_typesize);
  vec->m_size++;

  SDL_UnlockMutex(vec->m_mutex);
}

void*
nv_list_push_empty(nv_list_t* __restrict vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (vec->m_size >= vec->m_capacity)
  {
    nv_list_resize(vec, NV_MAX(1, vec->m_capacity * 2));
  }

  nv_assert(vec->m_data != NULL);
  void* p = (uchar*)vec->m_data + (vec->m_size * vec->m_typesize);
  nv_memset(p, 0, vec->m_typesize);
  vec->m_size++;

  SDL_UnlockMutex(vec->m_mutex);

  return p;
}

void
nv_list_push_set(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT arr, size_t count)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  size_t required_capacity = vec->m_size + count;
  if (required_capacity >= vec->m_capacity)
  {
    nv_list_resize(vec, required_capacity);
  }
  nv_memcpy((uchar*)vec->m_data + (vec->m_size * vec->m_typesize), arr, count * vec->m_typesize);
  vec->m_size += count;

  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_pop_back(nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (vec->m_size > 0)
  {
    vec->m_size--;
  }

  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_pop_front(nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (vec->m_size > 0)
  {
    vec->m_size--;
    nv_memcpy(vec->m_data, (uchar*)vec->m_data + vec->m_typesize, vec->m_size * vec->m_typesize);
  }

  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_insert(nv_list_t* NV_RESTRICT vec, size_t index, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (index >= vec->m_capacity)
  {
    nv_list_resize(vec, NV_MAX(1, index * 2));
  }
  if (index >= vec->m_size)
  {
    vec->m_size = index + 1;
  }
  nv_memcpy((uchar*)vec->m_data + (vec->m_typesize * index), elem, vec->m_typesize);

  SDL_UnlockMutex(vec->m_mutex);
}

void
nv_list_remove(nv_list_t* vec, size_t index)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  if (index >= vec->m_size)
  {
    return;
  }

  if (vec->m_size - index - 1)
  {
    // please don't ask me what this is
    nv_memcpy((uchar*)vec->m_data + (index * vec->m_typesize), (uchar*)vec->m_data + ((index + 1) * vec->m_typesize), (vec->m_size - index - 1) * vec->m_typesize);
  }
  vec->m_size--;

  SDL_UnlockMutex(vec->m_mutex);
}

int
nv_list_find(const nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex((SDL_mutex*)vec->m_mutex);

  for (int i = 0; i < (int)vec->m_size; i++)
  {
    if (nv_memcmp((unsigned char*)vec->m_data + (i * vec->m_typesize), elem, vec->m_typesize))
    {
      SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
      return i;
    }
  }

  SDL_UnlockMutex((SDL_mutex*)vec->m_mutex);
  return -1;
}

void
nv_list_sort(nv_list_t* vec, nv_list_compare_fn compare)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->m_mutex);

  qsort(vec->m_data, vec->m_size, vec->m_typesize, compare);

  SDL_UnlockMutex(vec->m_mutex);
}

// ==============================
// STRING
// ==============================

#define _nv_string_alloc(size) str->m_alloc->m_calloc(str->m_alloc, 1, size)
#define _nv_string_calloc(size) str->m_alloc->m_calloc(str->m_alloc, 1, size)
#define _nv_string_realloc(prevblock, size) str->m_alloc->m_realloc(str->m_alloc, prevblock, 1, size)
#define _nv_string_free(size) str->m_alloc->m_free(str->m_alloc, size)

static void
nv_string_resize(nv_string_t* str, size_t new_capacity)
{
  char* new_data = _nv_string_realloc(str->m_data, new_capacity);
  nv_assert(new_data != NULL);
  str->m_data     = new_data;
  str->m_capacity = new_capacity;
}

nv_string_t
nv_string_init(size_t initial_size, nv_allocator_t* allocator)
{
  nv_string_t str = nv_zero_init(nv_string_t);

  str.m_mutex = SDL_CreateMutex();

  str.m_alloc    = allocator;
  str.m_capacity = (initial_size > 0) ? initial_size : 1;
  str.m_data     = str.m_alloc->m_calloc(str.m_alloc, 1, str.m_capacity);
  str.m_canary   = CONT_CANARY;
  nv_assert(str.m_data != NULL);

  str.m_data[0] = 0;
  str.m_size    = 0;
  return str;
}

nv_string_t
nv_string_init_str(const char* init, nv_allocator_t* allocator)
{
  nv_assert(init != NULL && nv_strlen(init) > 0);
  nv_string_t str = nv_zero_init(nv_string_t);
  str.m_alloc     = allocator;
  size_t len      = nv_strlen(init);
  str.m_capacity  = len + 1;
  str.m_data      = str.m_alloc->m_calloc(str.m_alloc, 1, str.m_capacity);
  str.m_canary    = CONT_CANARY;
  nv_assert(str.m_data != NULL);

  nv_strcpy(str.m_data, init);
  str.m_size = len;

  str.m_mutex = SDL_CreateMutex();

  return str;
}

nv_string_t
nv_string_substring(const nv_string_t* str, size_t start, size_t length, nv_allocator_t* new_allocator)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(start + length <= str->m_size);

  nv_string_t substr = nv_string_init(length + 1, new_allocator);

  nv_strlcpy(substr.m_data, str->m_data + start, length + 1);
  substr.m_data[length] = 0;
  substr.m_size         = length;
  return substr;
}

void
nv_string_destroy(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex(str->m_mutex);
  if (str)
  {
    _nv_string_free(str->m_data);
    SDL_UnlockMutex(str->m_mutex);
    SDL_DestroyMutex(str->m_mutex);
  }
}

void
nv_string_clear(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex(str->m_mutex);
  if (str)
  {
    str->m_size    = 0;
    str->m_data[0] = 0;
  }
  SDL_UnlockMutex(str->m_mutex);
}

size_t
nv_string_length(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->m_mutex);
  size_t size = str->m_size;
  SDL_UnlockMutex((SDL_mutex*)str->m_mutex);
  return size;
}

size_t
nv_string_capacity(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->m_mutex);
  size_t capacity = str->m_capacity;
  SDL_UnlockMutex((SDL_mutex*)str->m_mutex);
  return capacity;
}

const char*
nv_string_data(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->m_mutex);
  const char* data = str->m_data;
  SDL_UnlockMutex((SDL_mutex*)str->m_mutex);
  return data;
}

void
nv_string_append(nv_string_t* str, const char* suffix)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(suffix != NULL);

  size_t suffix_length = nv_strlen(suffix);
  if (nv_string_length(str) + suffix_length + 1 > str->m_capacity)
  {
    nv_string_resize(str, str->m_size + suffix_length + 1);
  }

  SDL_LockMutex(str->m_mutex);
  nv_strcpy(str->m_data + str->m_size, suffix);
  str->m_size += suffix_length;
  SDL_UnlockMutex(str->m_mutex);
}

void
nv_string_append_char(nv_string_t* str, char suffix)
{
  nv_assert(CONT_IS_VALID(str));

  SDL_LockMutex(str->m_mutex);

  if (str->m_size + 2 > str->m_capacity)
  {
    SDL_UnlockMutex(str->m_mutex);
    nv_string_resize(str, str->m_size + 2);
    SDL_LockMutex(str->m_mutex);
  }

  str->m_data[str->m_size] = suffix;
  str->m_size++;
  str->m_data[str->m_size] = 0;

  SDL_UnlockMutex(str->m_mutex);
}

void
nv_string_prepend(nv_string_t* str, const char* prefix)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(prefix != NULL);

  size_t prefix_length = nv_strlen(prefix);
  if (nv_string_length(str) + prefix_length + 1 > nv_string_capacity(str))
  {
    nv_string_resize(str, nv_string_length(str) + prefix_length + 1);
  }

  SDL_LockMutex(str->m_mutex);
  nv_memmove(str->m_data + prefix_length, str->m_data, str->m_size + 1);
  nv_memcpy(str->m_data, prefix, prefix_length);
  str->m_size += prefix_length;
  SDL_UnlockMutex(str->m_mutex);
}

void
nv_string_set(nv_string_t* str, const char* new_str)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(new_str != NULL);

  size_t new_length = nv_strlen(new_str);
  if (new_length + 1 > str->m_capacity)
  {
    nv_string_resize(str, new_length + 1);
  }

  SDL_LockMutex(str->m_mutex);

  nv_strcpy(str->m_data, new_str);
  str->m_size = new_length;

  SDL_UnlockMutex(str->m_mutex);
}

size_t
nv_string_find(const nv_string_t* str, const char* substr)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(substr != NULL);

  SDL_LockMutex((SDL_mutex*)str->m_mutex);
  char*  pos = nv_strstr(str->m_data, substr);
  size_t ret = pos ? (size_t)(pos - str->m_data) : (size_t)-1;
  SDL_UnlockMutex((SDL_mutex*)str->m_mutex);
  return ret;
}

void
nv_string_remove(nv_string_t* str, size_t index, size_t length)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(index < str->m_size);

  SDL_LockMutex(str->m_mutex);

  if (index + length > str->m_size)
  {
    length = str->m_size - index;
  }

  nv_memmove(str->m_data + index, str->m_data + index + length, str->m_size - index - length + 1);
  str->m_size -= length;

  SDL_UnlockMutex(str->m_mutex);
}

void
nv_string_copy_from(const nv_string_t* src, nv_string_t* dst)
{
  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src != NULL);
  nv_assert(dst != NULL);
  nv_string_set(dst, src->m_data);
}

void
nv_string_move_from(nv_string_t* src, nv_string_t* dst)
{
  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src != NULL);
  nv_assert(dst != NULL);
  nv_string_copy_from(src, dst);
  nv_string_destroy(src);
}

// ==============================
// HASHMAP
// ==============================

u32
next_power_of_two(u32 num)
{
  if (num == 0)
  {
    return 1;
  }
  num--;
  num |= num >> 1U;
  num |= num >> 2U;
  num |= num >> 4U;
  num |= num >> 8U;
  num |= num >> 16U;
  num++;
  return num;
}

u32
power_of_two_mod(u32 num, u32 mod_by)
{
  return num & (mod_by - 1);
}

#define _nv_hashmap_alloc(size) map->m_alloc->m_calloc(map->m_alloc, 1, size)
#define _nv_hashmap_calloc(size) map->m_alloc->m_calloc(map->m_alloc, 1, size)
#define _nv_hashmap_free(block) map->m_alloc->m_free(map->m_alloc, block);

#define NV_NODE_OCCUPIED(node) ((node).m_key != NULL && (node).m_value != NULL)

void
nv_hashmap_init(size_t init_size, size_t key_size, size_t value_size, nv_hash_fn hash_fn, nv_allocator_t* allocator, nv_hashmap_t* dst)
{
  nv_assert(dst != NULL);
  nv_assert(key_size > 0 && value_size > 0);

  *dst = nv_zero_init(nv_hashmap_t);

  dst->m_mutex = SDL_CreateMutex();

  // TODO: Is this needed?
  SDL_LockMutex(dst->m_mutex);

  dst->m_alloc = allocator;
  dst->m_nodes = init_size == 0 ? NULL : (nv_hashmap_node_t*)allocator->m_calloc(allocator, 1, init_size * sizeof(nv_hashmap_node_t));
  nv_assert(dst->m_nodes != NULL);

  dst->m_hash_fn    = hash_fn ? hash_fn : nv_hash_murmur3;
  dst->m_key_size   = key_size;
  dst->m_value_size = value_size;
  dst->m_entries    = next_power_of_two(init_size);
  dst->m_size       = 0;
  dst->m_canary     = CONT_CANARY;

  SDL_UnlockMutex(dst->m_mutex);
}

void
nv_hashmap_destroy(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->m_mutex);
  if (map->m_nodes)
  {
    for (size_t idx = 0; idx < map->m_entries; idx++)
    {
      nv_hashmap_node_t* node = &map->m_nodes[idx];
      if (NV_NODE_OCCUPIED(*node))
      {
        _nv_hashmap_free(node->m_key);
        node->m_key = NULL;
      }
    }
    _nv_hashmap_free((void*)map->m_nodes);
    map->m_nodes = NULL;
  }
  SDL_UnlockMutex(map->m_mutex);
  SDL_DestroyMutex(map->m_mutex);
}

void
nv_hashmap_resize(nv_hashmap_t* map, size_t new_size, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->m_mutex);

  nv_hashmap_node_t* old_nodes     = map->m_nodes;
  const size_t       old_m_entries = map->m_entries;

  if (new_size <= 0)
  {
    new_size = 1;
  }

  map->m_entries = next_power_of_two(new_size);
  map->m_size    = 0;

  // we can't do realloc here because we need to rehash all the nodes
  map->m_nodes = (nv_hashmap_node_t*)_nv_hashmap_calloc(new_size * sizeof(nv_hashmap_node_t));
  nv_assert(map->m_nodes != NULL);

  if (old_nodes)
  {
    for (size_t i = 0; i < old_m_entries; i++)
    {
      nv_hashmap_node_t* node = &old_nodes[i];
      if (NV_NODE_OCCUPIED(*node))
      {
        SDL_UnlockMutex(map->m_mutex);
        nv_hashmap_insert(map, node->m_key, node->m_value, hash_fn_arg);
        SDL_LockMutex(map->m_mutex);
        _nv_hashmap_free(node->m_key);
      }
    }
    _nv_hashmap_free((void*)old_nodes);
    old_nodes = NULL;
  }

  SDL_UnlockMutex(map->m_mutex);
}

void
nv_hashmap_clear(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  nv_hashmap_destroy(map);
  map->m_nodes   = NULL;
  map->m_size    = 0;
  map->m_entries = 0;
}

size_t
nv_hashmap_size(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_size;
}

size_t
nv_hashmap_capacity(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_entries;
}

size_t
nv_hashmap_keysize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_key_size;
}

size_t
nv_hashmap_valuesize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_value_size;
}

nv_hashmap_node_t*
nv_hashmap_iterate(const nv_hashmap_t* map, size_t* __i)
{
  nv_assert(CONT_IS_VALID(map));
  for (; (*__i) < map->m_entries; (*__i)++)
  {
    size_t i = *__i;
    if (NV_NODE_OCCUPIED(map->m_nodes[i]))
    {
      (*__i)++;
      return &map->m_nodes[i];
    }
  }
  return NULL;
}

nv_hashmap_node_t*
nv_hashmap_root_node(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->m_nodes;
}

void*
nv_hashmap_find(const nv_hashmap_t* NV_RESTRICT map, const void* NV_RESTRICT key, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex((SDL_mutex*)map->m_mutex);

  if (!map->m_nodes)
  {
    SDL_UnlockMutex((SDL_mutex*)map->m_mutex);
    return NULL;
  }

  const u32 hash  = map->m_hash_fn(key, map->m_key_size, hash_fn_arg);
  const u32 begin = power_of_two_mod(hash, map->m_entries);

  u32 index = begin;
  u32 probe = 1;

  while (NV_NODE_OCCUPIED(map->m_nodes[index]))
  {
    // if (map->m_equal_fn(map->m_nodes[index].m_key, key, map->m_key_size))
    if (map->m_nodes[index].m_hash == hash)
    {
      void* value = map->m_nodes[index].m_value;
      SDL_UnlockMutex((SDL_mutex*)map->m_mutex);
      return value;
    }
    index = power_of_two_mod((hash + probe + (probe * probe)), map->m_entries);
    if (index == begin)
    {
      break;
    }
    probe++;
  }

  SDL_UnlockMutex((SDL_mutex*)map->m_mutex);
  return NULL;
}

static inline void
_nv_hashmap_insert_internal(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value, bool replace_if_exists, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->m_mutex);

  // the second check
  if (!map->m_nodes || (flt_t)map->m_size >= ((flt_t)map->m_entries * NV_HASHMAP_LOAD_FACTOR))
  {
    // The check to whether map->m_entries is greater than 0 is already done in
    // resize();
    SDL_UnlockMutex(map->m_mutex);
    nv_hashmap_resize(map, map->m_entries * 2, hash_fn_arg);
    SDL_LockMutex(map->m_mutex);
  }

  const u32 hash  = map->m_hash_fn(key, map->m_key_size, hash_fn_arg);
  const u32 begin = power_of_two_mod(hash, map->m_entries);

  u32 index = begin;
  u32 probe = 1;

  while (NV_NODE_OCCUPIED(map->m_nodes[index]))
  {
    index = power_of_two_mod((hash + probe + (probe * probe)), map->m_entries);
    if (hash == map->m_nodes[index].m_hash && nv_memcmp(map->m_nodes[index].m_key, key, map->m_key_size) == 0 && replace_if_exists)
    {
      nv_memcpy(map->m_nodes[index].m_value, value, map->m_value_size);
      return;
    }
    if (index == begin)
    {
      SDL_UnlockMutex(map->m_mutex);
      return;
    }
    probe++;
  }

  map->m_nodes[index].m_key   = _nv_hashmap_calloc(map->m_key_size + map->m_value_size);
  map->m_nodes[index].m_value = (char*)map->m_nodes[index].m_key + map->m_key_size;
  map->m_nodes[index].m_hash  = hash;

  nv_memcpy(map->m_nodes[index].m_key, key, map->m_key_size);
  nv_memcpy(map->m_nodes[index].m_value, value, map->m_value_size);
  map->m_size++;

  SDL_UnlockMutex(map->m_mutex);
}

void
nv_hashmap_insert(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value, void* hash_fn_arg)
{
  // TODO: This should not be structured like this????
  _nv_hashmap_insert_internal(map, key, value, 0, hash_fn_arg);
}

void
nv_hashmap_insert_or_replace(nv_hashmap_t* map, const void* NV_RESTRICT key, void* NV_RESTRICT value, void* hash_fn_arg)
{
  _nv_hashmap_insert_internal(map, key, value, 1, hash_fn_arg);
}

void
nv_hashmap_serialize(nv_hashmap_t* map, FILE* f)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->m_mutex);

  const size_t key_size = map->m_key_size;
  const size_t val_size = map->m_value_size;

  for (size_t i = 0; i < map->m_entries; i++)
  {
    if (NV_NODE_OCCUPIED(map->m_nodes[i]))
    {
      void* node_key   = map->m_nodes[i].m_key;
      void* node_value = map->m_nodes[i].m_value;

      fwrite(node_value, val_size, 1, f);
      fwrite(node_key, key_size, 1, f);
    }
  }

  SDL_UnlockMutex(map->m_mutex);
}

void
nv_hashmap_deserialize(nv_hashmap_t* map, FILE* f, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->m_mutex);

  void* key   = nv_malloc(map->m_key_size);
  void* value = nv_malloc(map->m_value_size);

  while (fread(value, map->m_value_size, 1, f) == 1 && fread(key, map->m_key_size, 1, f) == 1)
  {
    SDL_UnlockMutex(map->m_mutex);
    nv_hashmap_insert(map, key, value, hash_fn_arg);
    SDL_LockMutex(map->m_mutex);
  }

  nv_free(key);
  nv_free(value);

  SDL_UnlockMutex(map->m_mutex);
}

// ==============================
// ATLAS
// ==============================

void
nv_texture_atlas_init(size_t width, size_t height, nv_format fmt, int padding, nv_texture_atlas_t* dst)
{
  if (!dst)
  {
    nv_push_error("dst == NULL");
    return;
  }
  if (width == 0 || height == 0 || nv_format_get_bytes_per_pixel(fmt) == 0)
  {
    nv_push_error("invalid size/format");
    return;
  }

  dst->m_canary  = CONT_CANARY;
  dst->m_width   = width;
  dst->m_height  = height;
  dst->m_format  = fmt;
  dst->m_padding = padding;
  dst->m_data    = (unsigned char*)nv_calloc(width * height * nv_format_get_bytes_per_pixel(dst->m_format));
  nv_assert(dst->m_data != NULL);

  dst->m_mutex = SDL_CreateMutex();
  nv_skyline_bin_init(width, height, &dst->m_bin);

  nv_assert(CONT_IS_VALID(dst));
}

SDL_mutex* atlas_resize_mutex = NULL;

int
nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image_t* img, size_t* out_x, size_t* out_y)
{
  if (!atlas || !img || img->m_width <= 0 || img->m_height <= 0 || !out_x || !out_y)
  {
    return 0;
  }
  nv_assert(CONT_IS_VALID(atlas));

  if (!atlas_resize_mutex)
  {
    atlas_resize_mutex = SDL_CreateMutex();
  }

  SDL_LockMutex(atlas->m_mutex);

  nv_skyline_rect_t rect = { .m_width = img->m_width + (2 * atlas->m_padding), .m_height = img->m_height + (2 * atlas->m_padding) };

  size_t x, y;
  bool   packed = nv_skyline_bin_find_best_placement(&atlas->m_bin, &rect, &x, &y);

  while (!packed)
  {
    SDL_UnlockMutex(atlas->m_mutex);

    SDL_LockMutex(atlas_resize_mutex);
    nv_texture_atlas_resize(atlas, 2);
    SDL_UnlockMutex(atlas_resize_mutex);

    SDL_LockMutex(atlas->m_mutex);

    packed = nv_skyline_bin_find_best_placement(&atlas->m_bin, &rect, &x, &y);
  }

  nv_skyline_bin_place_rect(&atlas->m_bin, &rect, x, y);
  *out_x = x + atlas->m_padding;
  *out_y = y + atlas->m_padding;

  nv_image_t dst = { .m_width = atlas->m_width, .m_height = atlas->m_height, .m_format = NOVA_FORMAT_R8, .m_data = atlas->m_data };
  nv_image_overlay(&dst, img, (int)*out_x, (int)*out_y, 0, 0);

  SDL_UnlockMutex(atlas->m_mutex);
  return 1;
}

void
nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale)
{
  nv_assert(CONT_IS_VALID(atlas));
  SDL_LockMutex(atlas->m_mutex);

  if (atlas->m_width == 0 || atlas->m_height == 0)
  {
    nv_push_error("zero size atlas? possible corruption");
    SDL_UnlockMutex(atlas->m_mutex);
    return;
  }

  size_t old_w    = atlas->m_width;
  size_t old_h    = atlas->m_height;
  size_t new_w    = atlas->m_width * scale;
  size_t new_h    = atlas->m_height * scale;
  size_t channels = nv_format_get_bytes_per_pixel(atlas->m_format);

  unsigned char* new_data = nv_calloc(new_w * new_h * channels);
  nv_assert(new_data != NULL);

  if (atlas->m_data)
  {
    for (size_t y = 0; y < old_h; y++)
    {
      size_t src_offset = y * old_w * channels;
      size_t dst_offset = y * new_w * channels;
      nv_memcpy(&new_data[dst_offset], &atlas->m_data[src_offset], old_w * channels);
    }
  }

  nv_free(atlas->m_data);
  atlas->m_data   = new_data;
  atlas->m_width  = new_w;
  atlas->m_height = new_h;

  nv_skyline_bin_resize(&atlas->m_bin, new_w, new_h);

  SDL_UnlockMutex(atlas->m_mutex);
}

int
nv_texture_atlas_finish(nv_texture_atlas_t* atlas)
{
  nv_assert(CONT_IS_VALID(atlas));

  SDL_LockMutex(atlas->m_mutex);

  size_t max_w = 0;
  size_t max_h = 0;

  for (size_t i = 0; i < atlas->m_bin.m_num_rects; i++)
  {
    nv_skyline_rect_t* r = &atlas->m_bin.m_rects[i];
    max_w                = NV_MAX(max_w, r->m_posx + r->m_width);
    max_h                = NV_MAX(max_h, r->m_posy + r->m_height);
  }

  size_t optimal_w = max_w, optimal_h = max_h;

  if ((optimal_w == atlas->m_width && optimal_h == atlas->m_height) || (optimal_w == 0 || optimal_h == 0))
  {
    SDL_UnlockMutex(atlas->m_mutex);
    return 0;
  }

  if (atlas->m_width > optimal_w || atlas->m_height > optimal_h)
  {
    if (max_w == 0 || max_h == 0)
    {
      nv_push_error("0 optimal w/h??");
      return -1;
    }
    size_t channels = nv_format_get_bytes_per_pixel(atlas->m_format);
    if (channels == 0)
    {
      nv_push_error("invalid format?");
      return -1;
    }
    unsigned char* new_data = (unsigned char*)nv_calloc(max_w * max_h * channels);
    if (new_data)
    {
      for (size_t y = 0; y < max_h; y++)
      {
        nv_memcpy(new_data + y * max_w * channels, atlas->m_data + y * atlas->m_width * channels, max_w * channels);
      }
      nv_free(atlas->m_data);
      atlas->m_data   = new_data;
      atlas->m_width  = max_w;
      atlas->m_height = max_h;
    }
  }

  SDL_UnlockMutex(atlas->m_mutex);
  return -1;
}

void
nv_texture_atlas_destroy(nv_texture_atlas_t* atlas)
{
  if (!atlas)
  {
    return;
  }
  nv_assert(CONT_IS_VALID(atlas));

  SDL_LockMutex(atlas->m_mutex);
  if (atlas->m_data)
  {
    nv_free(atlas->m_data);
  }
  nv_skyline_bin_destroy(&atlas->m_bin);
  SDL_UnlockMutex(atlas->m_mutex);

  SDL_DestroyMutex(atlas->m_mutex);
}

// ==============================
// RECTPACK
// ==============================

void
nv_skyline_bin_init(size_t w, size_t h, nv_skyline_bin_t* dst)
{
  if (!dst)
  {
    nv_push_error("dst = NULL");
    return;
  }

  *dst                        = nv_zero_init(nv_skyline_bin_t);
  dst->m_canary               = CONT_CANARY;
  dst->m_width                = w;
  dst->m_height               = h;
  dst->m_skyline              = (size_t*)nv_calloc(w * sizeof(size_t));
  dst->m_rects                = NULL;
  dst->m_num_rects            = 0;
  dst->m_allocated_rect_count = 0;
  dst->m_mutex                = SDL_CreateMutex();

  nv_assert(CONT_IS_VALID(dst));
}

void
nv_skyline_bin_destroy(nv_skyline_bin_t* bin)
{
  if (!bin)
  {
    return;
  }
  nv_assert(CONT_IS_VALID(bin));
  SDL_LockMutex(bin->m_mutex);
  if (bin->m_rects)
  {
    nv_free(bin->m_rects);
  }
  if (bin->m_skyline)
  {
    nv_free(bin->m_skyline);
  }
  SDL_UnlockMutex(bin->m_mutex);
  SDL_DestroyMutex(bin->m_mutex);
}

size_t
nv_skyline_bin_max_height(const nv_skyline_bin_t* bin, size_t x, size_t w)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex((SDL_mutex*)bin->m_mutex);

  size_t max_h = 0;
  for (size_t i = x; i < x + w && i < bin->m_width; i++)
  {
    if (bin->m_skyline[i] > max_h)
    {
      max_h = bin->m_skyline[i];
    }
  }

  SDL_UnlockMutex((SDL_mutex*)bin->m_mutex);
  return max_h;
}

int
nv_skyline_bin_find_best_placement(const nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t* best_x, size_t* best_y)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex((SDL_mutex*)bin->m_mutex);

  size_t min_y = SIZE_MAX;
  *best_x      = SIZE_MAX;
  *best_y      = SIZE_MAX;

  if (rect->m_width > bin->m_width)
  {
    return -1;
  }

  size_t max_x = bin->m_width - rect->m_width;
  for (size_t x = 0; x <= max_x; x++)
  {
    SDL_UnlockMutex((SDL_mutex*)bin->m_mutex);
    size_t y = nv_skyline_bin_max_height(bin, x, rect->m_width);
    if (y + rect->m_height <= bin->m_height && y < min_y)
    {
      min_y   = y;
      *best_x = x;
      *best_y = y;
    }
    SDL_LockMutex((SDL_mutex*)bin->m_mutex);
  }

  SDL_UnlockMutex((SDL_mutex*)bin->m_mutex);
  return (*best_x != SIZE_MAX);
}

void
nv_skyline_bin_place_rect(nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t x, size_t y)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex(bin->m_mutex);

  if (bin->m_num_rects >= bin->m_allocated_rect_count)
  {
    size_t new_alloc = (bin->m_allocated_rect_count == 0) ? 2 : bin->m_allocated_rect_count * 2;

    if (bin->m_rects)
    {
      bin->m_rects = nv_realloc(bin->m_rects, new_alloc * sizeof(nv_skyline_rect_t));
    }
    else
    {
      bin->m_rects = nv_calloc(new_alloc * sizeof(nv_skyline_rect_t));
    }
    bin->m_allocated_rect_count = new_alloc;
  }

  bin->m_rects[bin->m_num_rects++] = (nv_skyline_rect_t){ rect->m_width, rect->m_height, x, y };

  for (size_t i = x; i < x + rect->m_width && i < bin->m_width; i++)
  {
    bin->m_skyline[i] = y + rect->m_height;
  }

  SDL_UnlockMutex(bin->m_mutex);
}

static int
_nv_skyline_compare_rect(const void* rect1, const void* rect2)
{
  size_t rect2_height = ((const nv_skyline_rect_t*)rect2)->m_height;
  size_t rect1_height = ((const nv_skyline_rect_t*)rect1)->m_height;
  return (int)rect2_height - (int)rect1_height;
}

void
nv_skyline_bin_pack_rects(nv_skyline_bin_t* bin, nv_skyline_rect_t* rects, size_t nrects)
{
  nv_assert(CONT_IS_VALID(bin));

  qsort(rects, nrects, sizeof(nv_skyline_rect_t), _nv_skyline_compare_rect);

  for (size_t i = 0; i < nrects; i++)
  {
    size_t x, y;
    if (nv_skyline_bin_find_best_placement(bin, &rects[i], &x, &y))
    {
      nv_skyline_bin_place_rect(bin, &rects[i], x, y);
      rects[i].m_posx = x;
      rects[i].m_posy = y;
    }
    else
    {
      nv_push_error("failed to pack rect %d", (int)i);
    }
  }
}

// Please do not look at this.
// Please
// This is stupid and I can't (just don't) want to find a work around
void
nv_skyline_bin_resize(nv_skyline_bin_t* bin, size_t new_w, size_t new_h)
{
  nv_assert(CONT_IS_VALID(bin));

  nv_skyline_rect_t* valid_rects   = NULL;
  size_t             num_valid     = 0;
  nv_skyline_rect_t* invalid_rects = NULL;
  size_t             num_invalid   = 0;

  SDL_LockMutex(bin->m_mutex);

  for (size_t i = 0; i < bin->m_num_rects; i++)
  {
    nv_skyline_rect_t rect = bin->m_rects[i];
    if (rect.m_posx + rect.m_width > new_w || rect.m_posy + rect.m_height > new_h)
    {
      nv_skyline_rect_t* tmp = NULL;
      if (!invalid_rects)
      {
        tmp = (nv_skyline_rect_t*)nv_calloc(sizeof(nv_skyline_rect_t));
      }
      else
      {
        tmp = (nv_skyline_rect_t*)nv_realloc(invalid_rects, (num_invalid + 1) * sizeof(nv_skyline_rect_t));
      }
      if (!tmp)
      {
        nv_free(valid_rects);
        nv_free(invalid_rects);
        SDL_UnlockMutex(bin->m_mutex);
        return;
      }
      invalid_rects                = tmp;
      invalid_rects[num_invalid++] = rect;
    }
    else
    {
      nv_skyline_rect_t* tmp = (nv_skyline_rect_t*)nv_realloc(valid_rects, (num_valid + 1) * sizeof(nv_skyline_rect_t));
      if (!tmp)
      {
        nv_free(valid_rects);
        nv_free(invalid_rects);
        SDL_UnlockMutex(bin->m_mutex);
        return;
      }
      valid_rects              = tmp;
      valid_rects[num_valid++] = rect;
    }
  }

  if (new_w != bin->m_width)
  {
    size_t* new_skyline = (size_t*)nv_realloc(bin->m_skyline, new_w * sizeof(size_t));
    if (!new_skyline)
    {
      nv_push_error("Memory allocation failed for bin->skyline in nv_skyline_bin_resize");
      nv_free(valid_rects);
      nv_free(invalid_rects);
      SDL_UnlockMutex(bin->m_mutex);
      return;
    }
    // if it's bigger horizontally, clear the new entries
    if (new_w > bin->m_width)
    {
      for (size_t i = bin->m_width; i < new_w; i++)
      {
        new_skyline[i] = 0;
      }
    }
    bin->m_skyline = new_skyline;
  }

  for (size_t i = 0; i < new_w; i++)
  {
    if (bin->m_skyline[i] > new_h)
    {
      bin->m_skyline[i] = new_h;
    }
  }

  for (size_t i = 0; i < num_valid; i++)
  {
    nv_skyline_rect_t rect = valid_rects[i];
    for (size_t x = rect.m_posx; x < rect.m_posx + rect.m_width && x < new_w; x++)
    {
      if (bin->m_skyline[x] < rect.m_posy + rect.m_height)
      {
        bin->m_skyline[x] = rect.m_posy + rect.m_height;
      }
    }
  }

  if (bin->m_rects)
  {
    nv_free(bin->m_rects);
  }
  bin->m_rects                = valid_rects;
  bin->m_num_rects            = num_valid;
  bin->m_allocated_rect_count = num_valid;

  for (size_t i = 0; i < num_invalid; i++)
  {
    size_t x, y;
    if (nv_skyline_bin_find_best_placement(bin, &invalid_rects[i], &x, &y))
    {
      nv_skyline_bin_place_rect(bin, &invalid_rects[i], x, y);
    }
    else
    {
      nv_push_error("failed to repack rect %lu after resize", i);
    }
  }

  if (invalid_rects)
  {
    nv_free(invalid_rects);
  }

  bin->m_width  = new_w;
  bin->m_height = new_h;
  SDL_UnlockMutex(bin->m_mutex);
}

// ==============================
// BITSET
// ==============================

void
nv_bitset_init(int init_capacity, nv_allocator_t* allocator, nv_bitset_t* set)
{
  *set = nv_zero_init(nv_bitset_t);

  set->m_mutex = SDL_CreateMutex();

  if (init_capacity > 0)
  {
    init_capacity = (init_capacity + 7) / 8;
    set->m_size   = init_capacity;
    set->m_alloc  = allocator;
    set->m_data   = set->m_alloc->m_calloc(set->m_alloc, 1, init_capacity * sizeof(uint8_t));
  }
  else
  {
    set->m_size = 0;
  }
}

void
nv_bitset_set_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->m_mutex);
  set->m_data[bitindex / 8] |= (1U << (bitindex % 8U));
  SDL_UnlockMutex(set->m_mutex);
}

void
nv_bitset_set_bit_to(nv_bitset_t* set, int bitindex, nv_bitset_bit to)
{
  to ? nv_bitset_set_bit(set, bitindex) : nv_bitset_clear_bit(set, bitindex);
}

void
nv_bitset_clear_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->m_mutex);
  set->m_data[bitindex / 8] &= ~(1U << (bitindex % 8U));
  SDL_UnlockMutex(set->m_mutex);
}

void
nv_bitset_toggle_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->m_mutex);
  set->m_data[bitindex / 8] ^= (1U << (bitindex % 8U));
  SDL_UnlockMutex(set->m_mutex);
}

nv_bitset_bit
nv_bitset_access_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->m_mutex);
  nv_bitset_bit bit = (set->m_data[bitindex / 8] & (1U << (bitindex % 8U))) != 0;
  SDL_UnlockMutex(set->m_mutex);
  return bit;
}

void
nv_bitset_copy_from(nv_bitset_t* dst, const nv_bitset_t* src)
{
  if (!src->m_data)
  {
    return;
  }
  SDL_LockMutex(dst->m_mutex);
  SDL_LockMutex((SDL_mutex*)src->m_mutex);
  if (src->m_size != dst->m_size && dst->m_data)
  {
    dst->m_alloc->m_free(dst->m_alloc, dst->m_data);
    dst->m_data = src->m_alloc->m_calloc(src->m_alloc, 1, src->m_size);
    dst->m_size = src->m_size;
  }
  if (dst->m_data && src->m_data)
  {
    nv_memcpy(dst->m_data, src->m_data, src->m_size);
  }
  SDL_UnlockMutex(dst->m_mutex);
  SDL_UnlockMutex((SDL_mutex*)src->m_mutex);
}

void
nv_bitset_destroy(nv_bitset_t* set)
{
  SDL_LockMutex(set->m_mutex);
  set->m_alloc->m_free(set->m_alloc, set->m_data);
  SDL_UnlockMutex(set->m_mutex);
}

void
nv_freelist_check_circle(const nv_freelist_t* list)
{
#ifndef NDEBUG
  SDL_LockMutex((SDL_mutex*)list->m_mutex);

  nv_node_t* node = list->m_root;
  nv_node_t* slow = node;
  nv_node_t* fast = node;

  while (fast && fast->m_next)
  {
    slow = slow->m_next;
    fast = fast->m_next->m_next;
    if (fast)
    {
      nv_assert(fast->m_canary == NOVA_ALLOCATION_CANARY);
    }
    if (slow)
    {
      nv_assert(slow->m_canary == NOVA_ALLOCATION_CANARY);
    }

    if (slow == fast)
    {
      nv_log_and_abort("circular freelist\n");
      return;
    }
  }
  SDL_UnlockMutex((SDL_mutex*)list->m_mutex);
#endif
}

nv_node_t*
nv_freelist_mknode(const nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  nv_node_t* node = list->m_alloc_fn(alignment, size);
  nv_assert(node != NULL);
  return node;
}

void
nv_freelist_init(size_t init_size, nv_freelist_alloc_fn alloc_fn, nv_freelist_free_fn free_fn, nv_allocator_t* allocator, nv_freelist_t* list)
{
  *list = nv_zero_init(nv_freelist_t);

  list->m_mutex = SDL_CreateMutex();

  list->m_alloc_fn = alloc_fn;
  list->m_free_fn  = free_fn;
  if (init_size > 0)
  {
    list->m_root         = nv_freelist_mknode(list, 1, init_size);
    list->m_root->m_size = init_size;
  }
  else
  {
    list->m_root = NULL;
  }

  list->m_canary = CONT_CANARY;
  (void)allocator;
  nv_freelist_check_circle(list);
}

void
nv_freelist_destroy(nv_freelist_t* list)
{
  if (!list || !list->m_root)
  {
    return;
  }

  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->m_mutex);

  nv_node_t* node = list->m_root;
  while (node)
  {
    nv_node_t* next = node->m_next;
    if (node->m_in_use)
    {
      SDL_UnlockMutex(list->m_mutex);
      nv_freelist_free(list, node->m_payload);
      SDL_LockMutex(list->m_mutex);
    }
    node = next;
  }
  SDL_UnlockMutex(list->m_mutex);

  nv_freelist_check_circle(list);
}

void*
nv_freelist_alloc(nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->m_mutex);

  nv_node_t* node = list->m_root;
  while (node)
  {
    size_t aligned_node_size = align_up_size(node->m_mapping_size, alignment);
    if (!node->m_in_use && aligned_node_size >= size)
    {
      node->m_in_use  = 1;
      node->m_payload = align_up(node->m_payload, alignment);
      nv_assert(((uintptr_t)node->m_payload % alignment) == 0);
      return node->m_payload;
    }
    node = node->m_next;
  }

  node           = nv_freelist_expand(list, alignment, size);
  node->m_in_use = 1;

  SDL_UnlockMutex(list->m_mutex);

  nv_freelist_check_circle(list);
  nv_assert(((uintptr_t)node->m_payload % alignment) == 0);
  nv_assert(node->m_payload != NULL);
  return node->m_payload;
}

nv_node_t*
nv_freelist_expand(nv_freelist_t* list, size_t alignment, size_t expand_by)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->m_mutex);

  if (!list->m_root)
  {
    list->m_root         = nv_freelist_mknode(list, alignment, expand_by);
    list->m_root->m_size = expand_by;
    SDL_UnlockMutex(list->m_mutex);
    return list->m_root;
  }

  nv_node_t* last_node = list->m_root;
  while (last_node->m_next)
  {
    last_node = last_node->m_next;
  }
  // now we have last_node

  SDL_UnlockMutex(list->m_mutex);

  nv_node_t* new_node = nv_freelist_mknode(list, alignment, expand_by);
  last_node->m_next   = new_node;
  new_node->m_size    = expand_by;
  nv_freelist_check_circle(list);
  return new_node;
}

void
nv_freelist_free(nv_freelist_t* list, void* block)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  if (!block)
  {
    nv_log_info("invalid block\n");
    return;
  }

  SDL_LockMutex(list->m_mutex);

  bool       found = 0;
  nv_node_t* node  = list->m_root;
  nv_node_t* prev  = NULL;

  while (node)
  {
    if (block == node->m_payload)
    {
      found = 1;
      break;
    }
    prev = node;
    node = node->m_next;
  }

  if (!found)
  {
    nv_push_error("no block found");
    return;
  }

  if (!node->m_in_use)
  {
    /* real_t free, yeah */
    nv_push_error("double free");
    return;
  }

  node->m_in_use = 0;

  if (prev)
  {
    prev->m_next = node->m_next;
  }
  else
  {
    list->m_root = node->m_next;
  }

  list->m_free_fn(node);

  SDL_UnlockMutex(list->m_mutex);

  nv_freelist_check_circle(list);
}

nv_node_t*
nv_freelist_find(nv_freelist_t* list, void* alloc)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->m_mutex);

  nv_node_t* node = list->m_root;
  while (node)
  {
    if (node->m_payload == alloc)
    {
      SDL_UnlockMutex(list->m_mutex);
      return node;
    }
    node = node->m_next;
  }
  SDL_UnlockMutex(list->m_mutex);
  nv_freelist_check_circle(list);
  return NULL;
}

// RBMAP

void
nv_rbmap_init(size_t key_size, size_t val_size, nv_compare_fn compare_fn, nv_allocator_t* alloc, nv_rbmap_t* dst)
{
  nv_assert(key_size != 0);
  nv_assert(val_size != 0);
  nv_assert(compare_fn != NULL);
  nv_assert(alloc != NULL);

  *dst              = nv_zero_init(nv_rbmap_t);
  dst->m_canary     = CONT_CANARY;
  dst->m_key_size   = key_size;
  dst->m_val_size   = val_size;
  dst->m_compare_fn = compare_fn;
  dst->m_root       = NULL;
  dst->m_alloc      = alloc;
}

void
nv_rbmap_left_rotate(nv_rbmap_t* map, nv_rbmap_node_t* x)
{
  nv_assert(CONT_IS_VALID(map));
  nv_rbmap_node_t* y = x->m_children[1];
  x->m_children[1]   = y->m_children[0];
  if (y->m_children[0])
  {
    y->m_children[0]->m_parent = x;
  }

  y->m_parent = x->m_parent;
  if (!x->m_parent)
  {
    map->m_root = y;
  }
  else
  {
    x->m_parent->m_children[x == x->m_parent->m_children[1]] = y;
  }

  y->m_children[0] = x;
  x->m_parent      = y;
}

void
nv_rbmap_right_rotate(nv_rbmap_t* map, nv_rbmap_node_t* y)
{
  nv_assert(CONT_IS_VALID(map));
  nv_rbmap_node_t* x = y->m_children[0];
  y->m_children[0]   = x->m_children[1];
  if (x->m_children[1])
  {
    x->m_children[1]->m_parent = y;
  }

  x->m_parent = y->m_parent;
  if (!y->m_parent)
  {
    map->m_root = x;
  }
  else
  {
    y->m_parent->m_children[y == y->m_parent->m_children[1]] = x;
  }

  x->m_children[1] = y;
  y->m_parent      = x;
}

nv_rbmap_node_t*
nv_rbmap_minimum(nv_rbmap_node_t* node)
{
  while (node && node->m_children[0] != NULL)
  {
    node = node->m_children[0];
  }
  return node;
}

void
nv_rbmap_transplant(nv_rbmap_t* map, nv_rbmap_node_t* u, nv_rbmap_node_t* v)
{
  nv_assert(CONT_IS_VALID(map));
  if (u->m_parent == NULL)
  {
    map->m_root = v;
  }
  else if (u == u->m_parent->m_children[0])
  {
    u->m_parent->m_children[0] = v;
  }
  else
  {
    u->m_parent->m_children[1] = v;
  }

  if (v != NULL)
  {
    v->m_parent = u->m_parent;
  }
}

void
nv_rbmap_delete_fixup(nv_rbmap_t* map, nv_rbmap_node_t* x)
{
  nv_assert(CONT_IS_VALID(map));
  while (x != map->m_root && (x == NULL || x->m_color == NOVA_RBNODE_COLOR_BLK))
  {
    if (x == x->m_parent->m_children[0])
    {
      nv_rbmap_node_t* w = x->m_parent->m_children[1];
      if (!w)
      {
        continue;
      }
      if (w && w->m_color == NOVA_RBNODE_COLOR_RED)
      {
        w->m_color           = NOVA_RBNODE_COLOR_BLK;
        x->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_left_rotate(map, x->m_parent);
        w = x->m_parent->m_children[1];
      }
      if (w && (w->m_children[0] == NULL || (w->m_children[0] && w->m_children[0]->m_color == NOVA_RBNODE_COLOR_BLK))
          && (w->m_children[1] == NULL || (w->m_children[1] && w->m_children[1]->m_color == NOVA_RBNODE_COLOR_BLK)))
      {
        w->m_color = NOVA_RBNODE_COLOR_RED;
        x          = x->m_parent;
      }
      else
      {
        if (w->m_children[1] == NULL || w->m_children[1]->m_color == NOVA_RBNODE_COLOR_BLK)
        {
          if (w->m_children[0])
          {
            w->m_children[0]->m_color = NOVA_RBNODE_COLOR_BLK;
          }
          w->m_color = NOVA_RBNODE_COLOR_RED;
          nv_rbmap_right_rotate(map, w);
          w = x->m_parent->m_children[1];
        }
        w->m_color           = x->m_parent->m_color;
        x->m_parent->m_color = NOVA_RBNODE_COLOR_BLK;
        if (w->m_children[1])
        {
          w->m_children[1]->m_color = NOVA_RBNODE_COLOR_BLK;
        }
        nv_rbmap_left_rotate(map, x->m_parent);
        x = map->m_root;
      }
    }
    else
    {
      nv_rbmap_node_t* w = x->m_parent->m_children[0];
      if (w && w->m_color == NOVA_RBNODE_COLOR_RED)
      {
        w->m_color           = NOVA_RBNODE_COLOR_BLK;
        x->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_right_rotate(map, x->m_parent);
        w = x->m_parent->m_children[0];
      }
      if ((w && (w->m_children[0] == NULL || w->m_children[0]->m_color == NOVA_RBNODE_COLOR_BLK)
           && (w->m_children[1] == NULL || w->m_children[1]->m_color == NOVA_RBNODE_COLOR_BLK)))
      {
        w->m_color = NOVA_RBNODE_COLOR_RED;
        x          = x->m_parent;
      }
      else if (w)
      {
        if (w->m_children[0] == NULL || w->m_children[0]->m_color == NOVA_RBNODE_COLOR_BLK)
        {
          if (w->m_children[1])
          {
            w->m_children[1]->m_color = NOVA_RBNODE_COLOR_BLK;
          }
          w->m_color = NOVA_RBNODE_COLOR_RED;
          nv_rbmap_left_rotate(map, w);
          w = x->m_parent->m_children[0];
        }
        w->m_color           = x->m_parent->m_color;
        x->m_parent->m_color = NOVA_RBNODE_COLOR_BLK;
        if (w->m_children[0])
        {
          w->m_children[0]->m_color = NOVA_RBNODE_COLOR_BLK;
        }
        nv_rbmap_right_rotate(map, x->m_parent);
        x = map->m_root;
      }
    }
  }
  if (x)
  {
    x->m_color = NOVA_RBNODE_COLOR_BLK;
  }
}

void
nv_rbmap_delete(nv_rbmap_t* map, nv_rbmap_node_t* z)
{
  nv_assert(CONT_IS_VALID(map));
  nv_assert(z != NULL);

  nv_rbmap_node_t* y                = z;
  int              y_original_color = y->m_color;
  nv_rbmap_node_t* x;

  if (z->m_children[0] == NULL)
  {
    x = z->m_children[1];
    nv_rbmap_transplant(map, z, z->m_children[1]);
  }
  else if (z->m_children[1] == NULL)
  {
    x = z->m_children[0];
    nv_rbmap_transplant(map, z, z->m_children[0]);
  }
  else
  {
    y                = nv_rbmap_minimum(z->m_children[1]);
    y_original_color = y->m_color;
    x                = y->m_children[1];
    if (y->m_parent == z)
    {
      if (x)
      {
        x->m_parent = y;
      }
    }
    else
    {
      nv_rbmap_transplant(map, y, y->m_children[1]);
      y->m_children[1] = z->m_children[1];
      if (y->m_children[1])
      {
        y->m_children[1]->m_parent = y;
      }
    }
    nv_rbmap_transplant(map, z, y);
    y->m_children[0] = z->m_children[0];
    if (y->m_children[0])
    {
      y->m_children[0]->m_parent = y;
    }
    y->m_color = z->m_color;
  }

  if (y_original_color == NOVA_RBNODE_COLOR_BLK && x != NULL)
  {
    nv_rbmap_delete_fixup(map, x);
  }

  map->m_alloc->m_free(map->m_alloc, z);
}

void
nv_rbmap_insert_fixup(nv_rbmap_t* map, nv_rbmap_node_t* z)
{
  nv_assert(CONT_IS_VALID(map));
  nv_assert(z != NULL);

  while (z->m_parent && z->m_parent->m_color == NOVA_RBNODE_COLOR_RED)
  {
    if (z->m_parent == z->m_parent->m_parent->m_children[0])
    {
      nv_rbmap_node_t* uncle = z->m_parent->m_parent->m_children[1];
      if (uncle && uncle->m_color == NOVA_RBNODE_COLOR_RED)
      {
        z->m_parent->m_color           = NOVA_RBNODE_COLOR_BLK;
        uncle->m_color                 = NOVA_RBNODE_COLOR_BLK;
        z->m_parent->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        z                              = z->m_parent->m_parent;
      }
      else
      {
        if (z == z->m_parent->m_children[1])
        {
          z = z->m_parent;
          nv_rbmap_left_rotate(map, z);
        }
        z->m_parent->m_color           = NOVA_RBNODE_COLOR_BLK;
        z->m_parent->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_right_rotate(map, z->m_parent->m_parent);
      }
    }
    else
    {
      nv_rbmap_node_t* uncle = z->m_parent->m_parent->m_children[0];
      if (uncle && uncle->m_color == NOVA_RBNODE_COLOR_RED)
      {
        z->m_parent->m_color           = NOVA_RBNODE_COLOR_BLK;
        uncle->m_color                 = NOVA_RBNODE_COLOR_BLK;
        z->m_parent->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        z                              = z->m_parent->m_parent;
      }
      else
      {
        if (z == z->m_parent->m_children[0])
        {
          z = z->m_parent;
          nv_rbmap_right_rotate(map, z);
        }
        z->m_parent->m_color           = NOVA_RBNODE_COLOR_BLK;
        z->m_parent->m_parent->m_color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_left_rotate(map, z->m_parent->m_parent);
      }
    }
  }
  map->m_root->m_color = NOVA_RBNODE_COLOR_BLK;
}

static inline nv_rbmap_node_t*
_nv_rbmap_allocate_node(nv_rbmap_t* map)
{
  return (nv_rbmap_node_t*)map;
}

void
nv_rbmap_insert(nv_rbmap_t* map, const void* key, const void* value, void* user_hash_data)
{
  (void)_nv_rbmap_allocate_node;
  nv_assert(CONT_IS_VALID(map));
  nv_assert(map != NULL);
  nv_assert(key != NULL);
  nv_assert(value != NULL);

  void* allocation = map->m_alloc->m_calloc(map->m_alloc, 1, sizeof(nv_rbmap_node_t) + map->m_key_size + map->m_val_size);

  nv_rbmap_node_t* z = (nv_rbmap_node_t*)allocation;

  z->m_key = (char*)allocation + sizeof(nv_rbmap_node_t);
  z->m_val = (char*)allocation + sizeof(nv_rbmap_node_t) + map->m_key_size;
  nv_assert(z->m_val != NULL);

  nv_memcpy(z->m_key, key, map->m_key_size);
  nv_memcpy(z->m_val, value, map->m_val_size);

  z->m_color       = NOVA_RBNODE_COLOR_RED;
  z->m_parent      = NULL;
  z->m_children[0] = z->m_children[1] = NULL;

  nv_rbmap_node_t* y = NULL;
  nv_rbmap_node_t* x = map->m_root;
  while (x != NULL)
  {
    y       = x;
    int cmp = map->m_compare_fn(key, x->m_key, map->m_key_size, user_hash_data);
    if (cmp < 0)
    {
      x = x->m_children[0];
    }
    else
    {
      x = x->m_children[1];
    }
  }
  z->m_parent = y;
  if (y == NULL)
  {
    map->m_root = z;
  }
  else if (map->m_compare_fn(key, y->m_key, map->m_key_size, user_hash_data) < 0)
  {
    y->m_children[0] = z;
  }
  else
  {
    y->m_children[1] = z;
  }

  nv_rbmap_insert_fixup(map, z);
}

void
nv_rbmap_iterator_init(nv_rbmap_t* map, nv_rbmap_iterator_t* dst)
{
  nv_assert(dst != NULL);
  nv_assert(map != NULL);

  *dst            = nv_zero_init(nv_rbmap_iterator_t);
  dst->m_current  = map->m_root;
  dst->m_stack    = NULL;
  dst->m_capacity = 0;
  dst->m_top      = 0;
}

void
nv_rbmap_iterator_reserve(nv_rbmap_iterator_t* itr, size_t num_elems)
{
  nv_assert(itr != NULL);
  if (itr->m_stack)
  {
    itr->m_stack = (nv_rbmap_node_t**)nv_realloc(itr->m_stack, num_elems * sizeof(nv_rbmap_node_t*));
  }
  else
  {
    itr->m_stack = (nv_rbmap_node_t**)nv_calloc(num_elems * sizeof(nv_rbmap_node_t*));
  }
  itr->m_capacity = num_elems;
}

nv_rbmap_node_t*
nv_rbmap_iterator_next(nv_rbmap_iterator_t* itr)
{
  while (itr->m_current || itr->m_top > 0)
  {
    if (itr->m_current)
    {
      if (!itr->m_stack || itr->m_top >= itr->m_capacity)
      {
        itr->m_capacity = itr->m_capacity ? itr->m_capacity * 2 : 4;
        itr->m_stack    = (nv_rbmap_node_t**)nv_realloc(itr->m_stack, itr->m_capacity * sizeof(nv_rbmap_node_t*));
      }
      itr->m_stack[itr->m_top++] = itr->m_current;
      itr->m_current             = itr->m_current->m_children[0];
    }
    else
    {
      itr->m_current        = itr->m_stack[--itr->m_top];
      nv_rbmap_node_t* node = itr->m_current;
      itr->m_current        = itr->m_current->m_children[1];
      return node;
    }
  }
  return NULL;
}

void
nv_rbmap_iterator_destroy(nv_rbmap_iterator_t* itr)
{
  if (!itr)
  {
    return;
  }
  if (itr->m_stack)
  {
    nv_free(itr->m_stack);
  }
  itr->m_stack    = NULL;
  itr->m_capacity = 0;
}

void*
nv_rbmap_find(const nv_rbmap_t* map, const void* key, void* user_hash_data)
{
  nv_assert(CONT_IS_VALID(map));

  nv_rbmap_node_t* node = map->m_root;
  while (node != NULL)
  {
    int cmp = map->m_compare_fn(key, node->m_key, map->m_key_size, user_hash_data);
    if (cmp < 0)
    {
      node = node->m_children[0];
    }
    else if (cmp > 0)
    {
      node = node->m_children[1];
    }
    else
    {
      return node->m_val;
    }
  }
  return NULL;
}

void
nv_rbmap_destroy(nv_rbmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));

  nv_rbmap_iterator_t itr;
  nv_rbmap_iterator_init(map, &itr);

  nv_rbmap_node_t** nodes = NULL;
  size_t            count = 0, capacity = 16;
  nodes = (nv_rbmap_node_t**)nv_calloc(capacity * sizeof(nv_rbmap_node_t*));

  nv_rbmap_node_t* node;
  while ((node = nv_rbmap_iterator_next(&itr)) != NULL)
  {
    if (count >= capacity)
    {
      capacity *= 2;
      nodes = (nv_rbmap_node_t**)nv_realloc(nodes, capacity * sizeof(nv_rbmap_node_t*));
    }
    nodes[count++] = node;
  }

  for (size_t i = 0; i < count; i++)
  {
    node = nodes[i];
    map->m_alloc->m_free(map->m_alloc, node);
  }
  nv_free(nodes);
  nv_rbmap_iterator_destroy(&itr);
  map->m_root = NULL;
}

// POOL

int
nv_pool_init(nv_pool_t* pool, size_t type_size, size_t capacity)
{
  if (!pool || type_size == 0 || capacity == 0)
  {
    nv_push_error("invalid args");
    return -1;
  }

  // object_size = ALIGN(object_size);

  pool->m_allocation = nv_calloc(capacity * type_size);
  if (!pool->m_allocation)
  {
    return -1;
  }

  pool->m_free_list = NULL;

  // link all objects to freelist so we can use em
  for (size_t i = 0; i < capacity; i++)
  {
    void* object      = (char*)pool->m_allocation + (i * type_size);
    *(void**)object   = pool->m_free_list; // link next free object
    pool->m_free_list = object;
  }

  pool->m_type_size  = type_size;
  pool->m_capacity   = capacity;
  pool->m_free_count = capacity;

  pool->m_mutex = SDL_CreateMutex();

  return 0;
}

void
nv_pool_destroy(nv_pool_t* pool)
{
  if (!pool)
  {
    return;
  }

  nv_free(pool->m_allocation);

  SDL_DestroyMutex(pool->m_mutex);

  pool->m_allocation = NULL;
  pool->m_free_list  = NULL;
  pool->m_free_count = 0;
  pool->m_capacity   = 0;
  pool->m_type_size  = 0;
}

void*
nv_pool_alloc(nv_pool_t* pool)
{
  if (!pool)
  {
    return NULL;
  }

  SDL_LockMutex(pool->m_mutex);

  if (pool->m_free_count == 0)
  {
    SDL_UnlockMutex(pool->m_mutex);
    return NULL; // pool full
  }

  void* object      = pool->m_free_list;
  pool->m_free_list = *(void**)object; // unlink from linked list
  pool->m_free_count--;

  SDL_UnlockMutex(pool->m_mutex);

  return object;
}

void
nv_pool_free(nv_pool_t* pool, void* object)
{
  if (!pool || !object)
  {
    return;
  }

  SDL_LockMutex(pool->m_mutex);

  *(void**)object   = pool->m_free_list;
  pool->m_free_list = object;
  pool->m_free_count++;

  SDL_UnlockMutex(pool->m_mutex);
}
