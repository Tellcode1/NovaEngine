#ifndef __NOVA_SEMVER_H__
#define __NOVA_SEMVER_H__

#include "stdafx.h"

NOVA_HEADER_START

#define NV_SEMVER_MAJOR_VERSION_NUMBER_MAX 1023
#define NV_SEMVER_MINOR_VERSION_NUMBER_MAX 1023
#define NV_SEMVER_PATCH_VERSION_NUMBER_MAX 4095

typedef uint32_t version_t;

static inline version_t
nv_semver_pack_version(version_t major, version_t minor, version_t patch)
{
  nv_assert(major <= NV_SEMVER_MAJOR_VERSION_NUMBER_MAX);
  nv_assert(minor <= NV_SEMVER_MINOR_VERSION_NUMBER_MAX);
  nv_assert(patch <= NV_SEMVER_PATCH_VERSION_NUMBER_MAX);

  return (major << 22) | (minor << 12) | patch;
}

static inline void
nv_semver_unpack_version(version_t version, version_t* major, version_t* minor, version_t* patch)
{
  *major = (version >> 22) & 0x3FF;
  *minor = (version >> 12) & 0x3FF;
  *patch = version & 0xFFF;
}

NOVA_HEADER_END

#endif //__NOVA_SEMVER_H__
