#ifndef __MAT_H__
#define __MAT_H__

#include "../stdafx.h"
#include "vec3.h"
#include "vec4.h"

NOVA_HEADER_START;

typedef struct mat4 {
    vec4 data[4];
} mat4;

#define m4ptr(expr) ({ &(mat4){expr}; })

static inline mat4 m4init(float diag) {
    return (mat4){
        (vec4){diag, 0, 0, 0},
        (vec4){0, diag, 0, 0},
        (vec4){0, 0, diag, 0},
        (vec4){0, 0, 0, diag},
    };
}

static inline mat4 m4add(const mat4 m1, const mat4 m2) {
    mat4 result;
    for (int i = 0; i < 4; i++) {
        result.data[i] = v4add(m1.data[i], m2.data[i]);
    };
    return result;
}

static inline mat4 m4sub(const mat4 m1, const mat4 m2) {
    mat4 result;
    for (int i = 0; i < 4; i++) {
        result.data[i] = v4sub(m1.data[i], m2.data[i]);
    };
    return result;
}

static inline mat4 m4mul(const mat4 m1, const mat4 m2) {
    mat4 result;
    for (int i = 0; i < 4; i++) {
        result.data[i] = v4add(v4add(v4muls(m1.data[0], m2.data[i].x), v4muls(m1.data[1], m2.data[i].y)), v4add(v4muls(m1.data[2], m2.data[i].z), v4muls(m1.data[3], m2.data[i].w)));
    }
    return result;
}

static inline vec4 m4mulv4(const mat4 m, const vec4 v) {
    vec4 result;
    for (int i = 0; i < 4; i++) {
        ((float *)(&result))[i] = v4dot(m.data[i], v);
    };
    return result;
}

static inline mat4 m4scale(const mat4 m, const vec3 v) {
    mat4 matrix = m4init(1.0f);
    // for (int i = 0; i < 3; i++) {
    //     matrix.data[i] = v4muls(m.data[i], ((float *)&v)[i]);
    // }
    matrix.data[0] = v4muls(m.data[0], v.x);
    matrix.data[1] = v4muls(m.data[1], v.y);
    matrix.data[2] = v4muls(m.data[2], v.z);
    matrix.data[3] = m.data[3];
    return matrix;
}

static inline mat4 m4translate(const mat4 m, const vec3 v) {
    mat4 matrix = m4init(1.0f);
    matrix.data[3].x = v.x;
    matrix.data[3].y = v.y;
    matrix.data[3].z = v.z;
    return m4mul(m, matrix);
}

static inline mat4 m4rotate(const mat4 m, const float angle_rads, const vec3 v) {
    // I've seen box2d store the angles like this, maybe it's faster
    const float c = cosf(angle_rads);
    const float s = sinf(angle_rads);

    const vec3 axis = v3normalize(v);
    const vec3 temp = v3muls(axis, (1.0f - c));

    mat4 rotation;
    rotation.data[0].x = c + temp.x * axis.x;
    rotation.data[0].y = temp.x * axis.y + s * axis.z;
    rotation.data[0].z = temp.x * axis.z - s * axis.y;
    rotation.data[0].w = 0.0f;

    rotation.data[1].x = temp.y * axis.x - s * axis.z;
    rotation.data[1].y = c + temp.y * axis.y;
    rotation.data[1].z = temp.y * axis.z + s * axis.x;
    rotation.data[1].w = 0.0f;

    rotation.data[2].x = temp.z * axis.x + s * axis.y;
    rotation.data[2].y = temp.z * axis.y - s * axis.x;
    rotation.data[2].z = c + temp.z * axis.z;
    rotation.data[2].w = 0.0f;

    rotation.data[3] = (vec4){0.0f, 0.0f, 0.0f, 1.0f};

    mat4 result;
    for (int i = 0; i < 3; i++) {
        vec4 row_0 = v4muls(m.data[0], rotation.data[i].x);
        vec4 row_1 = v4muls(m.data[1], rotation.data[i].y);
        vec4 row_2 = v4muls(m.data[2], rotation.data[i].z);

        vec4 sum_01 = v4add(row_0, row_1);
        result.data[i] = v4add(sum_01, row_2);
    }

    result.data[3] = m.data[3];

    return result;
}

// Rotates a matrix along the three axes. The vector v must contain the magnitude for rotation in each axis, in radians`
static inline mat4 m4rotatev(const mat4 m, const vec3 v) {
    mat4 m1 = m4rotate( m, v.x, ((vec3){1.0f, 0.0f, 0.0f}) );
    mat4 m2 = m4rotate( m, v.y, ((vec3){0.0f, 1.0f, 0.0f}) );
    mat4 m3 = m4rotate( m, v.z, ((vec3){0.0f, 0.0f, 1.0f}) );
    return m4mul(m3, m4mul(m2, m1));
}

// Generates a view matrix
static inline mat4 m4lookat(const vec3 eye, const vec3 center, const vec3 up) {
    const vec3 f = v3normalize(v3sub(center, eye));
    const vec3 s = v3normalize(v3cross(f, up));
    const vec3 u = v3cross(s, f);

    mat4 ret = m4init(1.0f);
    ret.data[0].x = s.x;
    ret.data[1].x = s.y;
    ret.data[2].x = s.z;
    ret.data[0].y = u.x;
    ret.data[1].y = u.y;
    ret.data[2].y = u.z;
    ret.data[0].z = -f.x;
    ret.data[1].z = -f.y;
    ret.data[2].z = -f.z;
    ret.data[3].x = -v3dot(s, eye);
    ret.data[3].y = -v3dot(u, eye);
    ret.data[3].z = v3dot(f, eye);
    return ret;
}

static inline mat4 m4perspective(float fovradians, float aspect_ratio, float near, float far) {
    const float halftan = tanf(fovradians * 0.5f);

    mat4 result = {};
    result.data[0].x = 1.0f / (aspect_ratio * halftan);
    result.data[1].y = -(1.0f / (halftan)); // y flip
    result.data[2].z = -(far + near) / (far - near);
    result.data[2].w = -1.0f;
    result.data[3].z = -(2.0f * far * near) / (far - near);
    return result;
}

static inline mat4 m4ortho(float left, float right, float bottom, float top, float near, float far) {
    return (mat4){
      (vec4){ 2.0f / (right - left), 0.0f, 0.0f, 0.0f },
      (vec4){ 0.0f, 2.0f / (bottom - top), 0.0f, 0.0f },
      (vec4){ 0.0f, 0.0f, 1.0f / (near - far), 0.0f   },
      // I wonder if anyone uses such a small font for vscode haha
      (vec4){ -(right + left) / (right - left), -(bottom + top) / (bottom - top), near / (near - far), 1.0f }
    };
}

NOVA_HEADER_END;

#endif