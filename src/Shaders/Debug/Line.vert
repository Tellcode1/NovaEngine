// output: Shaders/Debug/Line.vert.spv stage: vert name: Debug/Line/vert

#version 450 core

layout(set = 0, binding = 0, std140) uniform camera_buffer {
    mat4 perspective;
    mat4 ortho;
    mat4 view;
} cam_ub;

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