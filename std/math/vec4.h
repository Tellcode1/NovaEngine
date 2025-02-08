#ifndef __CMATH_VEC4_H
#define __CMATH_VEC4_H

#include "../stdafx.h"
#include <math.h>

NOVA_HEADER_START;

typedef unsigned char v4bool;
typedef struct vec4 {
    float x,y,z,w;
} vec4;

#define v4ptr(expr) (&(vec4){expr})

static inline vec4 v4add(const vec4 v1, const vec4 v2) {
    return (vec4){v1.x + v2.x, v1.y + v2.y, v1.z + v2.z, v1.w + v2.w};
}

static inline vec4 v4sub(const vec4 v1, const vec4 v2) {
    return (vec4){v1.x - v2.x, v1.y - v2.y, v1.z - v2.z, v1.w - v2.w};
}

static inline vec4 v4mulv(const vec4 v1, const vec4 v2) {
    return (vec4){v1.x * v2.x, v1.y * v2.y, v1.z * v2.z, v1.w * v2.w};
}

static inline vec4 v4muls(const vec4 v1, const float s) {
    return (vec4){v1.x * s, v1.y * s, v1.z * s, v1.w * s};
}

static inline vec4 v4divs(const vec4 v1, const float s) {
    return (vec4){v1.x / s, v1.y / s, v1.z / s, v1.w / s};
}

static inline float v4mag(const vec4 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
}

static inline vec4 v4normalize(const vec4 v) {
    float magnitude = v4mag(v);
    if (magnitude == 0) return (vec4){};
    return v4divs(v, magnitude);
}

static inline float v4dot(const vec4 v1, const vec4 v2) {
    return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z + v1.w * v2.w);
}

static inline v4bool v4areeq(const vec4 v1, const vec4 v2) {
    return (v4bool)( v1.x == v2.x && v1.y == v2.y && v1.z == v2.z && v1.w == v2.w  );
}

NOVA_HEADER_END;

#endif // __CMATH_VEC4_H