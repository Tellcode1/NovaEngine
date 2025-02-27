#ifndef __C_MATH_H__
#define __C_MATH_H__

#include "vec2.h"
#include <stdbool.h>

NOVA_HEADER_START

static const real_t NVM_PI  = 3.1415926535897932385;
static const real_t NVM_2PI = 6.283185307179586;

// Multiply these to convert degrees to radians or vice versa
static const real_t NVM_DEG2RAD_CONSTANT = 0.017453292519943295; // 2Pi / 360.0
static const real_t NVM_RAD2DEG_CONSTANT = 57.29577951308232;    // 1.0 / DEG2RAD_CONSTANT -> rad / DEG2RAD_CONSTANT

#define NVM_LERP(a, b, t) (a + ((b - a) * t))

#define NVM_MAX(a, b) ((a) > ((NV_TYPEOF(a))(b)) ? (a) : ((NV_TYPEOF(a))(b)))
#define NVM_MIN(a, b) ((a) < ((NV_TYPEOF(a))(b)) ? (a) : ((NV_TYPEOF(a))(b)))

#define NVM_CLAMP(x, min, max) (((x) < (min)) ? (min) : (((x) > (max)) ? (max) : (x)))
#define NVM_CLAMP01(x) NVM_CLAMP((x), 0, 1)

// Round x to the nearest multiple of y
#define NVM_ROUND(x, y) (round((x) / (y)) * (y))

// The constants aren't used because for static const variables, they can cause minor issues like not compiling at all.
#define NVM_DEG2RAD(x) ((x) * 0.017453292519943295)
#define NVM_RAD2DEG(x) ((x) * 57.29577951308232)

typedef struct nvm_rect2d
{
  // The CENTER of the rect
  vec2 m_position;
  vec2 m_size;
} nvm_rect2d;

static inline bool
nvm_aabb(const nvm_rect2d* r1, const nvm_rect2d* r2)
{
  const vec2 r1_half_size = v2muls(r1->m_size, 0.5f);
  const vec2 r2_half_size = v2muls(r2->m_size, 0.5f);

  const vec2 r1_min = v2sub(r1->m_position, r1_half_size);
  const vec2 r1_max = v2add(r1->m_position, r1_half_size);

  const vec2 r2_min = v2sub(r2->m_position, r2_half_size);
  const vec2 r2_max = v2add(r2->m_position, r2_half_size);

  const bool overlaps_x = r1_max.x > r2_min.x && r1_min.x < r2_max.x;
  const bool overlaps_y = r1_max.y > r2_min.y && r1_min.y < r2_max.y;

  return overlaps_x && overlaps_y;
}

// Checks if a point is inside the rect
static inline bool
nvm_is_point_inside_rect(const vec2* point, const nvm_rect2d* r)
{
  const vec2 r_half_size = v2muls(r->m_size, 0.5f);

  const vec2 r_min = v2sub(r->m_position, r_half_size);
  const vec2 r_max = v2add(r->m_position, r_half_size);

  const bool overlaps_x = r_max.x > point->x && r_min.x < point->x;
  const bool overlaps_y = r_max.y > point->y && r_min.y < point->y;

  return overlaps_x && overlaps_y;
}

NOVA_HEADER_END

#endif //__C_MATH_H__
