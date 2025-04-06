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

#include "common/format.h"
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
#include "std/errorcodes.h"
#include "std/math/math.h"
#include "std/stdafx.h"

/* find last quote in a line: "([^"]+)"(?!.*") */

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

static inline const char*
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
  const int src_channels = nv_format_get_num_channels(src->format);
  nv_assert(src_channels < dst_channels);

  uint8_t* dst = nv_calloc(src->width * src->height * dst_channels * sizeof(uchar));

  for (size_t y = 0; y < src->height; y++)
  {
    for (size_t x = 0; x < src->width; x++)
    {
      for (int c = 0; c < dst_channels; c++)
      {
        if (c < src_channels)
        {
          dst[(((y * src->width) + x) * dst_channels) + c] = src->data[(((y * src->width) + x) * src_channels) + c];
        }
        else
        {
          if (c == 3)
          { // alpha channel
            dst[(((y * src->width) + x) * dst_channels) + c] = __UINT8_MAX__;
          }
          else
          {
            dst[(((y * src->width) + x) * dst_channels) + c] = 0;
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

  const int src_channels = nv_format_get_num_channels(src->format);

  for (ssize_t y = src_y_offset; y < (ssize_t)src->height; y++)
  {
    for (ssize_t x = src_x_offset; x < (ssize_t)src->width; x++)
    {
      ssize_t dst_x = dst_x_offset + (x - src_x_offset);
      ssize_t dst_y = dst_y_offset + (y - src_y_offset);

      if (dst_x >= 0 && dst_x < (ssize_t)dst->width && dst_y >= 0 && dst_y < (ssize_t)dst->height)
      {
        size_t src_i = (y * src->width + x) * src_channels;
        size_t dst_i = (dst_y * dst->width + dst_x) * src_channels;

        for (int c = 0; c < src_channels; c++)
        {
          dst->data[dst_i + c] = src->data[src_i + c];
        }
      }
    }
  }

  return 0;
}

void
nv_image_enlarge(nv_image_t* dst, const nv_image_t* src, int scale)
{
  size_t new_w = src->width * scale;

  nv_assert(dst->data != NULL);

  uchar*       write = dst->data;
  const uchar* read  = src->data;

  int bpp = nv_format_get_bytes_per_pixel(src->format); // bytes per pixel
  for (size_t y = 0; y < src->height; y++)
  {
    for (size_t x = 0; x < src->width; x++)
    {
      size_t src_i = (y * src->width + x) * bpp;
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
  const int nchannels = nv_format_get_num_channels(src->format);

  dst->width  = (size_t)((flt_t)src->width / scale);
  dst->height = (size_t)((flt_t)src->width / scale);
  dst->format = src->format;
  dst->data   = nv_calloc(dst->width * dst->height * nv_format_get_bytes_per_pixel(dst->format));

  // Calculate the ratios for x and y coordinates
  flt_t x_ratio, y_ratio;
  if (dst->width > 1)
  {
    x_ratio = ((flt_t)src->width - 1.0F) / ((flt_t)dst->width - 1.0F);
  }
  else
  {
    x_ratio = 0;
  }

  if (dst->height > 1)
  {
    y_ratio = ((flt_t)src->height - 1.0F) / ((flt_t)dst->height - 1.0F);
  }
  else
  {
    y_ratio = 0;
  }

  for (size_t y = 0; y < dst->height; y++)
  {
    const flt_t ratiod_y = y_ratio * (flt_t)y;
    flt_t       y_l      = floorf(ratiod_y);
    flt_t       y_h      = ceilf(ratiod_y);
    flt_t       y_weight = (ratiod_y)-y_l;

    const size_t y_l_offset = (size_t)y_l * src->width * nchannels;
    const size_t y_h_offset = (size_t)y_h * src->width * nchannels;

    for (size_t x = 0; x < dst->width; x++)
    {
      const flt_t ratiod_x = x_ratio * (flt_t)x;

      flt_t x_l      = floorf(ratiod_x);
      flt_t x_h      = ceilf(ratiod_x);
      flt_t x_weight = (ratiod_x)-x_l;

      const size_t x_l_offset = (size_t)x_l * nchannels;
      const size_t x_h_offset = (size_t)x_h * nchannels;

      uchar* top_left_pixel     = &src->data[y_l_offset + x_l_offset];
      uchar* top_right_pixel    = &src->data[y_l_offset + x_h_offset];
      uchar* bottom_left_pixel  = &src->data[y_h_offset + x_l_offset];
      uchar* bottom_right_pixel = &src->data[y_h_offset + x_h_offset];
      for (int c = 0; c < nchannels; c++)
      {
        flt_t pixel = (flt_t)top_left_pixel[c] * (1.0F - x_weight) * (1.0F - y_weight) + (flt_t)top_right_pixel[c] * x_weight * (1.0F - y_weight)
            + (flt_t)bottom_left_pixel[c] * y_weight * (1.0F - x_weight) + (flt_t)bottom_right_pixel[c] * x_weight * y_weight;

        dst->data[(y * dst->width + x) * nchannels + c] = (unsigned char)NVM_CLAMP(pixel, 0.0f, 255.0f);
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

  texture.width       = png_get_image_width(png, info);
  texture.height      = png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth  = png_get_bit_depth(png, info);

  if (texture.width == 0 || texture.height == 0)
  {
    nv_log_error("zero w/h\n");
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
    case 1: texture.format = NOVA_FORMAT_R8; break;
    case 2: texture.format = NOVA_FORMAT_RG8; break;
    case 3: texture.format = NOVA_FORMAT_RGB8; break;
    case 4: texture.format = NOVA_FORMAT_RGBA8; break;
    default:
      nv_log_error("unsupported file(png) format: channels = %d\n", channels);
      fclose(f);
      png_destroy_read_struct(&png, &info, NULL);
      return nv_zero_init(nv_image_t);
      break;
  }

  size_t rowbytes = png_get_rowbytes(png, info);
  texture.data    = (unsigned char*)nv_malloc(rowbytes * texture.height * channels);
  nv_assert(texture.data != NULL);

  u8** row_pointers = nv_malloc(sizeof(u8*) * texture.height);
  for (size_t y = 0; y < texture.height; y++)
  {
    row_pointers[y] = texture.data + y * texture.width * nv_format_get_bytes_per_pixel(texture.format);
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
    nv_log_error("invalid input path (NULL)\n");
    return img;
  }

  if ((f = fopen(path, "rb")) == NULL)
  {
    nv_log_error("couldn't open file \"%s\". Are you sure that it exists?", path);
    return img;
  }

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);

  jpeg_stdio_src(&cinfo, f);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
  {
    nv_log_error("failed to read JPEG header from \"%s\"", path);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  jpeg_start_decompress(&cinfo);

  img.width  = cinfo.output_width;
  img.height = cinfo.output_height;

  switch (cinfo.output_components)
  {
    case 1: img.format = NOVA_FORMAT_R8; break;
    case 3: img.format = NOVA_FORMAT_RGB8; break;
    default:
      nv_log_error("invalid number of channels: %d\n", cinfo.output_components);
      jpeg_destroy_decompress(&cinfo);
      fclose(f);
      return img;
  }

  const size_t bytes_per_pixel = nv_format_get_bytes_per_pixel(img.format);
  if (bytes_per_pixel == 0)
  {
    nv_log_error("invalid bytes per pixel for format.\n");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  img.data = (unsigned char*)nv_malloc(img.width * img.height * bytes_per_pixel);
  if (!img.data)
  {
    nv_log_error("malloc for imagedata failed\n");
    jpeg_destroy_decompress(&cinfo);
    fclose(f);
    return img;
  }

  unsigned char* bufarr[1];
  for (int i = 0; i < (int)cinfo.output_height; i++)
  {
    bufarr[0] = img.data + i * img.width * bytes_per_pixel;
    if (jpeg_read_scanlines(&cinfo, bufarr, 1) != 1)
    {
      nv_log_error("failed to read scanline %d\n", i);
      nv_free(img.data);
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
  if (tex == NULL || path == NULL || tex->data == NULL)
  {
    return;
  }

  FILE* f = fopen(path, "wb");
  if (!f)
  {
    nv_log_error("Failed to open file: %s\n", path);
    return;
  }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
  {
    nv_log_error("png error\n");
    fclose(f);
    return;
  }

  png_infop info = png_create_info_struct(png);
  if (!info)
  {
    nv_log_error("png error\n");
    png_destroy_write_struct(&png, NULL);
    fclose(f);
    return;
  }

  if (setjmp(png_jmpbuf(png)))
  {
    nv_log_error("setjmp error\n");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_init_io(png, f);

  const int numc    = nv_format_get_num_channels(tex->format);
  int       coltype = -1;
  switch (numc)
  {
    case 1: coltype = PNG_COLOR_TYPE_GRAY; break;
    case 3: coltype = PNG_COLOR_TYPE_RGB; break;
    case 4: coltype = PNG_COLOR_TYPE_RGBA; break;
    default:
      nv_log_error("Unsupported number of channels: %i\n", numc);
      png_destroy_write_struct(&png, &info);
      fclose(f);
      return;
  }

  const int bytesperpixel = nv_format_get_bytes_per_pixel(tex->format);
  if (bytesperpixel <= 0)
  {
    nv_log_error("invalid bytes per pixel: %i\n", bytesperpixel);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  png_set_IHDR(png, info, tex->width, tex->height, bytesperpixel * 8, coltype, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png, info);

  png_bytep* row_pointers = (png_bytep*)nv_malloc(sizeof(png_bytep) * tex->height);
  if (!row_pointers)
  {
    nv_log_error("malloc row_pointers failed\n");
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return;
  }

  for (size_t y = 0; y < tex->height; y++)
  {
    row_pointers[y] = tex->data + y * tex->width * bytesperpixel;
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
  size_t   size;
  unsigned canary;
} sablock;

void
nv_allocator_stack_init(nv_allocator_stack* allocator, unsigned char* buf, size_t available)
{
  allocator->buf       = buf;
  allocator->bufsiz    = available;
  allocator->bufoffset = 0;
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
  if (prevblockp->size >= size)
  {
    return prevblock;
  }
  if (prevblockp->canary != NOVA_ALLOCATION_CANARY)
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
  nv_memcpy(new_data, prevblock, prevblockp->size);
  nv_stack_free(parent, prevblock);

  return new_data;
}

void*
nv_stack_alloc(nv_allocator_t* parent, size_t alignment, size_t size)
{
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->context;
  size                          = align_up_size(size, alignment);
  if ((allocator->bufoffset + size + sizeof(sablock)) > allocator->bufsiz)
  {
    nv_log_error("oom\n"); // out of memory
    return NULL;
  }

  sablock* block = align_up(allocator->buf + allocator->bufoffset, alignment);
  block->size    = size;
  block->canary  = NOVA_ALLOCATION_CANARY;
  block++; // move past the header, so return is the memory after header
  allocator->bufoffset += size + sizeof(sablock);
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
  nv_allocator_stack* allocator = (nv_allocator_stack*)parent->context;
  sablock*            p         = (sablock*)block;
  p--;
  nv_assert(p->canary == NOVA_ALLOCATION_CANARY);
  void* allocator_last_block = (allocator->buf + allocator->bufoffset - p->size - sizeof(sablock));
  if (block != allocator_last_block)
  {
    return;
  }
  allocator->bufoffset -= p->size + sizeof(sablock);
}

nv_allocator_t*
nv_allocator_get_default(void)
{
  static nv_allocator_t nv_allocator_default;
  // if the allocator was corrupted by a function, we'll get foked
  nv_allocator_default.alloc     = nv_heap_alloc;
  nv_allocator_default.calloc    = nv_heap_calloc;
  nv_allocator_default.realloc   = nv_heap_realloc;
  nv_allocator_default.free      = nv_heap_free;
  nv_allocator_default.context   = NULL;
  nv_allocator_default.user_data = NULL;
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
  nv_assert_and_ret(orig != NULL, NULL);

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
  nv_assert_and_ret(orig != NULL, NULL);

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
static inline nv_node_t*
heap_alloc_internal(size_t alignment, size_t size)
{
  nv_assert((alignment & (alignment - 1)) == 0 && alignment > 0);
  nv_assert(size < SIZE_MAX - alignment - sizeof(nv_node_t) - sizeof(unsigned));

  size += sizeof(nv_node_t);

  size_t total_size = align_up_size(size, alignment);

  if (total_size <= 0)
  {
    nv_log_error("zero size malloc\n\n");
    return NULL;
  }

  nv_assert_and_ret(0, NULL);

  // void* mapping = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  // if (mapping == MAP_FAILED || !mapping)
  // {
  // nv_log_error("mmap failed: %s\n\n", strerror(*(__errno_location())));
  // return NULL;
  // }
  void* mapping = NULL;

  nv_chunk_t* chk   = (nv_chunk_t*)mapping;
  chk->mapping      = mapping;
  chk->mapping_size = total_size;
  chk->available    = total_size;

  nv_node_t* p = (nv_node_t*)((nv_chunk_t*)mapping + 1);
  *p           = (nv_node_t){
              .payload      = align_up(p + 1, alignment),
              .mapping      = mapping,
              .mapping_size = total_size,
              .canary       = NOVA_ALLOCATION_CANARY,
              .in_use       = 1,
  };
  chk->root = p;
  return p;
}

static inline void
heap_free_node_internal(nv_node_t* node)
{
  if (!node)
  {
    nv_log_and_abort("invalid ptr\n\n");
    return;
  }

  if (node->canary != NOVA_ALLOCATION_CANARY)
  {
    nv_log_and_abort("memory is corrupt\n\n");
    return;
  }

  // void*  mapping = node->mapping;
  // size_t size    = node->mapping_size;
  *node = nv_zero_init(nv_node_t);

  nv_assert(0);

  // if (munmap(mapping, size) == -1)
  if (false)
  {
    nv_log_error("munmap failed: %s\n\n", strerror(*(__errno_location())));
    return;
  }
}

void
nv_allocator_heap_init(nv_allocator_heap* pool)
{
  nv_freelist_init(0, heap_alloc_internal, heap_free_node_internal, nv_allocator_get_default(), &pool->freelist);
}

// deadbeef is for losers
#define CONT_CANARY 0xFEEF

#define CONT_IS_VALID(cont) ((cont) && ((cont)->canary == CONT_CANARY))

// ==============================
// VECTOR
// ==============================

nv_errorc
nv_list_init(size_t typesize, size_t init_capacity, nv_allocator_t* allocator, nv_list_t* vec)
{
  nv_assert_and_ret(typesize > 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(allocator != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  *vec          = nv_zero_init(nv_list_t);
  vec->size     = 0;
  vec->typesize = typesize;
  vec->canary   = CONT_CANARY;

  vec->mutex = SDL_CreateMutex();
  if (!vec->mutex)
  {
    return NOVA_ERROR_CODE_EXTERNAL;
  }

  vec->alloc = allocator;

  SDL_LockMutex(vec->mutex);
  if (init_capacity > 0)
  {
    vec->data     = vec->alloc->calloc(vec->alloc, 1, vec->typesize * init_capacity);
    vec->capacity = init_capacity;
  }
  else
  {
    vec->data = NULL;
  }
  SDL_UnlockMutex(vec->mutex);

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_list_destroy(nv_list_t* vec)
{
  if (vec)
  {
    SDL_LockMutex(vec->mutex);
    nv_assert(CONT_IS_VALID(vec));
    if (vec->data)
    {
      vec->alloc->free(vec->alloc, vec->data);
      SDL_UnlockMutex(vec->mutex);
      SDL_DestroyMutex(vec->mutex);
    }
  }
}

void
nv_list_clear(nv_list_t* vec)
{
  SDL_LockMutex(vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  vec->size = 0;
  SDL_UnlockMutex(vec->mutex);
}

size_t
nv_list_size(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t sz = vec->size;
  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  return sz;
}

size_t
nv_list_capacity(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t cap = vec->capacity;
  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  return cap;
}

size_t
nv_list_typesize(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  size_t tsize = vec->typesize;
  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  return tsize;
}

void*
nv_list_data(const nv_list_t* vec)
{
  SDL_LockMutex((SDL_mutex*)vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* ptr = vec->data;
  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  return ptr;
}

void*
nv_list_front(nv_list_t* vec)
{
  SDL_LockMutex(vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* ptr = nv_list_get(vec, 0);
  SDL_UnlockMutex(vec->mutex);
  return ptr;
}

void*
nv_list_back(nv_list_t* vec)
{
  SDL_LockMutex(vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  void* ptr = nv_list_get(vec, NV_MAX(1ULL, vec->size) - 1); // stupid but works
  // that's how I'd describe the entirety of this projetc
  SDL_UnlockMutex(vec->mutex);
  return ptr;
}

void*
nv_list_get(const nv_list_t* vec, size_t i)
{
  SDL_LockMutex((SDL_mutex*)vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  uchar* data     = vec->data;
  size_t typesize = vec->typesize;
  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  if (!data)
  {
    return NULL;
  }
  return data + (typesize * i);
}

void
nv_list_set(nv_list_t* vec, size_t i, void* elem)
{
  SDL_LockMutex(vec->mutex);
  nv_assert(CONT_IS_VALID(vec));
  nv_memcpy((char*)vec + (vec->typesize * i), elem, vec->typesize);
  SDL_UnlockMutex(vec->mutex);
}

void
nv_list_copy_from(const nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst)
{
  SDL_LockMutex((SDL_mutex*)src->mutex);
  SDL_LockMutex(dst->mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src->typesize == dst->typesize);
  if (src->size >= dst->capacity)
  {
    nv_list_resize(dst, src->size);
  }
  dst->size = src->size;
  nv_memcpy(dst->data, src->data, src->size * src->typesize);

  SDL_UnlockMutex((SDL_mutex*)src->mutex);
  SDL_UnlockMutex(dst->mutex);
}

void
nv_list_move_from(nv_list_t* NV_RESTRICT src, nv_list_t* NV_RESTRICT dst)
{
  SDL_LockMutex(src->mutex);
  SDL_LockMutex(dst->mutex);

  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  dst->size     = src->size;
  dst->capacity = src->capacity;
  dst->data     = src->data;

  src->size     = 0;
  src->capacity = 0;
  src->data     = NULL;

  SDL_UnlockMutex(src->mutex);
  SDL_UnlockMutex(dst->mutex);
}

bool
nv_list_empty(const nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));
  return (vec->size == 0);
}

bool
nv_list_equal(const nv_list_t* vec1, const nv_list_t* vec2)
{
  nv_assert(CONT_IS_VALID(vec1));
  nv_assert(CONT_IS_VALID(vec2));

  SDL_LockMutex((SDL_mutex*)vec1->mutex);
  SDL_LockMutex((SDL_mutex*)vec2->mutex);

  bool equal = 1;
  if ((vec1->size != vec2->size || vec1->typesize != vec2->typesize) || (nv_memcmp(vec1->data, vec2->data, vec1->size * vec1->typesize) != 0))
  {
    equal = 0;
  }

  SDL_UnlockMutex((SDL_mutex*)vec1->mutex);
  SDL_UnlockMutex((SDL_mutex*)vec2->mutex);

  return equal;
}

void
nv_list_resize(nv_list_t* vec, size_t new_size)
{
  nv_assert(CONT_IS_VALID(vec));

  if (vec->data)
  {
    vec->data = vec->alloc->realloc(vec->alloc, vec->data, 1, vec->typesize * new_size);
  }
  else
  {
    vec->data = vec->alloc->calloc(vec->alloc, 1, vec->typesize * new_size);
  }
  nv_assert(vec->data != NULL);

  vec->capacity = new_size;
}

void
nv_list_push_back(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (vec->size >= vec->capacity)
  {
    nv_list_resize(vec, NV_MAX(1, vec->capacity * 2));
  }

  nv_assert(vec->data != NULL);
  nv_assert(!(elem >= vec->data && (unsigned char*)elem <= ((unsigned char*)vec->data + vec->size))); // breaks restriction rules
  nv_memcpy((uchar*)vec->data + (vec->size * vec->typesize), elem, vec->typesize);
  vec->size++;

  SDL_UnlockMutex(vec->mutex);
}

void*
nv_list_push_empty(nv_list_t* __restrict vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (vec->size >= vec->capacity)
  {
    nv_list_resize(vec, NV_MAX(1, vec->capacity * 2));
  }

  nv_assert(vec->data != NULL);
  void* p = (uchar*)vec->data + (vec->size * vec->typesize);
  nv_memset(p, 0, vec->typesize);
  vec->size++;

  SDL_UnlockMutex(vec->mutex);

  return p;
}

void
nv_list_push_set(nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT arr, size_t count)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  size_t required_capacity = vec->size + count;
  if (required_capacity >= vec->capacity)
  {
    nv_list_resize(vec, required_capacity);
  }
  nv_memcpy((uchar*)vec->data + (vec->size * vec->typesize), arr, count * vec->typesize);
  vec->size += count;

  SDL_UnlockMutex(vec->mutex);
}

void
nv_list_pop_back(nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (vec->size > 0)
  {
    vec->size--;
  }

  SDL_UnlockMutex(vec->mutex);
}

void
nv_list_pop_front(nv_list_t* vec)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (vec->size > 0)
  {
    vec->size--;
    nv_memcpy(vec->data, (uchar*)vec->data + vec->typesize, vec->size * vec->typesize);
  }

  SDL_UnlockMutex(vec->mutex);
}

void
nv_list_insert(nv_list_t* NV_RESTRICT vec, size_t index, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (index >= vec->capacity)
  {
    nv_list_resize(vec, NV_MAX(1, index * 2));
  }
  if (index >= vec->size)
  {
    vec->size = index + 1;
  }
  nv_memcpy((uchar*)vec->data + (vec->typesize * index), elem, vec->typesize);

  SDL_UnlockMutex(vec->mutex);
}

void
nv_list_remove(nv_list_t* vec, size_t index)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  if (index >= vec->size)
  {
    return;
  }

  if (vec->size - index - 1)
  {
    // please don't ask me what this is
    nv_memcpy((uchar*)vec->data + (index * vec->typesize), (uchar*)vec->data + ((index + 1) * vec->typesize), (vec->size - index - 1) * vec->typesize);
  }
  vec->size--;

  SDL_UnlockMutex(vec->mutex);
}

int
nv_list_find(const nv_list_t* NV_RESTRICT vec, const void* NV_RESTRICT elem)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex((SDL_mutex*)vec->mutex);

  for (int i = 0; i < (int)vec->size; i++)
  {
    if (nv_memcmp((unsigned char*)vec->data + (i * vec->typesize), elem, vec->typesize))
    {
      SDL_UnlockMutex((SDL_mutex*)vec->mutex);
      return i;
    }
  }

  SDL_UnlockMutex((SDL_mutex*)vec->mutex);
  return -1;
}

void
nv_list_sort(nv_list_t* vec, nv_list_compare_fn compare)
{
  nv_assert(CONT_IS_VALID(vec));

  SDL_LockMutex(vec->mutex);

  qsort(vec->data, vec->size, vec->typesize, compare);

  SDL_UnlockMutex(vec->mutex);
}

// ==============================
// STRING
// ==============================

#define _nv_string_alloc(size) str->alloc->calloc(str->alloc, 1, size)
#define _nv_string_calloc(size) str->alloc->calloc(str->alloc, 1, size)
#define _nv_string_realloc(prevblock, size) str->alloc->realloc(str->alloc, prevblock, 1, size)
#define _nv_string_free(size) str->alloc->free(str->alloc, size)

static void
nv_string_resize(nv_string_t* str, size_t new_capacity)
{
  char* new_data = _nv_string_realloc(str->data, new_capacity);
  nv_assert(new_data != NULL);
  str->data     = new_data;
  str->capacity = new_capacity;
}

nv_string_t
nv_string_init(size_t initial_size, nv_allocator_t* allocator)
{
  nv_string_t str = nv_zero_init(nv_string_t);

  str.mutex = SDL_CreateMutex();

  str.alloc    = allocator;
  str.capacity = (initial_size > 0) ? initial_size : 1;
  str.data     = str.alloc->calloc(str.alloc, 1, str.capacity);
  str.canary   = CONT_CANARY;
  nv_assert(str.data != NULL);

  str.data[0] = 0;
  str.size    = 0;
  return str;
}

nv_string_t
nv_string_init_str(const char* init, nv_allocator_t* allocator)
{
  nv_assert(init != NULL && nv_strlen(init) > 0);
  nv_string_t str = nv_zero_init(nv_string_t);
  str.alloc       = allocator;
  size_t len      = nv_strlen(init);
  str.capacity    = len + 1;
  str.data        = str.alloc->calloc(str.alloc, 1, str.capacity);
  str.canary      = CONT_CANARY;
  nv_assert(str.data != NULL);

  nv_strcpy(str.data, init);
  str.size = len;

  str.mutex = SDL_CreateMutex();

  return str;
}

nv_string_t
nv_string_substring(const nv_string_t* str, size_t start, size_t length, nv_allocator_t* new_allocator)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(start + length <= str->size);

  nv_string_t substr = nv_string_init(length + 1, new_allocator);

  nv_strlcpy(substr.data, str->data + start, length + 1);
  substr.data[length] = 0;
  substr.size         = length;
  return substr;
}

void
nv_string_destroy(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex(str->mutex);
  if (str)
  {
    _nv_string_free(str->data);
    SDL_UnlockMutex(str->mutex);
    SDL_DestroyMutex(str->mutex);
  }
}

void
nv_string_clear(nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex(str->mutex);
  if (str)
  {
    str->size    = 0;
    str->data[0] = 0;
  }
  SDL_UnlockMutex(str->mutex);
}

size_t
nv_string_length(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->mutex);
  size_t size = str->size;
  SDL_UnlockMutex((SDL_mutex*)str->mutex);
  return size;
}

size_t
nv_string_capacity(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->mutex);
  size_t capacity = str->capacity;
  SDL_UnlockMutex((SDL_mutex*)str->mutex);
  return capacity;
}

const char*
nv_string_data(const nv_string_t* str)
{
  nv_assert(CONT_IS_VALID(str));
  SDL_LockMutex((SDL_mutex*)str->mutex);
  const char* data = str->data;
  SDL_UnlockMutex((SDL_mutex*)str->mutex);
  return data;
}

void
nv_string_append(nv_string_t* str, const char* suffix)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(suffix != NULL);

  size_t suffix_length = nv_strlen(suffix);
  if (nv_string_length(str) + suffix_length + 1 > str->capacity)
  {
    nv_string_resize(str, str->size + suffix_length + 1);
  }

  SDL_LockMutex(str->mutex);
  nv_strcpy(str->data + str->size, suffix);
  str->size += suffix_length;
  SDL_UnlockMutex(str->mutex);
}

void
nv_string_append_char(nv_string_t* str, char suffix)
{
  nv_assert(CONT_IS_VALID(str));

  SDL_LockMutex(str->mutex);

  if (str->size + 2 > str->capacity)
  {
    SDL_UnlockMutex(str->mutex);
    nv_string_resize(str, str->size + 2);
    SDL_LockMutex(str->mutex);
  }

  str->data[str->size] = suffix;
  str->size++;
  str->data[str->size] = 0;

  SDL_UnlockMutex(str->mutex);
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

  SDL_LockMutex(str->mutex);
  nv_memmove(str->data + prefix_length, str->data, str->size + 1);
  nv_memcpy(str->data, prefix, prefix_length);
  str->size += prefix_length;
  SDL_UnlockMutex(str->mutex);
}

void
nv_string_set(nv_string_t* str, const char* new_str)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(new_str != NULL);

  size_t new_length = nv_strlen(new_str);
  if (new_length + 1 > str->capacity)
  {
    nv_string_resize(str, new_length + 1);
  }

  SDL_LockMutex(str->mutex);

  nv_strcpy(str->data, new_str);
  str->size = new_length;

  SDL_UnlockMutex(str->mutex);
}

size_t
nv_string_find(const nv_string_t* str, const char* substr)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(substr != NULL);

  SDL_LockMutex((SDL_mutex*)str->mutex);
  char*  pos = nv_strstr(str->data, substr);
  size_t ret = pos ? (size_t)(pos - str->data) : (size_t)-1;
  SDL_UnlockMutex((SDL_mutex*)str->mutex);
  return ret;
}

void
nv_string_remove(nv_string_t* str, size_t index, size_t length)
{
  nv_assert(CONT_IS_VALID(str));
  nv_assert(index < str->size);

  SDL_LockMutex(str->mutex);

  if (index + length > str->size)
  {
    length = str->size - index;
  }

  nv_memmove(str->data + index, str->data + index + length, str->size - index - length + 1);
  str->size -= length;

  SDL_UnlockMutex(str->mutex);
}

void
nv_string_copy_from(const nv_string_t* src, nv_string_t* dst)
{
  nv_assert(CONT_IS_VALID(src));
  nv_assert(CONT_IS_VALID(dst));

  nv_assert(src != NULL);
  nv_assert(dst != NULL);
  nv_string_set(dst, src->data);
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

static inline u32
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

static inline u32
power_of_two_mod(u32 num, u32 mod_by)
{
  return num & (mod_by - 1);
}

#define _nv_hashmap_alloc(size) map->alloc->calloc(map->alloc, 1, size)
#define _nv_hashmap_calloc(size) map->alloc->calloc(map->alloc, 1, size)
#define _nv_hashmap_free(block) map->alloc->free(map->alloc, block);

#define NV_NODE_OCCUPIED(node) ((node).key != NULL && (node).value != NULL)

nv_errorc
nv_hashmap_init(size_t init_size, size_t key_size, size_t value_size, nv_hash_fn hash_fn, nv_allocator_t* allocator, nv_hashmap_t* dst)
{
  nv_assert_and_ret(dst != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(key_size > 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(value_size > 0, NOVA_ERROR_CODE_INVALID_ARG);

  *dst = nv_zero_init(nv_hashmap_t);

  dst->mutex = SDL_CreateMutex();
  if (!dst->mutex)
  {
    return NOVA_ERROR_CODE_EXTERNAL;
  }

  // TODO: Is this needed?
  SDL_LockMutex(dst->mutex);

  dst->alloc = allocator;
  dst->nodes = init_size == 0 ? NULL : (nv_hashmap_node_t*)allocator->calloc(allocator, 1, init_size * sizeof(nv_hashmap_node_t));
  nv_assert_and_ret(dst->nodes != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  dst->hash_fn    = hash_fn ? hash_fn : nv_hash_murmur3;
  dst->key_size   = key_size;
  dst->value_size = value_size;
  dst->entries    = next_power_of_two(init_size);
  dst->size       = 0;
  dst->canary     = CONT_CANARY;

  SDL_UnlockMutex(dst->mutex);

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_hashmap_destroy(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->mutex);
  if (map->nodes)
  {
    for (size_t idx = 0; idx < map->entries; idx++)
    {
      nv_hashmap_node_t* node = &map->nodes[idx];
      if (NV_NODE_OCCUPIED(*node))
      {
        _nv_hashmap_free(node->key);
        node->key = NULL;
      }
    }
    _nv_hashmap_free((void*)map->nodes);
    map->nodes = NULL;
  }
  SDL_UnlockMutex(map->mutex);
  SDL_DestroyMutex(map->mutex);
}

void
nv_hashmap_resize(nv_hashmap_t* map, size_t new_size, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->mutex);

  nv_hashmap_node_t* old_nodes     = map->nodes;
  const size_t       old_m_entries = map->entries;

  if (new_size <= 0)
  {
    new_size = 1;
  }

  map->entries = next_power_of_two(new_size);
  map->size    = 0;

  // we can't do realloc here because we need to rehash all the nodes
  map->nodes = (nv_hashmap_node_t*)_nv_hashmap_calloc(new_size * sizeof(nv_hashmap_node_t));
  nv_assert(map->nodes != NULL);

  if (old_nodes)
  {
    for (size_t i = 0; i < old_m_entries; i++)
    {
      nv_hashmap_node_t* node = &old_nodes[i];
      if (NV_NODE_OCCUPIED(*node))
      {
        SDL_UnlockMutex(map->mutex);
        nv_hashmap_insert(map, node->key, node->value, hash_fn_arg);
        SDL_LockMutex(map->mutex);
        _nv_hashmap_free(node->key);
      }
    }
    _nv_hashmap_free((void*)old_nodes);
    old_nodes = NULL;
  }

  SDL_UnlockMutex(map->mutex);
}

void
nv_hashmap_clear(nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  nv_hashmap_destroy(map);
  map->nodes   = NULL;
  map->size    = 0;
  map->entries = 0;
}

size_t
nv_hashmap_size(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->size;
}

size_t
nv_hashmap_capacity(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->entries;
}

size_t
nv_hashmap_keysize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->key_size;
}

size_t
nv_hashmap_valuesize(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->value_size;
}

nv_hashmap_node_t*
nv_hashmap_iterate(const nv_hashmap_t* map, size_t* __i)
{
  nv_assert(CONT_IS_VALID(map));
  for (; (*__i) < map->entries; (*__i)++)
  {
    size_t i = *__i;
    if (NV_NODE_OCCUPIED(map->nodes[i]))
    {
      (*__i)++;
      return &map->nodes[i];
    }
  }
  return NULL;
}

nv_hashmap_node_t*
nv_hashmap_root_node(const nv_hashmap_t* map)
{
  nv_assert(CONT_IS_VALID(map));
  return map->nodes;
}

void*
nv_hashmap_find(const nv_hashmap_t* NV_RESTRICT map, const void* NV_RESTRICT key, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex((SDL_mutex*)map->mutex);

  if (!map->nodes)
  {
    SDL_UnlockMutex((SDL_mutex*)map->mutex);
    return NULL;
  }

  const u32 hash  = map->hash_fn(key, map->key_size, hash_fn_arg);
  const u32 begin = power_of_two_mod(hash, map->entries);

  u32 index = begin;
  u32 probe = 1;

  while (NV_NODE_OCCUPIED(map->nodes[index]))
  {
    // if (map->equal_fn(map->nodes[index].key, key, map->key_size))
    if (map->nodes[index].hash == hash)
    {
      void* value = map->nodes[index].value;
      SDL_UnlockMutex((SDL_mutex*)map->mutex);
      return value;
    }
    index = power_of_two_mod((hash + probe + (probe * probe)), map->entries);
    if (index == begin)
    {
      break;
    }
    probe++;
  }

  SDL_UnlockMutex((SDL_mutex*)map->mutex);
  return NULL;
}

static inline void
_nv_hashmap_insert_internal(nv_hashmap_t* map, const void* NV_RESTRICT key, const void* NV_RESTRICT value, bool replace_if_exists, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->mutex);

  // the second check
  if (!map->nodes || (flt_t)map->size >= ((flt_t)map->entries * NV_HASHMAP_LOAD_FACTOR))
  {
    // The check to whether map->entries is greater than 0 is already done in
    // resize();
    SDL_UnlockMutex(map->mutex);
    nv_hashmap_resize(map, map->entries * 2, hash_fn_arg);
    SDL_LockMutex(map->mutex);
  }

  const u32 hash  = map->hash_fn(key, map->key_size, hash_fn_arg);
  const u32 begin = power_of_two_mod(hash, map->entries);

  u32 index = begin;
  u32 probe = 1;

  while (NV_NODE_OCCUPIED(map->nodes[index]))
  {
    index = power_of_two_mod((hash + probe + (probe * probe)), map->entries);
    if (hash == map->nodes[index].hash && nv_memcmp(map->nodes[index].key, key, map->key_size) == 0 && replace_if_exists)
    {
      nv_memcpy(map->nodes[index].value, value, map->value_size);
      return;
    }
    if (index == begin)
    {
      SDL_UnlockMutex(map->mutex);
      return;
    }
    probe++;
  }

  map->nodes[index].key   = _nv_hashmap_calloc(map->key_size + map->value_size);
  map->nodes[index].value = (char*)map->nodes[index].key + map->key_size;
  map->nodes[index].hash  = hash;

  nv_memcpy(map->nodes[index].key, key, map->key_size);
  nv_memcpy(map->nodes[index].value, value, map->value_size);
  map->size++;

  SDL_UnlockMutex(map->mutex);
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

  SDL_LockMutex(map->mutex);

  const size_t key_size = map->key_size;
  const size_t val_size = map->value_size;

  for (size_t i = 0; i < map->entries; i++)
  {
    if (NV_NODE_OCCUPIED(map->nodes[i]))
    {
      void* node_key   = map->nodes[i].key;
      void* node_value = map->nodes[i].value;

      fwrite(node_value, val_size, 1, f);
      fwrite(node_key, key_size, 1, f);
    }
  }

  SDL_UnlockMutex(map->mutex);
}

void
nv_hashmap_deserialize(nv_hashmap_t* map, FILE* f, void* hash_fn_arg)
{
  nv_assert(CONT_IS_VALID(map));

  SDL_LockMutex(map->mutex);

  void* key   = nv_malloc(map->key_size);
  void* value = nv_malloc(map->value_size);

  while (fread(value, map->value_size, 1, f) == 1 && fread(key, map->key_size, 1, f) == 1)
  {
    SDL_UnlockMutex(map->mutex);
    nv_hashmap_insert(map, key, value, hash_fn_arg);
    SDL_LockMutex(map->mutex);
  }

  nv_free(key);
  nv_free(value);

  SDL_UnlockMutex(map->mutex);
}

// ==============================
// ATLAS
// ==============================

nv_errorc
nv_texture_atlas_init(size_t width, size_t height, nv_format fmt, int padding, nv_texture_atlas_t* dst)
{
  nv_assert_and_ret(dst != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(width != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(height != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(padding >= 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(fmt != NOVA_FORMAT_UNDEFINED, NOVA_ERROR_CODE_INVALID_ARG);

  *dst = nv_zero_init(nv_texture_atlas_t);

  dst->canary  = CONT_CANARY;
  dst->width   = width;
  dst->height  = height;
  dst->format  = fmt;
  dst->padding = padding;
  dst->data    = (unsigned char*)nv_calloc(width * height * nv_format_get_bytes_per_pixel(dst->format));
  nv_assert_and_ret(dst->data != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  dst->mutex = SDL_CreateMutex();
  if (!dst->mutex)
  {
    return NOVA_ERROR_CODE_EXTERNAL;
  }

  if (nv_skyline_bin_init(width, height, &dst->bin) != 0)
  {
    return NOVA_ERROR_CODE_INVALID_RETVAL;
  }

  nv_assert_and_ret(CONT_IS_VALID(dst), NOVA_ERROR_CODE_BROKEN_STATE);

  return NOVA_ERROR_CODE_SUCCESS;
}

SDL_mutex* atlas_resize_mutex = NULL;

int
nv_texture_atlas_add(nv_texture_atlas_t* atlas, const nv_image_t* img, size_t* out_x, size_t* out_y)
{
  if (!atlas || !img || img->width <= 0 || img->height <= 0 || !out_x || !out_y)
  {
    return 0;
  }
  nv_assert(CONT_IS_VALID(atlas));

  if (!atlas_resize_mutex)
  {
    atlas_resize_mutex = SDL_CreateMutex();
  }

  SDL_LockMutex(atlas->mutex);

  nv_skyline_rect_t rect = { .width = img->width + (2 * atlas->padding), .height = img->height + (2 * atlas->padding) };

  size_t x, y;
  bool   packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);

  while (!packed)
  {
    SDL_UnlockMutex(atlas->mutex);

    SDL_LockMutex(atlas_resize_mutex);
    nv_texture_atlas_resize(atlas, 2);
    SDL_UnlockMutex(atlas_resize_mutex);

    SDL_LockMutex(atlas->mutex);

    packed = nv_skyline_bin_find_best_placement(&atlas->bin, &rect, &x, &y);
  }

  nv_skyline_bin_place_rect(&atlas->bin, &rect, x, y);
  *out_x = x + atlas->padding;
  *out_y = y + atlas->padding;

  nv_image_t dst = { .width = atlas->width, .height = atlas->height, .format = NOVA_FORMAT_R8, .data = atlas->data };
  nv_image_overlay(&dst, img, (int)*out_x, (int)*out_y, 0, 0);

  SDL_UnlockMutex(atlas->mutex);
  return 1;
}

void
nv_texture_atlas_resize(nv_texture_atlas_t* atlas, int scale)
{
  nv_assert(CONT_IS_VALID(atlas));
  SDL_LockMutex(atlas->mutex);

  if (atlas->width == 0 || atlas->height == 0)
  {
    nv_log_error("zero size atlas? possible corruption\n");
    SDL_UnlockMutex(atlas->mutex);
    return;
  }

  size_t old_w    = atlas->width;
  size_t old_h    = atlas->height;
  size_t new_w    = atlas->width * scale;
  size_t new_h    = atlas->height * scale;
  size_t channels = nv_format_get_bytes_per_pixel(atlas->format);

  unsigned char* new_data = nv_calloc(new_w * new_h * channels);
  nv_assert(new_data != NULL);

  if (atlas->data)
  {
    for (size_t y = 0; y < old_h; y++)
    {
      size_t src_offset = y * old_w * channels;
      size_t dst_offset = y * new_w * channels;
      nv_memcpy(&new_data[dst_offset], &atlas->data[src_offset], old_w * channels);
    }
  }

  nv_free(atlas->data);
  atlas->data   = new_data;
  atlas->width  = new_w;
  atlas->height = new_h;

  nv_skyline_bin_resize(&atlas->bin, new_w, new_h);

  SDL_UnlockMutex(atlas->mutex);
}

int
nv_texture_atlas_finish(nv_texture_atlas_t* atlas)
{
  nv_assert(CONT_IS_VALID(atlas));

  SDL_LockMutex(atlas->mutex);

  size_t max_w = 0;
  size_t max_h = 0;

  for (size_t i = 0; i < atlas->bin.num_rects; i++)
  {
    nv_skyline_rect_t* r = &atlas->bin.rects[i];
    max_w                = NV_MAX(max_w, r->posx + r->width);
    max_h                = NV_MAX(max_h, r->posy + r->height);
  }

  size_t optimal_w = max_w, optimal_h = max_h;

  if ((optimal_w == atlas->width && optimal_h == atlas->height) || (optimal_w == 0 || optimal_h == 0))
  {
    SDL_UnlockMutex(atlas->mutex);
    return 0;
  }

  if (atlas->width > optimal_w || atlas->height > optimal_h)
  {
    if (max_w == 0 || max_h == 0)
    {
      nv_log_error("0 optimal w/h??\n");
      return -1;
    }
    size_t channels = nv_format_get_bytes_per_pixel(atlas->format);
    if (channels == 0)
    {
      nv_log_error("invalid format?\n");
      return -1;
    }
    unsigned char* new_data = (unsigned char*)nv_calloc(max_w * max_h * channels);
    if (new_data)
    {
      for (size_t y = 0; y < max_h; y++)
      {
        nv_memcpy(new_data + y * max_w * channels, atlas->data + y * atlas->width * channels, max_w * channels);
      }
      nv_free(atlas->data);
      atlas->data   = new_data;
      atlas->width  = max_w;
      atlas->height = max_h;
    }
  }

  SDL_UnlockMutex(atlas->mutex);
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

  SDL_LockMutex(atlas->mutex);
  if (atlas->data)
  {
    nv_free(atlas->data);
  }
  nv_skyline_bin_destroy(&atlas->bin);
  SDL_UnlockMutex(atlas->mutex);

  SDL_DestroyMutex(atlas->mutex);
}

// ==============================
// RECTPACK
// ==============================

nv_errorc
nv_skyline_bin_init(size_t w, size_t h, nv_skyline_bin_t* dst)
{
  nv_assert_and_ret(dst != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(w != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(h != 0, NOVA_ERROR_CODE_INVALID_ARG);

  *dst = nv_zero_init(nv_skyline_bin_t);

  dst->canary = CONT_CANARY;
  dst->width  = w;
  dst->height = h;

  dst->rects                = NULL;
  dst->num_rects            = 0;
  dst->allocated_rect_count = 0;

  dst->skyline = (size_t*)nv_calloc(w * sizeof(size_t));
  nv_assert_and_ret(dst->skyline != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  dst->mutex = SDL_CreateMutex();
  nv_assert_and_ret(dst->mutex != NULL, NOVA_ERROR_CODE_EXTERNAL);

  nv_assert_and_ret(CONT_IS_VALID(dst), NOVA_ERROR_CODE_BROKEN_STATE);

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_skyline_bin_destroy(nv_skyline_bin_t* bin)
{
  if (!bin)
  {
    return;
  }
  nv_assert(CONT_IS_VALID(bin));
  SDL_LockMutex(bin->mutex);
  if (bin->rects)
  {
    nv_free(bin->rects);
  }
  if (bin->skyline)
  {
    nv_free(bin->skyline);
  }
  SDL_UnlockMutex(bin->mutex);
  SDL_DestroyMutex(bin->mutex);
}

size_t
nv_skyline_bin_max_height(const nv_skyline_bin_t* bin, size_t x, size_t w)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex((SDL_mutex*)bin->mutex);

  size_t max_h = 0;
  for (size_t i = x; i < x + w && i < bin->width; i++)
  {
    if (bin->skyline[i] > max_h)
    {
      max_h = bin->skyline[i];
    }
  }

  SDL_UnlockMutex((SDL_mutex*)bin->mutex);
  return max_h;
}

int
nv_skyline_bin_find_best_placement(const nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t* best_x, size_t* best_y)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex((SDL_mutex*)bin->mutex);

  size_t min_y = SIZE_MAX;
  *best_x      = SIZE_MAX;
  *best_y      = SIZE_MAX;

  if (rect->width > bin->width)
  {
    return -1;
  }

  size_t max_x = bin->width - rect->width;
  for (size_t x = 0; x <= max_x; x++)
  {
    SDL_UnlockMutex((SDL_mutex*)bin->mutex);
    size_t y = nv_skyline_bin_max_height(bin, x, rect->width);
    if (y + rect->height <= bin->height && y < min_y)
    {
      min_y   = y;
      *best_x = x;
      *best_y = y;
    }
    SDL_LockMutex((SDL_mutex*)bin->mutex);
  }

  SDL_UnlockMutex((SDL_mutex*)bin->mutex);
  return (*best_x != SIZE_MAX);
}

void
nv_skyline_bin_place_rect(nv_skyline_bin_t* bin, const nv_skyline_rect_t* rect, size_t x, size_t y)
{
  nv_assert(CONT_IS_VALID(bin));

  SDL_LockMutex(bin->mutex);

  if (bin->num_rects >= bin->allocated_rect_count)
  {
    size_t new_alloc = (bin->allocated_rect_count == 0) ? 2 : bin->allocated_rect_count * 2;

    if (bin->rects)
    {
      bin->rects = nv_realloc(bin->rects, new_alloc * sizeof(nv_skyline_rect_t));
    }
    else
    {
      bin->rects = nv_calloc(new_alloc * sizeof(nv_skyline_rect_t));
    }
    bin->allocated_rect_count = new_alloc;
  }

  bin->rects[bin->num_rects++] = (nv_skyline_rect_t){ rect->width, rect->height, x, y };

  for (size_t i = x; i < x + rect->width && i < bin->width; i++)
  {
    bin->skyline[i] = y + rect->height;
  }

  SDL_UnlockMutex(bin->mutex);
}

static int
_nv_skyline_compare_rect(const void* rect1, const void* rect2)
{
  size_t rect2_height = ((const nv_skyline_rect_t*)rect2)->height;
  size_t rect1_height = ((const nv_skyline_rect_t*)rect1)->height;
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
      rects[i].posx = x;
      rects[i].posy = y;
    }
    else
    {
      nv_log_error("failed to pack rect %d\n", (int)i);
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

  SDL_LockMutex(bin->mutex);

  for (size_t i = 0; i < bin->num_rects; i++)
  {
    nv_skyline_rect_t rect = bin->rects[i];
    if (rect.posx + rect.width > new_w || rect.posy + rect.height > new_h)
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
        SDL_UnlockMutex(bin->mutex);
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
        SDL_UnlockMutex(bin->mutex);
        return;
      }
      valid_rects              = tmp;
      valid_rects[num_valid++] = rect;
    }
  }

  if (new_w != bin->width)
  {
    size_t* new_skyline = (size_t*)nv_realloc(bin->skyline, new_w * sizeof(size_t));
    if (!new_skyline)
    {
      nv_log_error("Memory allocation failed for bin->skyline in nv_skyline_bin_resize\n");
      nv_free(valid_rects);
      nv_free(invalid_rects);
      SDL_UnlockMutex(bin->mutex);
      return;
    }
    // if it's bigger horizontally, clear the new entries
    if (new_w > bin->width)
    {
      for (size_t i = bin->width; i < new_w; i++)
      {
        new_skyline[i] = 0;
      }
    }
    bin->skyline = new_skyline;
  }

  for (size_t i = 0; i < new_w; i++)
  {
    if (bin->skyline[i] > new_h)
    {
      bin->skyline[i] = new_h;
    }
  }

  for (size_t i = 0; i < num_valid; i++)
  {
    nv_skyline_rect_t rect = valid_rects[i];
    for (size_t x = rect.posx; x < rect.posx + rect.width && x < new_w; x++)
    {
      if (bin->skyline[x] < rect.posy + rect.height)
      {
        bin->skyline[x] = rect.posy + rect.height;
      }
    }
  }

  if (bin->rects)
  {
    nv_free(bin->rects);
  }
  bin->rects                = valid_rects;
  bin->num_rects            = num_valid;
  bin->allocated_rect_count = num_valid;

  for (size_t i = 0; i < num_invalid; i++)
  {
    size_t x, y;
    if (nv_skyline_bin_find_best_placement(bin, &invalid_rects[i], &x, &y))
    {
      nv_skyline_bin_place_rect(bin, &invalid_rects[i], x, y);
    }
    else
    {
      nv_log_error("failed to repack rect %lu after resize\n", i);
    }
  }

  if (invalid_rects)
  {
    nv_free(invalid_rects);
  }

  bin->width  = new_w;
  bin->height = new_h;
  SDL_UnlockMutex(bin->mutex);
}

// ==============================
// BITSET
// ==============================

nv_errorc
nv_bitset_init(int init_capacity, nv_allocator_t* allocator, nv_bitset_t* set)
{
  nv_assert_and_ret(set != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(allocator != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(init_capacity >= 0, NOVA_ERROR_CODE_INVALID_ARG);

  *set = nv_zero_init(nv_bitset_t);

  set->mutex = SDL_CreateMutex();
  nv_assert_and_ret(set->mutex != NULL, NOVA_ERROR_CODE_EXTERNAL);

  if (init_capacity > 0)
  {
    init_capacity = (init_capacity + 7) / 8;
    set->size     = init_capacity;
    set->alloc    = allocator;

    set->data = set->alloc->calloc(set->alloc, 1, init_capacity * sizeof(uint8_t));
    nv_assert_and_ret(set->data != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);
  }
  else
  {
    set->size = 0;
  }

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_bitset_set_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->mutex);
  set->data[bitindex / 8] |= (1U << (bitindex % 8U));
  SDL_UnlockMutex(set->mutex);
}

void
nv_bitset_set_bit_to(nv_bitset_t* set, int bitindex, nv_bitset_bit to)
{
  to ? nv_bitset_set_bit(set, bitindex) : nv_bitset_clear_bit(set, bitindex);
}

void
nv_bitset_clear_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->mutex);
  set->data[bitindex / 8] &= ~(1U << (bitindex % 8U));
  SDL_UnlockMutex(set->mutex);
}

void
nv_bitset_toggle_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->mutex);
  set->data[bitindex / 8] ^= (1U << (bitindex % 8U));
  SDL_UnlockMutex(set->mutex);
}

nv_bitset_bit
nv_bitset_access_bit(nv_bitset_t* set, int bitindex)
{
  SDL_LockMutex(set->mutex);
  nv_bitset_bit bit = (set->data[bitindex / 8] & (1U << (bitindex % 8U))) != 0;
  SDL_UnlockMutex(set->mutex);
  return bit;
}

void
nv_bitset_copy_from(nv_bitset_t* dst, const nv_bitset_t* src)
{
  if (!src->data)
  {
    return;
  }
  SDL_LockMutex(dst->mutex);
  SDL_LockMutex((SDL_mutex*)src->mutex);
  if (src->size != dst->size && dst->data)
  {
    dst->alloc->free(dst->alloc, dst->data);
    dst->data = src->alloc->calloc(src->alloc, 1, src->size);
    dst->size = src->size;
  }
  if (dst->data && src->data)
  {
    nv_memcpy(dst->data, src->data, src->size);
  }
  SDL_UnlockMutex(dst->mutex);
  SDL_UnlockMutex((SDL_mutex*)src->mutex);
}

void
nv_bitset_destroy(nv_bitset_t* set)
{
  SDL_LockMutex(set->mutex);
  set->alloc->free(set->alloc, set->data);
  SDL_UnlockMutex(set->mutex);
}

void
nv_freelist_check_circle(const nv_freelist_t* list)
{
#ifndef NDEBUG
  SDL_LockMutex((SDL_mutex*)list->mutex);

  nv_node_t* node = list->root;
  nv_node_t* slow = node;
  nv_node_t* fast = node;

  while (fast && fast->next)
  {
    slow = slow->next;
    fast = fast->next->next;
    if (fast)
    {
      nv_assert(fast->canary == NOVA_ALLOCATION_CANARY);
    }
    if (slow)
    {
      nv_assert(slow->canary == NOVA_ALLOCATION_CANARY);
    }

    if (slow == fast)
    {
      nv_log_and_abort("circular freelist\n");
      return;
    }
  }
  SDL_UnlockMutex((SDL_mutex*)list->mutex);
#endif
}

nv_node_t*
nv_freelist_mknode(const nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  nv_node_t* node = list->alloc_fn(alignment, size);
  nv_assert(node != NULL);
  return node;
}

nv_errorc
nv_freelist_init(size_t init_size, nv_freelist_alloc_fn alloc_fn, nv_freelist_free_fn free_fn, nv_allocator_t* allocator, nv_freelist_t* list)
{
  nv_assert_and_ret(list != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(allocator != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(alloc_fn != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(free_fn != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  *list = nv_zero_init(nv_freelist_t);

  list->canary = CONT_CANARY;

  list->mutex = SDL_CreateMutex();
  nv_assert_and_ret(list->mutex != NULL, NOVA_ERROR_CODE_EXTERNAL);

  list->alloc_fn = alloc_fn;
  list->free_fn  = free_fn;
  if (init_size > 0)
  {
    list->root = nv_freelist_mknode(list, 1, init_size);
    nv_assert_and_ret(list->root, NOVA_ERROR_CODE_BROKEN_STATE);

    list->root->size = init_size;
  }
  else
  {
    list->root = NULL;
  }

  (void)allocator;
  nv_freelist_check_circle(list);

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_freelist_destroy(nv_freelist_t* list)
{
  if (!list || !list->root)
  {
    return;
  }

  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->mutex);

  nv_node_t* node = list->root;
  while (node)
  {
    nv_node_t* next = node->next;
    if (node->in_use)
    {
      SDL_UnlockMutex(list->mutex);
      nv_freelist_free(list, node->payload);
      SDL_LockMutex(list->mutex);
    }
    node = next;
  }
  SDL_UnlockMutex(list->mutex);

  nv_freelist_check_circle(list);
}

void*
nv_freelist_alloc(nv_freelist_t* list, size_t alignment, size_t size)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->mutex);

  nv_node_t* node = list->root;
  while (node)
  {
    size_t aligned_node_size = align_up_size(node->mapping_size, alignment);
    if (!node->in_use && aligned_node_size >= size)
    {
      node->in_use  = 1;
      node->payload = align_up(node->payload, alignment);
      nv_assert(((uintptr_t)node->payload % alignment) == 0);
      return node->payload;
    }
    node = node->next;
  }

  node         = nv_freelist_expand(list, alignment, size);
  node->in_use = 1;

  SDL_UnlockMutex(list->mutex);

  nv_freelist_check_circle(list);
  nv_assert(((uintptr_t)node->payload % alignment) == 0);
  nv_assert(node->payload != NULL);
  return node->payload;
}

nv_node_t*
nv_freelist_expand(nv_freelist_t* list, size_t alignment, size_t expand_by)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->mutex);

  if (!list->root)
  {
    list->root       = nv_freelist_mknode(list, alignment, expand_by);
    list->root->size = expand_by;
    SDL_UnlockMutex(list->mutex);
    return list->root;
  }

  nv_node_t* last_node = list->root;
  while (last_node->next)
  {
    last_node = last_node->next;
  }
  // now we have last_node

  SDL_UnlockMutex(list->mutex);

  nv_node_t* new_node = nv_freelist_mknode(list, alignment, expand_by);
  last_node->next     = new_node;
  new_node->size      = expand_by;
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

  SDL_LockMutex(list->mutex);

  bool       found = 0;
  nv_node_t* node  = list->root;
  nv_node_t* prev  = NULL;

  while (node)
  {
    if (block == node->payload)
    {
      found = 1;
      break;
    }
    prev = node;
    node = node->next;
  }

  if (!found)
  {
    nv_log_error("no block found\n");
    return;
  }

  if (!node->in_use)
  {
    /* real_t free, yeah */
    nv_log_error("double free\n");
    return;
  }

  node->in_use = 0;

  if (prev)
  {
    prev->next = node->next;
  }
  else
  {
    list->root = node->next;
  }

  list->free_fn(node);

  SDL_UnlockMutex(list->mutex);

  nv_freelist_check_circle(list);
}

nv_node_t*
nv_freelist_find(nv_freelist_t* list, void* alloc)
{
  nv_assert(CONT_IS_VALID(list));
  nv_freelist_check_circle(list);

  SDL_LockMutex(list->mutex);

  nv_node_t* node = list->root;
  while (node)
  {
    if (node->payload == alloc)
    {
      SDL_UnlockMutex(list->mutex);
      return node;
    }
    node = node->next;
  }
  SDL_UnlockMutex(list->mutex);
  nv_freelist_check_circle(list);
  return NULL;
}

// RBMAP

nv_errorc
nv_rbmap_init(size_t key_size, size_t val_size, nv_compare_fn compare_fn, nv_allocator_t* alloc, nv_rbmap_t* dst)
{
  nv_assert_and_ret(key_size != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(val_size != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(compare_fn != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(alloc != NULL, NOVA_ERROR_CODE_INVALID_ARG);

  *dst = nv_zero_init(nv_rbmap_t);

  dst->canary     = CONT_CANARY;
  dst->key_size   = key_size;
  dst->val_size   = val_size;
  dst->compare_fn = compare_fn;
  dst->root       = NULL;
  dst->alloc      = alloc;

  return NOVA_ERROR_CODE_SUCCESS;
}

void
nv_rbmap_left_rotate(nv_rbmap_t* map, nv_rbmap_node_t* x)
{
  nv_assert(CONT_IS_VALID(map));
  nv_rbmap_node_t* y = x->children[1];
  x->children[1]     = y->children[0];
  if (y->children[0])
  {
    y->children[0]->parent = x;
  }

  y->parent = x->parent;
  if (!x->parent)
  {
    map->root = y;
  }
  else
  {
    x->parent->children[x == x->parent->children[1]] = y;
  }

  y->children[0] = x;
  x->parent      = y;
}

void
nv_rbmap_right_rotate(nv_rbmap_t* map, nv_rbmap_node_t* y)
{
  nv_assert(CONT_IS_VALID(map));
  nv_rbmap_node_t* x = y->children[0];
  y->children[0]     = x->children[1];
  if (x->children[1])
  {
    x->children[1]->parent = y;
  }

  x->parent = y->parent;
  if (!y->parent)
  {
    map->root = x;
  }
  else
  {
    y->parent->children[y == y->parent->children[1]] = x;
  }

  x->children[1] = y;
  y->parent      = x;
}

nv_rbmap_node_t*
nv_rbmap_minimum(nv_rbmap_node_t* node)
{
  while (node && node->children[0] != NULL)
  {
    node = node->children[0];
  }
  return node;
}

void
nv_rbmap_transplant(nv_rbmap_t* map, nv_rbmap_node_t* u, nv_rbmap_node_t* v)
{
  nv_assert(CONT_IS_VALID(map));
  if (u->parent == NULL)
  {
    map->root = v;
  }
  else if (u == u->parent->children[0])
  {
    u->parent->children[0] = v;
  }
  else
  {
    u->parent->children[1] = v;
  }

  if (v != NULL)
  {
    v->parent = u->parent;
  }
}

void
nv_rbmap_delete_fixup(nv_rbmap_t* map, nv_rbmap_node_t* x)
{
  nv_assert(CONT_IS_VALID(map));
  while (x != map->root && (x == NULL || x->color == NOVA_RBNODE_COLOR_BLK))
  {
    if (x == x->parent->children[0])
    {
      nv_rbmap_node_t* w = x->parent->children[1];
      if (!w)
      {
        continue;
      }
      if (w && w->color == NOVA_RBNODE_COLOR_RED)
      {
        w->color         = NOVA_RBNODE_COLOR_BLK;
        x->parent->color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_left_rotate(map, x->parent);
        w = x->parent->children[1];
      }
      if (w && (w->children[0] == NULL || (w->children[0] && w->children[0]->color == NOVA_RBNODE_COLOR_BLK))
          && (w->children[1] == NULL || (w->children[1] && w->children[1]->color == NOVA_RBNODE_COLOR_BLK)))
      {
        w->color = NOVA_RBNODE_COLOR_RED;
        x        = x->parent;
      }
      else if (w)
      {
        if (w->children[1] == NULL || w->children[1]->color == NOVA_RBNODE_COLOR_BLK)
        {
          if (w->children[0])
          {
            w->children[0]->color = NOVA_RBNODE_COLOR_BLK;
          }
          w->color = NOVA_RBNODE_COLOR_RED;
          nv_rbmap_right_rotate(map, w);
          w = x->parent->children[1];
        }
        w->color         = x->parent->color;
        x->parent->color = NOVA_RBNODE_COLOR_BLK;
        if (w->children[1])
        {
          w->children[1]->color = NOVA_RBNODE_COLOR_BLK;
        }
        nv_rbmap_left_rotate(map, x->parent);
        x = map->root;
      }
    }
    else
    {
      nv_rbmap_node_t* w = x->parent->children[0];
      if (w && w->color == NOVA_RBNODE_COLOR_RED)
      {
        w->color         = NOVA_RBNODE_COLOR_BLK;
        x->parent->color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_right_rotate(map, x->parent);
        w = x->parent->children[0];
      }
      if ((w && (w->children[0] == NULL || w->children[0]->color == NOVA_RBNODE_COLOR_BLK) && (w->children[1] == NULL || w->children[1]->color == NOVA_RBNODE_COLOR_BLK)))
      {
        w->color = NOVA_RBNODE_COLOR_RED;
        x        = x->parent;
      }
      else if (w)
      {
        if (w->children[0] == NULL || w->children[0]->color == NOVA_RBNODE_COLOR_BLK)
        {
          if (w->children[1])
          {
            w->children[1]->color = NOVA_RBNODE_COLOR_BLK;
          }
          w->color = NOVA_RBNODE_COLOR_RED;
          nv_rbmap_left_rotate(map, w);
          w = x->parent->children[0];
        }
        w->color         = x->parent->color;
        x->parent->color = NOVA_RBNODE_COLOR_BLK;
        if (w->children[0])
        {
          w->children[0]->color = NOVA_RBNODE_COLOR_BLK;
        }
        nv_rbmap_right_rotate(map, x->parent);
        x = map->root;
      }
    }
  }
  if (x)
  {
    x->color = NOVA_RBNODE_COLOR_BLK;
  }
}

void
nv_rbmap_delete(nv_rbmap_t* map, nv_rbmap_node_t* z)
{
  nv_assert(CONT_IS_VALID(map));
  nv_assert(z != NULL);

  nv_rbmap_node_t* y                = z;
  int              y_original_color = y->color;
  nv_rbmap_node_t* x;

  if (z->children[0] == NULL)
  {
    x = z->children[1];
    nv_rbmap_transplant(map, z, z->children[1]);
  }
  else if (z->children[1] == NULL)
  {
    x = z->children[0];
    nv_rbmap_transplant(map, z, z->children[0]);
  }
  else
  {
    y                = nv_rbmap_minimum(z->children[1]);
    y_original_color = y->color;
    x                = y->children[1];
    if (y->parent == z)
    {
      if (x)
      {
        x->parent = y;
      }
    }
    else
    {
      nv_rbmap_transplant(map, y, y->children[1]);
      y->children[1] = z->children[1];
      if (y->children[1])
      {
        y->children[1]->parent = y;
      }
    }
    nv_rbmap_transplant(map, z, y);
    y->children[0] = z->children[0];
    if (y->children[0])
    {
      y->children[0]->parent = y;
    }
    y->color = z->color;
  }

  if (y_original_color == NOVA_RBNODE_COLOR_BLK && x != NULL)
  {
    nv_rbmap_delete_fixup(map, x);
  }

  map->alloc->free(map->alloc, z);
}

void
nv_rbmap_insert_fixup(nv_rbmap_t* map, nv_rbmap_node_t* z)
{
  nv_assert(CONT_IS_VALID(map));
  nv_assert(z != NULL);

  while (z->parent && z->parent->color == NOVA_RBNODE_COLOR_RED)
  {
    if (z->parent == z->parent->parent->children[0])
    {
      nv_rbmap_node_t* uncle = z->parent->parent->children[1];
      if (uncle && uncle->color == NOVA_RBNODE_COLOR_RED)
      {
        z->parent->color         = NOVA_RBNODE_COLOR_BLK;
        uncle->color             = NOVA_RBNODE_COLOR_BLK;
        z->parent->parent->color = NOVA_RBNODE_COLOR_RED;
        z                        = z->parent->parent;
      }
      else
      {
        if (z == z->parent->children[1])
        {
          z = z->parent;
          nv_rbmap_left_rotate(map, z);
        }
        z->parent->color         = NOVA_RBNODE_COLOR_BLK;
        z->parent->parent->color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_right_rotate(map, z->parent->parent);
      }
    }
    else
    {
      nv_rbmap_node_t* uncle = z->parent->parent->children[0];
      if (uncle && uncle->color == NOVA_RBNODE_COLOR_RED)
      {
        z->parent->color         = NOVA_RBNODE_COLOR_BLK;
        uncle->color             = NOVA_RBNODE_COLOR_BLK;
        z->parent->parent->color = NOVA_RBNODE_COLOR_RED;
        z                        = z->parent->parent;
      }
      else
      {
        if (z == z->parent->children[0])
        {
          z = z->parent;
          nv_rbmap_right_rotate(map, z);
        }
        z->parent->color         = NOVA_RBNODE_COLOR_BLK;
        z->parent->parent->color = NOVA_RBNODE_COLOR_RED;
        nv_rbmap_left_rotate(map, z->parent->parent);
      }
    }
  }
  map->root->color = NOVA_RBNODE_COLOR_BLK;
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

  void* allocation = map->alloc->calloc(map->alloc, 1, sizeof(nv_rbmap_node_t) + map->key_size + map->val_size);

  nv_rbmap_node_t* z = (nv_rbmap_node_t*)allocation;

  z->key = (char*)allocation + sizeof(nv_rbmap_node_t);
  z->val = (char*)allocation + sizeof(nv_rbmap_node_t) + map->key_size;
  nv_assert(z->val != NULL);

  nv_memcpy(z->key, key, map->key_size);
  nv_memcpy(z->val, value, map->val_size);

  z->color       = NOVA_RBNODE_COLOR_RED;
  z->parent      = NULL;
  z->children[0] = z->children[1] = NULL;

  nv_rbmap_node_t* y = NULL;
  nv_rbmap_node_t* x = map->root;
  while (x != NULL)
  {
    y       = x;
    int cmp = map->compare_fn(key, x->key, map->key_size, user_hash_data);
    if (cmp < 0)
    {
      x = x->children[0];
    }
    else
    {
      x = x->children[1];
    }
  }
  z->parent = y;
  if (y == NULL)
  {
    map->root = z;
  }
  else if (map->compare_fn(key, y->key, map->key_size, user_hash_data) < 0)
  {
    y->children[0] = z;
  }
  else
  {
    y->children[1] = z;
  }

  nv_rbmap_insert_fixup(map, z);
}

void
nv_rbmap_iterator_init(nv_rbmap_t* map, nv_rbmap_iterator_t* dst)
{
  nv_assert(dst != NULL);
  nv_assert(map != NULL);

  *dst          = nv_zero_init(nv_rbmap_iterator_t);
  dst->current  = map->root;
  dst->stack    = NULL;
  dst->capacity = 0;
  dst->top      = 0;
}

void
nv_rbmap_iterator_reserve(nv_rbmap_iterator_t* itr, size_t num_elems)
{
  nv_assert(itr != NULL);
  if (itr->stack)
  {
    itr->stack = (nv_rbmap_node_t**)nv_realloc(itr->stack, num_elems * sizeof(nv_rbmap_node_t*));
  }
  else
  {
    itr->stack = (nv_rbmap_node_t**)nv_calloc(num_elems * sizeof(nv_rbmap_node_t*));
  }
  itr->capacity = num_elems;
}

nv_rbmap_node_t*
nv_rbmap_iterator_next(nv_rbmap_iterator_t* itr)
{
  while (itr->current || itr->top > 0)
  {
    if (itr->current)
    {
      if (!itr->stack || itr->top >= itr->capacity)
      {
        itr->capacity = itr->capacity ? itr->capacity * 2 : 4;
        itr->stack    = (nv_rbmap_node_t**)nv_realloc(itr->stack, itr->capacity * sizeof(nv_rbmap_node_t*));
      }
      itr->stack[itr->top++] = itr->current;
      itr->current           = itr->current->children[0];
    }
    else
    {
      itr->current          = itr->stack[--itr->top];
      nv_rbmap_node_t* node = itr->current;
      itr->current          = itr->current->children[1];
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
  if (itr->stack)
  {
    nv_free(itr->stack);
  }
  itr->stack    = NULL;
  itr->capacity = 0;
}

void*
nv_rbmap_find(const nv_rbmap_t* map, const void* key, void* user_hash_data)
{
  nv_assert(CONT_IS_VALID(map));

  nv_rbmap_node_t* node = map->root;
  while (node != NULL)
  {
    int cmp = map->compare_fn(key, node->key, map->key_size, user_hash_data);
    if (cmp < 0)
    {
      node = node->children[0];
    }
    else if (cmp > 0)
    {
      node = node->children[1];
    }
    else
    {
      return node->val;
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
    map->alloc->free(map->alloc, node);
  }
  nv_free(nodes);
  nv_rbmap_iterator_destroy(&itr);
  map->root = NULL;
}

// POOL

nv_errorc
nv_pool_init(nv_pool_t* pool, size_t type_size, size_t capacity)
{
  nv_assert_and_ret(pool != NULL, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(type_size != 0, NOVA_ERROR_CODE_INVALID_ARG);
  nv_assert_and_ret(capacity != 0, NOVA_ERROR_CODE_INVALID_ARG);

  // object_size = ALIGN(object_size);

  pool->allocation = nv_calloc(capacity * type_size);
  nv_assert_and_ret(pool->allocation != NULL, NOVA_ERROR_CODE_MALLOC_FAILED);

  pool->free_list = NULL;

  // link all objects to freelist so we can use em
  for (size_t i = 0; i < capacity; i++)
  {
    void* object    = (char*)pool->allocation + (i * type_size);
    *(void**)object = pool->free_list; // link next free object
    pool->free_list = object;
  }

  pool->type_size  = type_size;
  pool->capacity   = capacity;
  pool->free_count = capacity;

  pool->mutex = SDL_CreateMutex();
  nv_assert_and_ret(pool->mutex, NOVA_ERROR_CODE_EXTERNAL);

  return 0;
}

void
nv_pool_destroy(nv_pool_t* pool)
{
  if (!pool)
  {
    return;
  }

  nv_free(pool->allocation);

  SDL_DestroyMutex(pool->mutex);

  pool->allocation = NULL;
  pool->free_list  = NULL;
  pool->free_count = 0;
  pool->capacity   = 0;
  pool->type_size  = 0;
}

void*
nv_pool_alloc(nv_pool_t* pool)
{
  if (!pool)
  {
    return NULL;
  }

  SDL_LockMutex(pool->mutex);

  if (pool->free_count == 0)
  {
    SDL_UnlockMutex(pool->mutex);
    return NULL; // pool full
  }

  void* object    = pool->free_list;
  pool->free_list = *(void**)object; // unlink from linked list
  pool->free_count--;

  SDL_UnlockMutex(pool->mutex);

  return object;
}

void
nv_pool_free(nv_pool_t* pool, void* object)
{
  if (!pool || !object)
  {
    return;
  }

  SDL_LockMutex(pool->mutex);

  *(void**)object = pool->free_list;
  pool->free_list = object;
  pool->free_count++;

  SDL_UnlockMutex(pool->mutex);
}
