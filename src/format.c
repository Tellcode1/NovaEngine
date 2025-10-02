#include "../include/engine/format.h"

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <stdint.h>

#include <SDL3_image/SDL_image.h>

const char*
nv_format_to_string(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_UNDEFINED: return "NOVA_FORMAT_UNDEFINED";
    case NOVA_FORMAT_R8: return "NOVA_FORMAT_R8";
    case NOVA_FORMAT_RG8: return "NOVA_FORMAT_RG8";
    case NOVA_FORMAT_RGB8: return "NOVA_FORMAT_RGB8";
    case NOVA_FORMAT_RGBA8: return "NOVA_FORMAT_RGBA8";
    case NOVA_FORMAT_BGR8: return "NOVA_FORMAT_BGR8";
    case NOVA_FORMAT_BGRA8: return "NOVA_FORMAT_BGRA8";
    case NOVA_FORMAT_RGB16: return "NOVA_FORMAT_RGB16";
    case NOVA_FORMAT_RGBA16: return "NOVA_FORMAT_RGBA16";
    case NOVA_FORMAT_RG32: return "NOVA_FORMAT_RG32";
    case NOVA_FORMAT_RGB32: return "NOVA_FORMAT_RGB32";
    case NOVA_FORMAT_RGBA32: return "NOVA_FORMAT_RGBA32";
    case NOVA_FORMAT_R8_SINT: return "NOVA_FORMAT_R8_SINT";
    case NOVA_FORMAT_RG8_SINT: return "NOVA_FORMAT_RG8_SINT";
    case NOVA_FORMAT_RGB8_SINT: return "NOVA_FORMAT_RGB8_SINT";
    case NOVA_FORMAT_RGBA8_SINT: return "NOVA_FORMAT_RGBA8_SINT";
    case NOVA_FORMAT_R8_UINT: return "NOVA_FORMAT_R8_UINT";
    case NOVA_FORMAT_RG8_UINT: return "NOVA_FORMAT_RG8_UINT";
    case NOVA_FORMAT_RGB8_UINT: return "NOVA_FORMAT_RGB8_UINT";
    case NOVA_FORMAT_RGBA8_UINT: return "NOVA_FORMAT_RGBA8_UINT";
    case NOVA_FORMAT_R8_SRGB: return "NOVA_FORMAT_R8_SRGB";
    case NOVA_FORMAT_RG8_SRGB: return "NOVA_FORMAT_RG8_SRGB";
    case NOVA_FORMAT_RGB8_SRGB: return "NOVA_FORMAT_RGB8_SRGB";
    case NOVA_FORMAT_RGBA8_SRGB: return "NOVA_FORMAT_RGBA8_SRGB";
    case NOVA_FORMAT_BGR8_SRGB: return "NOVA_FORMAT_BGR8_SRGB";
    case NOVA_FORMAT_BGRA8_SRGB: return "NOVA_FORMAT_BGRA8_SRGB";
    case NOVA_FORMAT_D16: return "NOVA_FORMAT_D16";
    case NOVA_FORMAT_D24: return "NOVA_FORMAT_D24";
    case NOVA_FORMAT_D32: return "NOVA_FORMAT_D32";
    case NOVA_FORMAT_D24_S8: return "NOVA_FORMAT_D24_S8";
    case NOVA_FORMAT_D32_S8: return "NOVA_FORMAT_D32_S8";
    case NOVA_FORMAT_BC1: return "NOVA_FORMAT_BC1";
    case NOVA_FORMAT_BC3: return "NOVA_FORMAT_BC3";
    case NOVA_FORMAT_BC7: return "NOVA_FORMAT_BC7";
    default: return "(NotAFormat)";
  }
  return "(NotAFormat)";
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
    case NOVA_FORMAT_UNDEFINED: return false;
    default: return true;
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
    case NOVA_FORMAT_BGRA8_SRGB: return true;
    default: return false;
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
    case NOVA_FORMAT_D32_S8: return true;

    default: return false;
  }
}

bool
nv_format_has_stencil_channel(nv_format fmt)
{
  switch (fmt)
  {
    case NOVA_FORMAT_D24_S8:
    case NOVA_FORMAT_D32_S8: return true;

    default: return false;
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

nv_format
nv_sdl_format_to_nv_format(SDL_Format_ format)
{
  switch (format)
  {
    /* TODO: Add more? I wasn't able to find any more though */
    case SDL_PIXELFORMAT_RGB24: return NOVA_FORMAT_RGB8;
    case SDL_PIXELFORMAT_RGBA32: return NOVA_FORMAT_RGBA8;
    case SDL_PIXELFORMAT_ABGR32: return NOVA_FORMAT_BGRA8;

    case SDL_PIXELFORMAT_YV12:
    case SDL_PIXELFORMAT_IYUV:
    default: return NOVA_FORMAT_UNDEFINED;
  }
}

SDL_Format_
nv_format_to_sdl_format(nv_format format)
{
  switch (format)
  {
    case NOVA_FORMAT_RGB8: return SDL_PIXELFORMAT_RGB24;
    case NOVA_FORMAT_RGBA8: return SDL_PIXELFORMAT_RGBA32;
    case NOVA_FORMAT_BGRA8: return SDL_PIXELFORMAT_ABGR32;
    default: return SDL_PIXELFORMAT_UNKNOWN;
  }
}
