#version 450 core

layout(location = 0) out vec4 o_color;

layout (location = 0) in
vec2 f_tex_coords;

layout (location = 1) in
vec4 f_color;

layout(set = 1, binding = 0) uniform sampler2D f_texture;

layout (push_constant) uniform push_constant_block {
    mat4 model;
    vec4 color;
    vec2 tex_multiplier;
} pc;

void main() {
    vec4 sampling = texture(f_texture, f_tex_coords * pc.tex_multiplier);
    if (sampling.a < 0.01) discard;
    o_color =  sampling * pc.color * f_color;
}