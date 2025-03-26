// output: Shaders/Generic/Unlit.frag.spv stage: frag name: Unlit/frag

#version 450 core

layout(location = 0) out vec4 o_color;

layout (location = 0) in
vec2 f_tex_coords;

layout(set = 1, binding = 0) uniform sampler2D f_texture;

layout (push_constant) uniform push_constants {
    mat4 model;
    vec4 color;
    vec2 tex_multiplier;
} pc;

void main() {
    o_color = texture(f_texture, f_tex_coords * pc.tex_multiplier) * pc.color;
}