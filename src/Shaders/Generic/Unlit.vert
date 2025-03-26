// output: Shaders/Generic/Unlit.vert.spv stage: vert name: Unlit/vert

#version 450 core

layout(location=0) in
vec3 v_vertices;

layout(location=1) in
vec2 v_tex_coords;

layout(location=0) out
vec2 f_tex_coords;

layout(set = 0, binding = 0, std140) uniform camera_buffer {
    mat4 perspective;
    mat4 ortho;
    mat4 view;
} cam_ub;

layout (push_constant) uniform push_constants {
    mat4 model;
    vec4 color;
    vec2 tex_multiplier;
} pc;

void main() {
    f_tex_coords = v_tex_coords;
    gl_Position = cam_ub.ortho * cam_ub.view * pc.model * vec4(v_vertices, 1.0);
}