#ifndef _NOVA_ENGINE_AGNOSTIC_TEXTURE_HPP
#define _NOVA_ENGINE_AGNOSTIC_TEXTURE_HPP

#include "../std/stdafx.h"
#include "../std/types.h"

namespace gpu
{
struct texture
{
  u32 width, height, depth, mip_levels;
};
}

#endif //_NOVA_ENGINE_AGNOSTIC_TEXTURE_HPP
