// output: Shaders/Debug/Line.frag.spv stage: frag name: Debug/Line/frag

#version 450 core

layout(location = 0) out vec4 o_color;

layout (push_constant) uniform push_constants {
    mat4 model;
    vec4 color;
    vec2 line_begin;
    vec2 line_end;
} pc;

void main() {
    o_color = pc.color;
}