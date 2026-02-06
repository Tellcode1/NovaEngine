#version 450 core

#include "../camera.glsl"

layout (push_constant) uniform push_constant_block {
    mat4 model;
    vec4 color;
    vec4 line_begin;
    vec4 line_end;
} pc;

void main() {
    vec3 v_vertices;
    if (gl_VertexIndex == 0) {
        v_vertices = vec3(pc.line_begin);
    } else {
        v_vertices = vec3(pc.line_end);
    }
    gl_Position = cam_ub.perspective * cam_ub.view * vec4(v_vertices, 1.0);
}