#version 450 core

#include "../camera.glsl"

layout (push_constant) uniform push_constants {
    mat4 model;
    vec4 color;
    vec2 line_begin;
    vec2 line_end;
} pc;

void main() {
    vec2 v_vertices;
    if (gl_VertexIndex == 0) {
        v_vertices = pc.line_begin; 
    } else {
        v_vertices = pc.line_end;
    }
    gl_Position = cam_ub.ortho * cam_ub.view * pc.model * vec4(v_vertices, 0.0, 1.0);
}