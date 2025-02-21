#ifndef __MAT_H__
#define __MAT_H__

#include "../../std/stdafx.h"
#include "vec2.h"
#include "vec3.h"
#include "vec4.h"

NOVA_HEADER_START

#define _NV_DECL_MAT4(NAME, FUNC, SIZE, TYPE, TYPE_PREFIX)                                                                                                                    \
  typedef struct NAME                                                                                                                                                         \
  {                                                                                                                                                                           \
    vec##SIZE##TYPE_PREFIX data[SIZE];                                                                                                                                        \
  } NAME;                                                                                                                                                                     \
                                                                                                                                                                              \
  static inline NAME FUNC##init(TYPE diag)                                                                                                                                    \
  {                                                                                                                                                                           \
    NAME result = nv_zero_init(NAME);                                                                                                                                         \
    for (int i = 0; i < SIZE; i++)                                                                                                                                            \
    {                                                                                                                                                                         \
      result.data[i]              = (vec##SIZE##TYPE_PREFIX){ 0 };                                                                                                            \
      ((TYPE*)&result.data[i])[i] = diag;                                                                                                                                     \
    }                                                                                                                                                                         \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##add(const NAME m1, const NAME m2)                                                                                                                  \
  {                                                                                                                                                                           \
    NAME result;                                                                                                                                                              \
    for (int i = 0; i < SIZE; i++)                                                                                                                                            \
    {                                                                                                                                                                         \
      result.data[i] = v##SIZE##TYPE_PREFIX##add(m1.data[i], m2.data[i]);                                                                                                     \
    }                                                                                                                                                                         \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##sub(const NAME m1, const NAME m2)                                                                                                                  \
  {                                                                                                                                                                           \
    NAME result;                                                                                                                                                              \
    for (int i = 0; i < SIZE; i++)                                                                                                                                            \
    {                                                                                                                                                                         \
      result.data[i] = v##SIZE##TYPE_PREFIX##sub(m1.data[i], m2.data[i]);                                                                                                     \
    }                                                                                                                                                                         \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##mul(const NAME m1, const NAME m2)                                                                                                                  \
  {                                                                                                                                                                           \
    NAME result = nv_zero_init(NAME);                                                                                                                                         \
    for (int i = 0; i < SIZE; i++)                                                                                                                                            \
    {                                                                                                                                                                         \
      result.data[i] = (vec##SIZE##TYPE_PREFIX){ 0 };                                                                                                                         \
      for (int j = 0; j < SIZE; j++)                                                                                                                                          \
      {                                                                                                                                                                       \
        result.data[i] = v##SIZE##TYPE_PREFIX##add(result.data[i], v##SIZE##TYPE_PREFIX##muls(m1.data[j], ((TYPE*)&m2.data[j])[i]));                                          \
      }                                                                                                                                                                       \
    }                                                                                                                                                                         \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline vec##SIZE##TYPE_PREFIX FUNC##mulv(const NAME m, const vec##SIZE##TYPE_PREFIX v)                                                                               \
  {                                                                                                                                                                           \
    vec##SIZE##TYPE_PREFIX result = nv_zero_init(vec##SIZE##TYPE_PREFIX);                                                                                                     \
    for (int i = 0; i < SIZE; i++)                                                                                                                                            \
    {                                                                                                                                                                         \
      ((TYPE*)&result)[i] = v##SIZE##TYPE_PREFIX##dot(m.data[i], v);                                                                                                          \
    }                                                                                                                                                                         \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
  static inline NAME FUNC##scale(const NAME m, const vec3##TYPE_PREFIX v)                                                                                                     \
  {                                                                                                                                                                           \
    NAME matrix = FUNC##init(1.0f);                                                                                                                                           \
    for (int i = 0; i < 3; i++)                                                                                                                                               \
    {                                                                                                                                                                         \
      matrix.data[i] = v4##TYPE_PREFIX##muls(m.data[i], ((TYPE*)&v)[i]);                                                                                                      \
    }                                                                                                                                                                         \
    matrix.data[3] = m.data[3];                                                                                                                                               \
    return matrix;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##translate(const NAME m, const vec3##TYPE_PREFIX v)                                                                                                 \
  {                                                                                                                                                                           \
    NAME matrix      = FUNC##init(1.0f);                                                                                                                                      \
    matrix.data[3].x = v.x;                                                                                                                                                   \
    matrix.data[3].y = v.y;                                                                                                                                                   \
    matrix.data[3].z = v.z;                                                                                                                                                   \
    return FUNC##mul(m, matrix);                                                                                                                                              \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##rotate(const NAME m, const TYPE angle_rads, const vec3##TYPE_PREFIX v)                                                                             \
  {                                                                                                                                                                           \
    const TYPE c = cos(angle_rads);                                                                                                                                           \
    const TYPE s = sin(angle_rads);                                                                                                                                           \
                                                                                                                                                                              \
    const vec3##TYPE_PREFIX axis = v3##TYPE_PREFIX##normalize(v);                                                                                                             \
    const vec3##TYPE_PREFIX temp = v3##TYPE_PREFIX##muls(axis, (1.0f - c));                                                                                                   \
                                                                                                                                                                              \
    NAME rotation;                                                                                                                                                            \
    rotation.data[0].x = c + temp.x * axis.x;                                                                                                                                 \
    rotation.data[0].y = temp.x * axis.y + s * axis.z;                                                                                                                        \
    rotation.data[0].z = temp.x * axis.z - s * axis.y;                                                                                                                        \
    rotation.data[0].w = 0.0f;                                                                                                                                                \
                                                                                                                                                                              \
    rotation.data[1].x = temp.y * axis.x - s * axis.z;                                                                                                                        \
    rotation.data[1].y = c + temp.y * axis.y;                                                                                                                                 \
    rotation.data[1].z = temp.y * axis.z + s * axis.x;                                                                                                                        \
    rotation.data[1].w = 0.0f;                                                                                                                                                \
                                                                                                                                                                              \
    rotation.data[2].x = temp.z * axis.x + s * axis.y;                                                                                                                        \
    rotation.data[2].y = temp.z * axis.y - s * axis.x;                                                                                                                        \
    rotation.data[2].z = c + temp.z * axis.z;                                                                                                                                 \
    rotation.data[2].w = 0.0f;                                                                                                                                                \
                                                                                                                                                                              \
    rotation.data[3] = (vec##SIZE##TYPE_PREFIX){ 0.0f, 0.0f, 0.0f, 1.0f };                                                                                                    \
                                                                                                                                                                              \
    NAME result;                                                                                                                                                              \
    for (int i = 0; i < 3; i++)                                                                                                                                               \
    {                                                                                                                                                                         \
      vec##SIZE##TYPE_PREFIX row_0 = v##SIZE##TYPE_PREFIX##muls(m.data[0], rotation.data[i].x);                                                                               \
      vec##SIZE##TYPE_PREFIX row_1 = v##SIZE##TYPE_PREFIX##muls(m.data[1], rotation.data[i].y);                                                                               \
      vec##SIZE##TYPE_PREFIX row_2 = v##SIZE##TYPE_PREFIX##muls(m.data[2], rotation.data[i].z);                                                                               \
                                                                                                                                                                              \
      vec##SIZE##TYPE_PREFIX sum_01 = v##SIZE##TYPE_PREFIX##add(row_0, row_1);                                                                                                \
      result.data[i]                = v##SIZE##TYPE_PREFIX##add(sum_01, row_2);                                                                                               \
    }                                                                                                                                                                         \
                                                                                                                                                                              \
    result.data[3] = m.data[3];                                                                                                                                               \
                                                                                                                                                                              \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  /* Rotates a matrix along the three axes. The vector v must contain the magnitude for rotation in \                                                                         \
   each axis, in radians */                                                                                                                                                   \
  static inline NAME FUNC##rotatev(const NAME m, const vec3##TYPE_PREFIX v)                                                                                                   \
  {                                                                                                                                                                           \
    NAME m1 = FUNC##rotate(m, v.x, ((vec3##TYPE_PREFIX){ 1.0f, 0.0f, 0.0f }));                                                                                                \
    NAME m2 = FUNC##rotate(m, v.y, ((vec3##TYPE_PREFIX){ 0.0f, 1.0f, 0.0f }));                                                                                                \
    NAME m3 = FUNC##rotate(m, v.z, ((vec3##TYPE_PREFIX){ 0.0f, 0.0f, 1.0f }));                                                                                                \
    return FUNC##mul(m3, FUNC##mul(m2, m1));                                                                                                                                  \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##lookat(const vec3##TYPE_PREFIX eye, const vec3##TYPE_PREFIX center, const vec3##TYPE_PREFIX up)                                                    \
  {                                                                                                                                                                           \
    const vec3##TYPE_PREFIX f = v3##TYPE_PREFIX##normalize(v3##TYPE_PREFIX##sub(center, eye));                                                                                \
    const vec3##TYPE_PREFIX s = v3##TYPE_PREFIX##normalize(v3##TYPE_PREFIX##cross(f, up));                                                                                    \
    const vec3##TYPE_PREFIX u = v3##TYPE_PREFIX##cross(s, f);                                                                                                                 \
                                                                                                                                                                              \
    NAME ret      = FUNC##init(1.0f);                                                                                                                                         \
    ret.data[0].x = s.x;                                                                                                                                                      \
    ret.data[1].x = s.y;                                                                                                                                                      \
    ret.data[2].x = s.z;                                                                                                                                                      \
    ret.data[0].y = u.x;                                                                                                                                                      \
    ret.data[1].y = u.y;                                                                                                                                                      \
    ret.data[2].y = u.z;                                                                                                                                                      \
    ret.data[0].z = -f.x;                                                                                                                                                     \
    ret.data[1].z = -f.y;                                                                                                                                                     \
    ret.data[2].z = -f.z;                                                                                                                                                     \
    ret.data[3].x = -v3##TYPE_PREFIX##dot(s, eye);                                                                                                                            \
    ret.data[3].y = -v3##TYPE_PREFIX##dot(u, eye);                                                                                                                            \
    ret.data[3].z = v3##TYPE_PREFIX##dot(f, eye);                                                                                                                             \
    return ret;                                                                                                                                                               \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##perspective(TYPE fovradians, TYPE aspect_ratio, TYPE near, TYPE far)                                                                               \
  {                                                                                                                                                                           \
    const TYPE halftan = tan(fovradians * 0.5f);                                                                                                                              \
                                                                                                                                                                              \
    NAME result      = nv_zero_init(NAME);                                                                                                                                    \
    result.data[0].x = 1.0f / (aspect_ratio * halftan);                                                                                                                       \
    result.data[1].y = -(1.0f / (halftan)); /* y flip */                                                                                                                      \
    result.data[2].z = -(far + near) / (far - near);                                                                                                                          \
    result.data[2].w = -1.0f;                                                                                                                                                 \
    result.data[3].z = -(2.0f * far * near) / (far - near);                                                                                                                   \
    return result;                                                                                                                                                            \
  }                                                                                                                                                                           \
                                                                                                                                                                              \
  static inline NAME FUNC##ortho(TYPE left, TYPE right, TYPE bottom, TYPE top, TYPE near, TYPE far)                                                                           \
  {                                                                                                                                                                           \
    return (NAME){ (vec4##TYPE_PREFIX){ 2.0f / (right - left), 0.0f, 0.0f, 0.0f },                                                                                            \
                   (vec4##TYPE_PREFIX){ 0.0f, 2.0f / (bottom - top), 0.0f, 0.0f },                                                                                            \
                   (vec4##TYPE_PREFIX){ 0.0f, 0.0f, 1.0f / (near - far), 0.0f },                                                                                              \
                   (vec4##TYPE_PREFIX){ -(right + left) / (right - left), -(bottom + top) / (bottom - top), near / (near - far), 1.0f } };                                    \
  }

_NV_DECL_MAT4(mat4f, m4f, 4, float, f)
_NV_DECL_MAT4(mat4d, m4d, 4, double, d)
_NV_DECL_MAT4(mat4, m4, 4, flt_t, )

#define NV_MATRIX_COPY(m1, m2)                                                                                                                                                \
  do                                                                                                                                                                          \
  {                                                                                                                                                                           \
    const int size = nv_arrlen(m1.data);                                                                                                                                      \
    for (int __nv_matrix_copy_i = 0; __nv_matrix_copy_i < (size); __nv_matrix_copy_i++)                                                                                       \
      for (int __nv_matrix_copy_j = 0; __nv_matrix_copy_j < (size); __nv_matrix_copy_j++)                                                                                     \
        ((&(m1).data[__nv_matrix_copy_i].x)[__nv_matrix_copy_j] = (&(m2).data[__nv_matrix_copy_i].x)[__nv_matrix_copy_j]);                                                    \
  } while (0)

NOVA_HEADER_END

#endif
