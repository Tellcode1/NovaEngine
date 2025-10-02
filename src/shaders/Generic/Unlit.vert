#version 450 core

#include "../camera.glsl"

layout(location=0) in
vec3 v_vertices;

layout(location=1) in
vec2 v_tex_coords;

layout(location=2) in
vec4 v_color;

layout(location=0) out
vec2 f_tex_coords;

layout(location=1) out
vec4 f_color;

layout (push_constant) uniform push_constant_block {
    mat4 model;
    vec4 color;
    vec2 tex_multiplier;
} pc;

void main() {
    f_tex_coords = v_tex_coords;
    f_color = v_color;
    gl_Position = cam_ub.ortho * cam_ub.view * pc.model * vec4(v_vertices, 1.0);
}