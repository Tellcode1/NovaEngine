#ifndef __CMATH_VEC2_H
#define __CMATH_VEC2_H

#include "../../std/stdafx.h"
#include <math.h>

NOVA_HEADER_START

#define _NV_DEC_VEC2(TYPE, FUNC, NAME, SQRT_FUNC)                                                                                                                             \
  typedef struct NAME                                                                                                                                                         \
  {                                                                                                                                                                           \
    TYPE x, y;                                                                                                                                                                \
  } NAME;                                                                                                                                                                     \
  static inline NAME FUNC##add(const NAME v1, const NAME v2) { return (NAME){ v1.x + v2.x, v1.y + v2.y }; }                                                                   \
  static inline NAME FUNC##sub(const NAME v1, const NAME v2) { return (NAME){ v1.x - v2.x, v1.y - v2.y }; }                                                                   \
  static inline NAME FUNC##mulv(const NAME v1, const NAME v2) { return (NAME){ v1.x * v2.x, v1.y * v2.y }; }                                                                  \
  static inline NAME FUNC##muls(const NAME v1, const TYPE s) { return (NAME){ v1.x * s, v1.y * s }; }                                                                         \
  static inline NAME FUNC##divs(const NAME v1, const TYPE s) { return (NAME){ v1.x / s, v1.y / s }; }                                                                         \
  static inline TYPE FUNC##magcheap(const NAME v) { return v.x * v.x + v.y * v.y; }                                                                                           \
  static inline TYPE FUNC##dot(const NAME v1, const NAME v2) { return (v1.x * v2.x + v1.y * v2.y); }                                                                          \
  static inline bool FUNC##areeq(const NAME v1, const NAME v2) { return (bool)(v1.x == v2.x && v1.y == v2.y); }                                                               \
  static inline TYPE FUNC##mag(const NAME v) { return SQRT_FUNC(v.x * v.x + v.y * v.y); }                                                                                     \
  static inline NAME FUNC##normalize(const NAME v)                                                                                                                            \
  {                                                                                                                                                                           \
    TYPE magnitude = FUNC##mag(v);                                                                                                                                            \
    if (magnitude == 0)                                                                                                                                                       \
      return nv_zero_init(NAME);                                                                                                                                              \
    return FUNC##divs(v, magnitude);                                                                                                                                          \
  }

_NV_DEC_VEC2(int, v2i, vec2i, sqrt)
_NV_DEC_VEC2(float, v2f, vec2f, sqrtf)
_NV_DEC_VEC2(double, v2d, vec2d, sqrt)
_NV_DEC_VEC2(flt_t, v2, vec2, sqrtf)

NOVA_HEADER_END

#endif // __CMATH_VEC2_H
