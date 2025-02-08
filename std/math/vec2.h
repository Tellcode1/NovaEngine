#ifndef __CMATH_VEC2_H
#define __CMATH_VEC2_H

#include "../stdafx.h"
#include <math.h>

NOVA_HEADER_START;

typedef unsigned char v2bool;
typedef struct vec2 {
    float x,y;
} vec2;

static inline vec2 v2add(const vec2 v1, const vec2 v2) {
    return (vec2){v1.x + v2.x, v1.y + v2.y};
}

static inline vec2 v2sub(const vec2 v1, const vec2 v2) {
    return (vec2){v1.x - v2.x, v1.y - v2.y};
}

static inline vec2 v2mulv(const vec2 v1, const vec2 v2) {
    return (vec2){v1.x * v2.x, v1.y * v2.y};
}

static inline vec2 v2muls(const vec2 v1, const float s) {
    return (vec2){v1.x * s, v1.y * s};
}

static inline vec2 v2divs(const vec2 v1, const float s) {
    return (vec2){v1.x / s, v1.y / s};
}

// Circumvents the square root function
// this is useful when comparing the magnitude of a vector to be zero or 1
static inline float v2magcheap(const vec2 v) {
    return v.x * v.x + v.y * v.y;
}

static inline float v2mag(const vec2 v) {
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline vec2 v2normalize(const vec2 v) {
    float magnitude = v2mag(v);
    if (magnitude == 0) return (vec2){};
    return v2divs(v, magnitude);
}

// Normalizes the vector and deposits its magnitude in mg
static inline vec2 v2normalize2(const vec2 v, float *mg) {
    float magnitude = v2mag(v);
    *mg = magnitude;
    if (magnitude == 0) return (vec2){};
    return v2divs(v, magnitude);
}

static inline float v2dot(const vec2 v1, const vec2 v2) {
    return (v1.x * v2.x + v1.y * v2.y);
}

static inline v2bool v2areeq(const vec2 v1, const vec2 v2) {
    return (v2bool)( v1.x == v2.x && v1.y == v2.y  );
}

NOVA_HEADER_END;

#endif // __CMATH_VEC2_H