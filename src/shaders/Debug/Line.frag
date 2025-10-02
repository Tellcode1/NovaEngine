#version 450 core

layout(location = 0) out vec4 o_color;

layout (push_constant) uniform push_constant_block {
    mat4 model;
    vec4 color;
    vec4 line_begin;
    vec4 line_end;
} pc;

void main() {
    o_color = pc.color;
}