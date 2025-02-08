#ifndef __CMATH_VEC3_H
#define __CMATH_VEC3_H

#include "../stdafx.h"
#include <math.h>

NOVA_HEADER_START;

typedef unsigned char v3bool;
typedef struct vec3 {
    float x,y,z;
} vec3;

static inline vec3 v3add(const vec3 v1, const vec3 v2) {
    return (vec3){v1.x + v2.x, v1.y + v2.y, v1.z + v2.z};
}

static inline vec3 v3sub(const vec3 v1, const vec3 v2) {
    return (vec3){v1.x - v2.x, v1.y - v2.y, v1.z - v2.z};
}

static inline vec3 v3mulv(const vec3 v1, const vec3 v2) {
    return (vec3){v1.x * v2.x, v1.y * v2.y, v1.z * v2.z};
}

static inline vec3 v3muls(const vec3 v1, const float s) {
    return (vec3){v1.x * s, v1.y * s, v1.z * s};
}

static inline vec3 v3divs(const vec3 v1, const float s) {
    return (vec3){v1.x / s, v1.y / s, v1.z / s};
}

static inline float v3magcheap(const vec3 v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

static inline float v3mag(const vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline vec3 v3normalize(const vec3 v) {
    float magnitude = v3mag(v);
    if (magnitude == 0) return (vec3){};
    return v3divs(v, magnitude);
}

static inline float v3dot(const vec3 v1, const vec3 v2) {
    return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
}

static inline vec3 v3cross(const vec3 v1, const vec3 v2) {
    return (vec3){
        v1.y * v2.z - v2.y * v1.z,
        v1.z * v2.x - v2.z * v1.x,
        v1.x * v2.y - v2.x * v1.y
    };
}

static inline v3bool v3areeq(const vec3 v1, const vec3 v2) {
    return (v3bool)( v1.x == v2.x && v1.y == v2.y && v1.z == v2.z  );
}

NOVA_HEADER_END;

#endif // __CMATH_VEC3_H