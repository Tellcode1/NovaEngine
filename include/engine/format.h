#ifndef ENGINE_FORMAT_H
#define ENGINE_FORMAT_H

// implementation: core.c

#include "../std/include/attributes.h"
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t VkFormat_;
typedef uint32_t SDL_Format_;

/**
 * The number in each enum defines the BITS PER CHANNEL of the format
 * For example, NOVA_FORMAT_R8 is a single channel, 8 bit (1 byte) format
 * Also, NOVA_FORMAT_RGBA32 is a quad channel, 32(bitsperchannel) * 4(pixels) bit pixel size format with each channel being given 32 bits (4 bytes)
 */
typedef enum nv_format
{
  NOVA_FORMAT_UNDEFINED = 0,

  // These are UNORM's by the way.
  // Because that's how images are normally handled in computers.
  NOVA_FORMAT_R8,
  NOVA_FORMAT_RG8,
  NOVA_FORMAT_RGB8,
  NOVA_FORMAT_RGBA8,

  NOVA_FORMAT_BGR8,
  NOVA_FORMAT_BGRA8,

  NOVA_FORMAT_RGB16,
  NOVA_FORMAT_RGBA16,
  NOVA_FORMAT_RG32,
  NOVA_FORMAT_RGB32,
  NOVA_FORMAT_RGBA32,

  NOVA_FORMAT_R8_SINT,
  NOVA_FORMAT_RG8_SINT,
  NOVA_FORMAT_RGB8_SINT,
  NOVA_FORMAT_RGBA8_SINT,

  NOVA_FORMAT_R8_UINT,
  NOVA_FORMAT_RG8_UINT,
  NOVA_FORMAT_RGB8_UINT,
  NOVA_FORMAT_RGBA8_UINT,

  NOVA_FORMAT_R8_SRGB,
  NOVA_FORMAT_RG8_SRGB,
  NOVA_FORMAT_RGB8_SRGB,
  NOVA_FORMAT_RGBA8_SRGB,

  NOVA_FORMAT_BGR8_SRGB,
  NOVA_FORMAT_BGRA8_SRGB,

  NOVA_FORMAT_D16,
  NOVA_FORMAT_D24,
  NOVA_FORMAT_D32,
  NOVA_FORMAT_D24_S8,
  NOVA_FORMAT_D32_S8,

  NOVA_FORMAT_BC1,
  NOVA_FORMAT_BC3,
  NOVA_FORMAT_BC7,
} nv_format;

extern VkFormat_   nv_format_to_vk_format(nv_format format) NOVA_ATTR_CONST;
extern SDL_Format_ nv_format_to_sdl_format(nv_format format) NOVA_ATTR_CONST;

extern nv_format nv_format_from_vk_format(VkFormat_ format) NOVA_ATTR_CONST;
extern nv_format nv_format_from_sdl_format(SDL_Format_ format) NOVA_ATTR_CONST;

/**
 * dst is a pointer to a const char *
 * like:
 * const char *str; nv_format_to_string(NOVA_FORMAT_R8, &str);
 * This function will never return NULL
 */
extern const char* nv_format_to_string(nv_format format) NOVA_ATTR_CONST;

extern bool nv_format_has_color_channel(nv_format fmt) NOVA_ATTR_CONST;

// Returns false even for stencil/depth and undefined format
extern bool nv_format_has_alpha_channel(nv_format fmt) NOVA_ATTR_CONST;

extern bool nv_format_has_depth_channel(nv_format fmt) NOVA_ATTR_CONST;

extern bool nv_format_has_stencil_channel(nv_format fmt) NOVA_ATTR_CONST;

// returns -1 on invalid format (or compressed formats)
// returns the size of depth component (in bytes) even in combined depth stencil formats
extern int nv_format_get_bytes_per_channel(nv_format fmt) NOVA_ATTR_CONST;

extern int nv_format_get_bytes_per_pixel(nv_format fmt) NOVA_ATTR_CONST;

// 0 on error or undefined format.
// oh and I haven't implemented compressed formats yet..
// also, stencil channels are also counted
extern int nv_format_get_num_channels(nv_format fmt) NOVA_ATTR_CONST;

#endif
